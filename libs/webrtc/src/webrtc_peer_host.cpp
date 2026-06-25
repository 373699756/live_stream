#include "webrtc_peer_host.h"

#include "dtls_transport.h"
#include "srtp_session.h"
#include "webrtc_session.h"
#include "webrtc_transport.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace webrtc_internal {
namespace {

class NativeWebrtcPeerHost;

bool IsSupportedCodec(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

std::atomic<uintptr_t> &NextPeerHostId() {
    static std::atomic<uintptr_t> next_peer_host_id{1};
    return next_peer_host_id;
}

std::mutex &PeerHostTableMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<uintptr_t, NativeWebrtcPeerHost *> &PeerHostTable() {
    static std::map<uintptr_t, NativeWebrtcPeerHost *> table;
    return table;
}

std::map<uintptr_t, uint32_t> &PeerHostActiveCallbacks() {
    static std::map<uintptr_t, uint32_t> active_callbacks;
    return active_callbacks;
}

std::map<uintptr_t, bool> &PeerHostClosingFlags() {
    static std::map<uintptr_t, bool> closing_flags;
    return closing_flags;
}

std::condition_variable &PeerHostTableCondition() {
    static std::condition_variable condition;
    return condition;
}

uintptr_t AllocatePeerHostId() {
    return NextPeerHostId().fetch_add(1);
}

void RegisterPeerHost(uintptr_t peer_host_id, NativeWebrtcPeerHost &peer_host) {
    if (peer_host_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> guard(PeerHostTableMutex());
    PeerHostTable()[peer_host_id] = &peer_host;
    PeerHostActiveCallbacks()[peer_host_id] = 0;
    PeerHostClosingFlags()[peer_host_id] = false;
}

void UnregisterPeerHost(uintptr_t peer_host_id) {
    std::unique_lock<std::mutex> guard(PeerHostTableMutex());
    PeerHostClosingFlags()[peer_host_id] = true;
    PeerHostTableCondition().wait(guard, [peer_host_id]() {
        const auto iter = PeerHostActiveCallbacks().find(peer_host_id);
        return iter == PeerHostActiveCallbacks().end() || iter->second == 0;
    });
    PeerHostTable().erase(peer_host_id);
    PeerHostActiveCallbacks().erase(peer_host_id);
    PeerHostClosingFlags().erase(peer_host_id);
}

NativeWebrtcPeerHost *EnterPeerHostCallback(uintptr_t peer_host_id) {
    std::lock_guard<std::mutex> guard(PeerHostTableMutex());
    const auto peer_host_iter = PeerHostTable().find(peer_host_id);
    const auto closing_iter = PeerHostClosingFlags().find(peer_host_id);
    if (peer_host_iter == PeerHostTable().end() ||
        closing_iter == PeerHostClosingFlags().end() || closing_iter->second) {
        return nullptr;
    }
    ++PeerHostActiveCallbacks()[peer_host_id];
    return peer_host_iter->second;
}

void LeavePeerHostCallback(uintptr_t peer_host_id) {
    std::lock_guard<std::mutex> guard(PeerHostTableMutex());
    auto iter = PeerHostActiveCallbacks().find(peer_host_id);
    if (iter == PeerHostActiveCallbacks().end() || iter->second == 0) {
        return;
    }
    --iter->second;
    if (iter->second == 0) {
        PeerHostTableCondition().notify_all();
    }
}

class NativeWebrtcPeerHost : public IWebrtcPeerHost {
public:
    NativeWebrtcPeerHost(INetIo *net_io, event::Loop *net_loop)
        : net_io_(net_io),
          net_loop_(net_loop),
          peer_host_id_(AllocatePeerHostId()) {
        RegisterPeerHost(peer_host_id_, *this);
    }

    ~NativeWebrtcPeerHost() override {
        UnregisterPeerHost(peer_host_id_);
        StopInternal();
    }

    bool Available() const override {
        return net_io_ != nullptr && net_loop_ != nullptr;
    }

    bool Start(const WebrtcOptions &options,
               const WebrtcPeerHostCallbacks &callbacks) override {
        std::lock_guard<std::mutex> guard(mutex_);
        DtlsFingerprint local_fingerprint;
        if (!DtlsTransport::LocalCertificateFingerprint(&local_fingerprint)) {
            return false;
        }
        CloseAllSessionsLocked();
        options_ = options;
        callbacks_ = callbacks;
        local_fingerprint_ = local_fingerprint;
        sessions_.clear();
        next_port_offset_ = 0;
        return true;
    }

    void Stop() override { StopInternal(); }

    bool ApplyOptions(const WebrtcOptions &options) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (options.session_timeout_ms != options_.session_timeout_ms ||
            options.send_queue_capacity != options_.send_queue_capacity ||
            options.send_workers != options_.send_workers ||
            options.local_port_base != options_.local_port_base) {
            return false;
        }
        options_ = options;
        return true;
    }

    bool CreatePeer(const WebrtcPeerInfo &peer) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (peer.peer_id.empty() || !IsSupportedCodec(peer.codec) ||
            sessions_.find(peer.peer_id) != sessions_.end()) {
            return false;
        }
        std::unique_ptr<WebrtcSession> session(new WebrtcSession(peer));
        sessions_.emplace(peer.peer_id, std::move(session));
        return true;
    }

    std::string HandleOffer(const WebrtcPeerInfo &peer,
                            const std::string &offer_sdp) override {
        WebrtcPeerHostCallbacks callbacks;
        std::string answer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer.peer_id);
            if (it == sessions_.end() || offer_sdp.empty()) {
                return std::string();
            }

            WebrtcSessionOfferContext context;
            context.options = options_;
            context.local_fingerprint = local_fingerprint_;
            context.net_io = net_io_;
            context.net_loop = net_loop_;
            context.udp_callbacks.user = reinterpret_cast<void *>(peer_host_id_);
            context.udp_callbacks.on_read = &NativeWebrtcPeerHost::OnUdpPacket;
            context.next_port_offset = next_port_offset_;
            context.timer_user = reinterpret_cast<void *>(peer_host_id_);
            context.on_dtls_timeout =
                &NativeWebrtcPeerHost::OnTransportDtlsTimeout;

            WebrtcOfferAnswer offer_answer;
            if (!it->second->HandleOffer(offer_sdp, context,
                                         offer_answer)) {
                return std::string();
            }
            next_port_offset_ = offer_answer.next_port_offset;
            answer = offer_answer.answer_sdp;
            callbacks = callbacks_;
        }
        NotifyPeerState(callbacks, peer.peer_id, WebrtcPeerState::kConnecting);
        return answer;
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = sessions_.find(candidate.peer_id);
        if (it == sessions_.end()) {
            return false;
        }
        return it->second->AddIceCandidate(candidate);
    }

    bool ClosePeer(const std::string &peer_id) override {
        WebrtcPeerHostCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end()) {
                return false;
            }
            it->second->Close();
            sessions_.erase(it);
            callbacks = callbacks_;
        }
        NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kClosed);
        return true;
    }

    bool HandleDtlsPacket(const std::string &peer_id, const uint8_t *data,
                          size_t size,
                          std::vector<uint8_t> &outgoing_dtls) override {
        WebrtcPeerHostCallbacks callbacks;
        bool connected_now = false;
        bool failed = false;
        std::string failure_error;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end()) {
                return false;
            }
            WebrtcDtlsOutput dtls_output;
            if (!it->second->ProcessDtlsPacket(data, size, dtls_output)) {
                failure_error = dtls_output.error.empty()
                                    ? "dtls_failed"
                                    : dtls_output.error;
                failed = FailSessionLocked(peer_id, callbacks);
            } else {
                outgoing_dtls = dtls_output.outgoing_dtls;
                connected_now = dtls_output.connected_now;
                callbacks = callbacks_;
            }
        }
        if (failed) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kFailed,
                            failure_error);
            return false;
        }
        if (connected_now) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kConnected);
        }
        return true;
    }

    bool HandleSrtcpPacket(const std::string &peer_id, const uint8_t *data,
                           size_t size) override {
        WebrtcPeerHostCallbacks callbacks;
        bool need_keyframe = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end()) {
                return false;
            }
            if (!it->second->HandleSrtcpPacket(data, size,
                                               need_keyframe)) {
                return false;
            }
            callbacks = callbacks_;
        }
        if (need_keyframe &&
            callbacks.OnPeerKeyframeRequest != nullptr) {
            callbacks.OnPeerKeyframeRequest(callbacks.user, peer_id.c_str());
        }
        return true;
    }

    bool SendRtpPacket(const WebrtcPeerInfo &peer,
                       const MediaFrame &frame,
                       const rtp::RtpPacketView &packet) override {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = sessions_.find(peer.peer_id);
        if (it == sessions_.end()) {
            return false;
        }
        return it->second->SendRtpPacket(frame, packet);
    }

    bool GetRtpSendParameters(
        const std::string &peer_id,
        WebrtcRtpSendParameters &parameters) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = sessions_.find(peer_id);
        if (it == sessions_.end()) {
            return false;
        }
        return it->second->GetRtpSendParameters(parameters);
    }

    bool FillPeerInfo(const std::string &peer_id,
                      WebrtcPeerInfo &peer) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = sessions_.find(peer_id);
        if (it == sessions_.end()) {
            return false;
        }
        it->second->FillPeerInfo(peer);
        return true;
    }

    void FillStats(WebrtcStats &stats) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        stats.ice_ready = stats.ice_ready ||
                          (net_io_ != nullptr && net_loop_ != nullptr);
        stats.dtls_ready = stats.dtls_ready ||
                           !local_fingerprint_.value.empty();
        stats.srtp_ready = stats.srtp_ready || SrtpSession::Available();
        for (const auto &item : sessions_) {
            item.second->FillStats(stats);
        }
    }

private:
    void StopInternal() {
        std::lock_guard<std::mutex> guard(mutex_);
        CloseAllSessionsLocked();
        sessions_.clear();
    }

    static void OnUdpPacket(void *user, UdpSocketId socket_id,
                            NetAddress peer, const uint8_t *data,
                            size_t size) {
        if (user == nullptr) {
            return;
        }
        const uintptr_t peer_host_id = reinterpret_cast<uintptr_t>(user);
        DispatchUdpPacket(peer_host_id, socket_id, std::move(peer), data, size);
    }

    static void OnTransportDtlsTimeout(void *user,
                                       const std::string &peer_id) {
        if (user == nullptr) {
            return;
        }
        const uintptr_t peer_host_id = reinterpret_cast<uintptr_t>(user);
        DispatchDtlsTimeout(peer_host_id, peer_id);
    }

    static void DispatchUdpPacket(uintptr_t peer_host_id, UdpSocketId socket_id,
                                  NetAddress peer, const uint8_t *data,
                                  size_t size) {
        NativeWebrtcPeerHost *peer_host = EnterPeerHostCallback(peer_host_id);
        if (peer_host == nullptr) {
            return;
        }
        peer_host->HandleUdpPacket(socket_id, std::move(peer), data, size);
        LeavePeerHostCallback(peer_host_id);
    }

    static void DispatchDtlsTimeout(uintptr_t peer_host_id,
                                    const std::string &peer_id) {
        NativeWebrtcPeerHost *peer_host = EnterPeerHostCallback(peer_host_id);
        if (peer_host == nullptr) {
            return;
        }
        peer_host->HandleDtlsTimeout(peer_id);
        LeavePeerHostCallback(peer_host_id);
    }

    void HandleUdpPacket(UdpSocketId socket_id, NetAddress peer,
                         const uint8_t *data, size_t size) {
        if (data == nullptr || size == 0) {
            return;
        }
        if (WebrtcTransport::IsIcePacket(data, size)) {
            HandleIcePacket(socket_id, std::move(peer), data, size);
            return;
        }
        if (WebrtcTransport::IsDtlsPacket(data, size)) {
            HandleDtlsUdpPacket(socket_id, data, size);
            return;
        }
        if (WebrtcTransport::IsRtcpPacket(data, size)) {
            HandleSrtcpUdpPacket(socket_id, data, size);
        }
    }

    void HandleIcePacket(UdpSocketId socket_id, NetAddress peer,
                         const uint8_t *data, size_t size) {
        WebrtcPeerHostCallbacks callbacks;
        std::string peer_id;
        bool connected_now = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            WebrtcSession *session =
                FindSessionBySocketLocked(socket_id, peer_id);
            if (session == nullptr) {
                return;
            }
            if (!session->HandleIcePacket(std::move(peer), data, size,
                                          connected_now)) {
                return;
            }
            if (connected_now) {
                callbacks = callbacks_;
            }
        }
        if (connected_now) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kConnecting);
        }
    }

    void HandleDtlsUdpPacket(UdpSocketId socket_id, const uint8_t *data,
                             size_t size) {
        WebrtcPeerHostCallbacks callbacks;
        std::string peer_id;
        bool connected_now = false;
        bool failed = false;
        std::string failure_error;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            WebrtcSession *session =
                FindSessionBySocketLocked(socket_id, peer_id);
            if (session == nullptr || !session->ice_connected()) {
                return;
            }
            WebrtcDtlsOutput dtls_output;
            const bool processed =
                session->ProcessDtlsPacket(data, size, dtls_output);
            const bool sent =
                processed && session->SendDtlsResult(dtls_output);
            if (!processed || !sent) {
                failure_error = dtls_output.error.empty()
                                    ? (processed ? "dtls_send_failed" : "dtls_failed")
                                    : dtls_output.error;
                failed = FailSessionLocked(peer_id, callbacks);
            } else {
                connected_now = dtls_output.connected_now;
                callbacks = callbacks_;
            }
        }
        if (failed) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kFailed,
                            failure_error);
            return;
        }
        if (connected_now) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kConnected);
        }
    }

    void HandleSrtcpUdpPacket(UdpSocketId socket_id, const uint8_t *data,
                              size_t size) {
        WebrtcPeerHostCallbacks callbacks;
        std::string peer_id;
        bool need_keyframe = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            WebrtcSession *session =
                FindSessionBySocketLocked(socket_id, peer_id);
            if (session == nullptr ||
                !session->HandleSrtcpPacket(data, size,
                                            need_keyframe)) {
                return;
            }
            callbacks = callbacks_;
        }
        if (need_keyframe &&
            callbacks.OnPeerKeyframeRequest != nullptr) {
            callbacks.OnPeerKeyframeRequest(callbacks.user, peer_id.c_str());
        }
    }

    void HandleDtlsTimeout(const std::string &peer_id) {
        WebrtcPeerHostCallbacks callbacks;
        bool connected_now = false;
        bool failed = false;
        std::string failure_error;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end()) {
                return;
            }
            WebrtcDtlsOutput dtls_output;
            const bool handled =
                it->second->HandleDtlsTimeout(dtls_output);
            const bool sent = handled && it->second->SendDtlsResult(dtls_output);
            if (!handled || !sent) {
                failure_error = dtls_output.error.empty()
                                    ? (handled ? "dtls_send_failed" : "dtls_timeout")
                                    : dtls_output.error;
                failed = FailSessionLocked(peer_id, callbacks);
            } else {
                connected_now = dtls_output.connected_now;
                callbacks = callbacks_;
            }
        }
        if (failed) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kFailed,
                            failure_error);
            return;
        }
        if (connected_now) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kConnected);
        }
    }

    WebrtcSession *FindSessionBySocketLocked(UdpSocketId socket_id,
                                             std::string &peer_id) {
        for (auto &item : sessions_) {
            if (item.second->MatchesSocket(socket_id)) {
                peer_id = item.first;
                return item.second.get();
            }
        }
        return nullptr;
    }

    bool FailSessionLocked(const std::string &peer_id,
                           WebrtcPeerHostCallbacks &callbacks) {
        auto it = sessions_.find(peer_id);
        if (it == sessions_.end()) {
            return false;
        }
        it->second->Close();
        sessions_.erase(it);
        callbacks = callbacks_;
        return true;
    }

    void CloseAllSessionsLocked() {
        for (auto &item : sessions_) {
            item.second->Close();
        }
    }

    static void NotifyPeerState(const WebrtcPeerHostCallbacks &callbacks,
                                const std::string &peer_id,
                                WebrtcPeerState state,
                                const std::string &last_error = std::string()) {
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer_id.c_str(),
                                         state, last_error.c_str());
        }
    }

    INetIo *net_io_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    uintptr_t peer_host_id_ = 0;
    mutable std::mutex mutex_;
    WebrtcPeerHostCallbacks callbacks_;
    WebrtcOptions options_;
    DtlsFingerprint local_fingerprint_;
    std::map<std::string, std::unique_ptr<WebrtcSession>> sessions_;
    uint32_t next_port_offset_ = 0;
};

}  // namespace

std::unique_ptr<IWebrtcPeerHost> CreateWebrtcPeerHost(
    INetIo *net_io,
    event::Loop *net_loop) {
    return std::unique_ptr<IWebrtcPeerHost>(
        new NativeWebrtcPeerHost(net_io, net_loop));
}

}  // namespace webrtc_internal
}  // namespace live_stream

#include "webrtc_engine.h"

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

class NativeWebrtcEngine;

bool IsSupportedCodec(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

std::atomic<uintptr_t> &NextEngineId() {
    static std::atomic<uintptr_t> next_engine_id{1};
    return next_engine_id;
}

std::mutex &EngineTableMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<uintptr_t, NativeWebrtcEngine *> &EngineTable() {
    static std::map<uintptr_t, NativeWebrtcEngine *> table;
    return table;
}

std::map<uintptr_t, uint32_t> &EngineActiveCallbacks() {
    static std::map<uintptr_t, uint32_t> active_callbacks;
    return active_callbacks;
}

std::map<uintptr_t, bool> &EngineClosingFlags() {
    static std::map<uintptr_t, bool> closing_flags;
    return closing_flags;
}

std::condition_variable &EngineTableCondition() {
    static std::condition_variable condition;
    return condition;
}

uintptr_t AllocateEngineId() {
    return NextEngineId().fetch_add(1);
}

void RegisterEngine(uintptr_t engine_id, NativeWebrtcEngine *engine) {
    if (engine_id == 0 || engine == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(EngineTableMutex());
    EngineTable()[engine_id] = engine;
    EngineActiveCallbacks()[engine_id] = 0;
    EngineClosingFlags()[engine_id] = false;
}

void UnregisterEngine(uintptr_t engine_id) {
    std::unique_lock<std::mutex> guard(EngineTableMutex());
    EngineClosingFlags()[engine_id] = true;
    EngineTableCondition().wait(guard, [engine_id]() {
        const auto iter = EngineActiveCallbacks().find(engine_id);
        return iter == EngineActiveCallbacks().end() || iter->second == 0;
    });
    EngineTable().erase(engine_id);
    EngineActiveCallbacks().erase(engine_id);
    EngineClosingFlags().erase(engine_id);
}

NativeWebrtcEngine *EnterEngineCallback(uintptr_t engine_id) {
    std::lock_guard<std::mutex> guard(EngineTableMutex());
    const auto engine_iter = EngineTable().find(engine_id);
    const auto closing_iter = EngineClosingFlags().find(engine_id);
    if (engine_iter == EngineTable().end() ||
        closing_iter == EngineClosingFlags().end() || closing_iter->second) {
        return nullptr;
    }
    ++EngineActiveCallbacks()[engine_id];
    return engine_iter->second;
}

void LeaveEngineCallback(uintptr_t engine_id) {
    std::lock_guard<std::mutex> guard(EngineTableMutex());
    auto iter = EngineActiveCallbacks().find(engine_id);
    if (iter == EngineActiveCallbacks().end() || iter->second == 0) {
        return;
    }
    --iter->second;
    if (iter->second == 0) {
        EngineTableCondition().notify_all();
    }
}

class NativeWebrtcEngine : public IWebrtcEngine {
public:
    NativeWebrtcEngine(INetEngine *net_engine, event::Loop *net_loop)
        : net_engine_(net_engine),
          net_loop_(net_loop),
          engine_id_(AllocateEngineId()) {
        RegisterEngine(engine_id_, this);
    }

    ~NativeWebrtcEngine() override {
        UnregisterEngine(engine_id_);
        StopInternal();
    }

    bool Available() const override {
        return net_engine_ != nullptr && net_loop_ != nullptr;
    }

    bool Start(const WebrtcOptions &options,
               const WebrtcEngineCallbacks &callbacks) override {
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
            options.send_worker_count != options_.send_worker_count ||
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
        WebrtcEngineCallbacks callbacks;
        std::string answer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer.peer_id);
            if (it == sessions_.end() || it->second == nullptr ||
                offer_sdp.empty()) {
                return std::string();
            }

            WebrtcSessionOfferContext context;
            context.options = options_;
            context.local_fingerprint = local_fingerprint_;
            context.net_engine = net_engine_;
            context.net_loop = net_loop_;
            context.udp_callbacks.user = reinterpret_cast<void *>(engine_id_);
            context.udp_callbacks.on_read = &NativeWebrtcEngine::OnUdpPacket;
            context.next_port_offset = next_port_offset_;
            context.timer_user = reinterpret_cast<void *>(engine_id_);
            context.on_dtls_timeout =
                &NativeWebrtcEngine::OnTransportDtlsTimeout;

            WebrtcSessionOfferResult result;
            if (!it->second->HandleOffer(offer_sdp, context, &result)) {
                return std::string();
            }
            next_port_offset_ = result.next_port_offset;
            answer = result.answer_sdp;
            callbacks = callbacks_;
        }
        NotifyPeerState(callbacks, peer.peer_id, WebrtcPeerState::kConnecting);
        return answer;
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = sessions_.find(candidate.peer_id);
        if (it == sessions_.end() || it->second == nullptr) {
            return false;
        }
        return it->second->AddIceCandidate(candidate);
    }

    bool ClosePeer(const std::string &peer_id) override {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end()) {
                return false;
            }
            if (it->second != nullptr) {
                it->second->Close();
            }
            sessions_.erase(it);
            callbacks = callbacks_;
        }
        NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kClosed);
        return true;
    }

    bool HandleDtlsPacket(const std::string &peer_id, const uint8_t *data,
                          size_t size,
                          std::vector<uint8_t> *outgoing_dtls) override {
        if (outgoing_dtls == nullptr) {
            return false;
        }
        WebrtcEngineCallbacks callbacks;
        bool connected_now = false;
        bool failed = false;
        std::string failure_error;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end() || it->second == nullptr) {
                return false;
            }
            WebrtcTransportDtlsResult result;
            if (!it->second->ProcessDtlsPacket(data, size, &result)) {
                failure_error = result.error.empty() ? "dtls_failed" : result.error;
                failed = FailSessionLocked(peer_id, &callbacks);
            } else {
                *outgoing_dtls = result.outgoing_dtls;
                connected_now = result.connected_now;
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
        WebrtcEngineCallbacks callbacks;
        bool need_keyframe = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end() || it->second == nullptr) {
                return false;
            }
            if (!it->second->HandleSrtcpPacket(data, size,
                                               &need_keyframe)) {
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
        if (it == sessions_.end() || it->second == nullptr) {
            return false;
        }
        return it->second->SendRtpPacket(frame, packet);
    }

    bool GetRtpSendParameters(
        const std::string &peer_id,
        WebrtcRtpSendParameters *parameters) const override {
        if (parameters == nullptr) {
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = sessions_.find(peer_id);
        if (it == sessions_.end() || it->second == nullptr) {
            return false;
        }
        return it->second->GetRtpSendParameters(parameters);
    }

    bool FillPeerInfo(const std::string &peer_id,
                      WebrtcPeerInfo *peer) const override {
        if (peer == nullptr) {
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = sessions_.find(peer_id);
        if (it == sessions_.end() || it->second == nullptr) {
            return false;
        }
        it->second->FillPeerInfo(peer);
        return true;
    }

    void FillStats(WebrtcStats *stats) const override {
        if (stats == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        stats->ice_ready = stats->ice_ready ||
                           (net_engine_ != nullptr && net_loop_ != nullptr);
        stats->dtls_ready = stats->dtls_ready ||
                            !local_fingerprint_.value.empty();
        stats->srtp_ready = stats->srtp_ready || SrtpSession::Available();
        for (const auto &item : sessions_) {
            if (item.second != nullptr) {
                item.second->FillStats(stats);
            }
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
        const uintptr_t engine_id = reinterpret_cast<uintptr_t>(user);
        DispatchUdpPacket(engine_id, socket_id, std::move(peer), data, size);
    }

    static void OnTransportDtlsTimeout(void *user,
                                       const std::string &peer_id) {
        if (user == nullptr) {
            return;
        }
        const uintptr_t engine_id = reinterpret_cast<uintptr_t>(user);
        DispatchDtlsTimeout(engine_id, peer_id);
    }

    static void DispatchUdpPacket(uintptr_t engine_id, UdpSocketId socket_id,
                                  NetAddress peer, const uint8_t *data,
                                  size_t size) {
        NativeWebrtcEngine *engine = EnterEngineCallback(engine_id);
        if (engine == nullptr) {
            return;
        }
        engine->HandleUdpPacket(socket_id, std::move(peer), data, size);
        LeaveEngineCallback(engine_id);
    }

    static void DispatchDtlsTimeout(uintptr_t engine_id,
                                    const std::string &peer_id) {
        NativeWebrtcEngine *engine = EnterEngineCallback(engine_id);
        if (engine == nullptr) {
            return;
        }
        engine->HandleDtlsTimeout(peer_id);
        LeaveEngineCallback(engine_id);
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
        WebrtcEngineCallbacks callbacks;
        std::string peer_id;
        bool connected_now = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            WebrtcSession *session =
                FindSessionBySocketLocked(socket_id, &peer_id);
            if (session == nullptr) {
                return;
            }
            if (!session->HandleIcePacket(std::move(peer), data, size,
                                          &connected_now)) {
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
        WebrtcEngineCallbacks callbacks;
        std::string peer_id;
        bool connected_now = false;
        bool failed = false;
        std::string failure_error;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            WebrtcSession *session =
                FindSessionBySocketLocked(socket_id, &peer_id);
            if (session == nullptr || !session->ice_connected()) {
                return;
            }
            WebrtcTransportDtlsResult result;
            const bool processed =
                session->ProcessDtlsPacket(data, size, &result);
            const bool sent = processed && session->SendDtlsResult(result);
            if (!processed || !sent) {
                failure_error = result.error.empty()
                                    ? (processed ? "dtls_send_failed" : "dtls_failed")
                                    : result.error;
                failed = FailSessionLocked(peer_id, &callbacks);
            } else {
                connected_now = result.connected_now;
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
        WebrtcEngineCallbacks callbacks;
        std::string peer_id;
        bool need_keyframe = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            WebrtcSession *session =
                FindSessionBySocketLocked(socket_id, &peer_id);
            if (session == nullptr ||
                !session->HandleSrtcpPacket(data, size,
                                            &need_keyframe)) {
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
        WebrtcEngineCallbacks callbacks;
        bool connected_now = false;
        bool failed = false;
        std::string failure_error;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = sessions_.find(peer_id);
            if (it == sessions_.end() || it->second == nullptr) {
                return;
            }
            WebrtcTransportDtlsResult result;
            const bool handled =
                it->second->HandleDtlsTimeout(&result);
            const bool sent = handled && it->second->SendDtlsResult(result);
            if (!handled || !sent) {
                failure_error = result.error.empty()
                                    ? (handled ? "dtls_send_failed" : "dtls_timeout")
                                    : result.error;
                failed = FailSessionLocked(peer_id, &callbacks);
            } else {
                connected_now = result.connected_now;
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
                                             std::string *peer_id) {
        for (auto &item : sessions_) {
            if (item.second != nullptr &&
                item.second->MatchesSocket(socket_id)) {
                if (peer_id != nullptr) {
                    *peer_id = item.first;
                }
                return item.second.get();
            }
        }
        return nullptr;
    }

    bool FailSessionLocked(const std::string &peer_id,
                           WebrtcEngineCallbacks *callbacks) {
        auto it = sessions_.find(peer_id);
        if (it == sessions_.end()) {
            return false;
        }
        if (it->second != nullptr) {
            it->second->Close();
        }
        sessions_.erase(it);
        if (callbacks != nullptr) {
            *callbacks = callbacks_;
        }
        return true;
    }

    void CloseAllSessionsLocked() {
        for (auto &item : sessions_) {
            if (item.second != nullptr) {
                item.second->Close();
            }
        }
    }

    static void NotifyPeerState(const WebrtcEngineCallbacks &callbacks,
                                const std::string &peer_id,
                                WebrtcPeerState state,
                                const std::string &last_error = std::string()) {
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer_id.c_str(),
                                         state, last_error.c_str());
        }
    }

    INetEngine *net_engine_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    uintptr_t engine_id_ = 0;
    mutable std::mutex mutex_;
    WebrtcEngineCallbacks callbacks_;
    WebrtcOptions options_;
    DtlsFingerprint local_fingerprint_;
    std::map<std::string, std::unique_ptr<WebrtcSession>> sessions_;
    uint32_t next_port_offset_ = 0;
};

}  // namespace

std::unique_ptr<IWebrtcEngine> CreateWebrtcEngine(
    INetEngine *net_engine,
    event::Loop *net_loop) {
    return std::unique_ptr<IWebrtcEngine>(
        new NativeWebrtcEngine(net_engine, net_loop));
}

}  // namespace webrtc_internal
}  // namespace live_stream

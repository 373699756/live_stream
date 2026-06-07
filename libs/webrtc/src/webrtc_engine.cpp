#include "webrtc_engine.h"

#include "dtls_transport.h"
#include "ice_transport.h"
#include "srtp_session.h"
#include "stun_packet.h"
#include "webrtc_sdp.h"

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

bool IsSupportedCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

bool IsRtcpPacket(const uint8_t *data, size_t size) {
    return data != nullptr && size >= 2 && data[0] >= 128 &&
           data[0] <= 191 && data[1] >= 192 && data[1] <= 223;
}

uint16_t PortWithOffset(uint16_t base_port, uint32_t offset) {
    const uint32_t port = static_cast<uint32_t>(base_port) + offset;
    if (port == 0 || port > 65535U) {
        return 0;
    }
    return static_cast<uint16_t>(port);
}

SrtpCryptoSuite ToSrtpCryptoSuite(DtlsSrtpCryptoSuite suite) {
    switch (suite) {
        case DtlsSrtpCryptoSuite::kAes128CmSha1_80:
            return SrtpCryptoSuite::kAes128CmSha180;
        case DtlsSrtpCryptoSuite::kNone:
            return SrtpCryptoSuite::kNone;
    }
    return SrtpCryptoSuite::kNone;
}

std::string LocalCandidateIp(const WebrtcOptions &options) {
    if (!options.public_ip.empty() && options.public_ip != "0.0.0.0") {
        return options.public_ip;
    }
    return std::string();
}

std::string BuildLocalIceUfrag(const std::string &peer_id) {
    return "ls" + std::to_string(BuildWebrtcSsrc(peer_id + ":ufrag"));
}

std::string BuildLocalIcePassword(const std::string &peer_id) {
    return "live_stream_" +
           std::to_string(BuildWebrtcSsrc(peer_id + ":pwd")) +
           std::to_string(BuildWebrtcSsrc(peer_id + ":ice"));
}

WebrtcSdpAnswerOptions BuildAnswerOptions(
    const WebrtcPeerInfo &peer,
    const WebrtcOptions &options,
    const DtlsFingerprint &local_fingerprint,
    const std::string &local_ice_ufrag,
    const std::string &local_ice_pwd,
    const NetAddress &local_candidate) {
    WebrtcSdpAnswerOptions answer_options;
    answer_options.peer_id = peer.peer_id;
    answer_options.local_codec = peer.codec;
    answer_options.local_ip = LocalCandidateIp(options);
    answer_options.local_candidate_ip = local_candidate.ip;
    answer_options.local_port = local_candidate.port;
    answer_options.local_ice_ufrag = local_ice_ufrag;
    answer_options.local_ice_pwd = local_ice_pwd;
    answer_options.local_fingerprint_hash =
        DtlsFingerprintAlgorithmName(local_fingerprint.algorithm);
    answer_options.local_fingerprint = local_fingerprint.value;
    answer_options.local_ssrc = BuildWebrtcSsrc(peer.peer_id);
    return answer_options;
}

std::atomic<uintptr_t> &NextEngineId() {
    static std::atomic<uintptr_t> next_engine_id{1};
    return next_engine_id;
}

std::mutex &EngineRegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<uintptr_t, NativeWebrtcEngine *> &EngineRegistry() {
    static std::map<uintptr_t, NativeWebrtcEngine *> registry;
    return registry;
}

std::map<uintptr_t, uint32_t> &EngineActiveCallbacks() {
    static std::map<uintptr_t, uint32_t> active_callbacks;
    return active_callbacks;
}

std::map<uintptr_t, bool> &EngineClosingFlags() {
    static std::map<uintptr_t, bool> closing_flags;
    return closing_flags;
}

std::condition_variable &EngineRegistryCondition() {
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
    std::lock_guard<std::mutex> guard(EngineRegistryMutex());
    EngineRegistry()[engine_id] = engine;
    EngineActiveCallbacks()[engine_id] = 0;
    EngineClosingFlags()[engine_id] = false;
}

void UnregisterEngine(uintptr_t engine_id) {
    std::unique_lock<std::mutex> guard(EngineRegistryMutex());
    EngineClosingFlags()[engine_id] = true;
    EngineRegistryCondition().wait(guard, [engine_id]() {
        const auto iter = EngineActiveCallbacks().find(engine_id);
        return iter == EngineActiveCallbacks().end() || iter->second == 0;
    });
    EngineRegistry().erase(engine_id);
    EngineActiveCallbacks().erase(engine_id);
    EngineClosingFlags().erase(engine_id);
}

NativeWebrtcEngine *EnterEngineCallback(uintptr_t engine_id) {
    std::lock_guard<std::mutex> guard(EngineRegistryMutex());
    const auto engine_iter = EngineRegistry().find(engine_id);
    const auto closing_iter = EngineClosingFlags().find(engine_id);
    if (engine_iter == EngineRegistry().end() ||
        closing_iter == EngineClosingFlags().end() || closing_iter->second) {
        return nullptr;
    }
    ++EngineActiveCallbacks()[engine_id];
    return engine_iter->second;
}

void LeaveEngineCallback(uintptr_t engine_id) {
    std::lock_guard<std::mutex> guard(EngineRegistryMutex());
    auto iter = EngineActiveCallbacks().find(engine_id);
    if (iter == EngineActiveCallbacks().end() || iter->second == 0) {
        return;
    }
    --iter->second;
    if (iter->second == 0) {
        EngineRegistryCondition().notify_all();
    }
}

class NativeWebrtcEngine : public IWebrtcEngine {
public:
    explicit NativeWebrtcEngine(NetEngine *net_engine)
        : net_engine_(net_engine),
          engine_id_(AllocateEngineId()) {
        RegisterEngine(engine_id_, this);
    }

    ~NativeWebrtcEngine() override {
        UnregisterEngine(engine_id_);
        Stop();
    }

    bool Available() const override { return net_engine_ != nullptr; }

    bool Start(const WebrtcOptions &options,
               const WebrtcEngineCallbacks &callbacks) override {
        std::lock_guard<std::mutex> guard(mutex_);
        DtlsFingerprint local_fingerprint;
        if (!DtlsTransport::LocalCertificateFingerprint(&local_fingerprint)) {
            return false;
        }
        CloseAllPeersLocked();
        options_ = options;
        callbacks_ = callbacks;
        local_fingerprint_ = local_fingerprint;
        peers_.clear();
        next_port_offset_ = 0;
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> guard(mutex_);
        CloseAllPeersLocked();
        peers_.clear();
    }

    bool CreatePeer(const WebrtcPeerInfo &peer) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (peer.peer_id.empty() || !IsSupportedCodec(peer.codec) ||
            peers_.find(peer.peer_id) != peers_.end()) {
            return false;
        }
        PeerRuntime runtime;
        runtime.peer = peer;
        peers_.emplace(peer.peer_id, std::move(runtime));
        return true;
    }

    std::string HandleOffer(const WebrtcPeerInfo &peer,
                            const std::string &offer_sdp) override {
        WebrtcEngineCallbacks callbacks;
        std::string answer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer.peer_id);
            if (it == peers_.end() || offer_sdp.empty()) {
                return std::string();
            }

            WebrtcSdpOffer parsed_offer;
            if (!ParseWebrtcOffer(offer_sdp, &parsed_offer)) {
                return std::string();
            }
            DtlsFingerprint remote_fingerprint;
            if (!ParseRemoteFingerprint(offer_sdp, &remote_fingerprint)) {
                return std::string();
            }

            std::unique_ptr<DtlsTransport> dtls(new DtlsTransport());
            if (!dtls->StartServer(remote_fingerprint)) {
                return std::string();
            }

            const std::string local_ice_ufrag =
                BuildLocalIceUfrag(peer.peer_id);
            const std::string local_ice_pwd =
                BuildLocalIcePassword(peer.peer_id);
            uint16_t local_port = 0;
            std::unique_ptr<IceTransport> ice;
            if (!StartIceTransportLocked(peer.peer_id, local_ice_ufrag,
                                         local_ice_pwd, &ice, &local_port)) {
                return std::string();
            }

            NetAddress local_candidate = ice->local_address();
            if (local_candidate.port == 0) {
                local_candidate.port = local_port;
            }
            const WebrtcSdpAnswerOptions answer_options = BuildAnswerOptions(
                peer, options_, local_fingerprint_, local_ice_ufrag,
                local_ice_pwd, local_candidate);
            answer = BuildWebrtcAnswer(parsed_offer, answer_options);
            if (answer.empty()) {
                return std::string();
            }

            it->second.offer_sdp = offer_sdp;
            it->second.answer_sdp = answer;
            it->second.offer = parsed_offer;
            it->second.remote_fingerprint = remote_fingerprint;
            it->second.local_ice_ufrag = local_ice_ufrag;
            it->second.local_ice_pwd = local_ice_pwd;
            it->second.ice = std::move(ice);
            it->second.dtls = std::move(dtls);
            it->second.rtp_payload_type =
                static_cast<uint8_t>(parsed_offer.video_codec.payload_type);
            it->second.rtp_clock_rate = parsed_offer.video_codec.clock_rate;
            it->second.rtp_ssrc = answer_options.local_ssrc;
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer.peer_id.c_str(),
                                         WebrtcPeerState::kConnecting);
        }
        return answer;
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = peers_.find(candidate.peer_id);
        if (it == peers_.end() || candidate.candidate.empty()) {
            return false;
        }
        it->second.has_remote_candidate = true;
        return true;
    }

    bool ClosePeer(const std::string &peer_id) override {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end()) {
                return false;
            }
            ClosePeerRuntimeLocked(&it->second);
            peers_.erase(it);
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer_id.c_str(),
                                         WebrtcPeerState::kClosed);
        }
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
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end()) {
                return false;
            }
            DtlsProcessResult result;
            if (!ProcessDtlsPacketLocked(&it->second, data, size, &result,
                                         &connected_now)) {
                failed = FailPeerLocked(peer_id, &callbacks);
            } else {
                *outgoing_dtls = result.outgoing_dtls;
                callbacks = callbacks_;
            }
        }
        if (failed) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kFailed);
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
        bool request_key_frame = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end()) {
                return false;
            }
            if (!HandleSrtcpPacketLocked(&it->second, data, size,
                                         &request_key_frame)) {
                return false;
            }
            callbacks = callbacks_;
        }
        if (request_key_frame &&
            callbacks.OnPeerKeyFrameRequested != nullptr) {
            callbacks.OnPeerKeyFrameRequested(callbacks.user, peer_id.c_str());
        }
        return true;
    }

    bool SendRtpPacket(const WebrtcPeerInfo &peer,
                       const EncodedFrame &frame,
                       const media_mux::RtpPacketView &packet) override {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = peers_.find(peer.peer_id);
        if (it == peers_.end() || it->second.ice == nullptr ||
            !it->second.ice->connected() ||
            !it->second.outbound_srtp.ready() ||
            !EncodedFrameHasPayload(&frame) || packet.Size() == 0 ||
            packet.ssrc == 0 || packet.payload_type == 0) {
            return false;
        }
        std::vector<uint8_t> protected_packet;
        if (!it->second.outbound_srtp.ProtectRtp(packet,
                                                 &protected_packet) ||
            protected_packet.empty() ||
            !it->second.ice->SendToSelected(protected_packet.data(),
                                            protected_packet.size())) {
            return false;
        }
        ++it->second.protected_rtp_packets;
        it->second.protected_rtp_bytes += protected_packet.size();
        return true;
    }

    bool GetRtpSendParameters(
        const std::string &peer_id,
        WebrtcRtpSendParameters *parameters) const override {
        if (parameters == nullptr) {
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = peers_.find(peer_id);
        if (it == peers_.end() || it->second.rtp_payload_type == 0 ||
            it->second.rtp_ssrc == 0) {
            return false;
        }
        parameters->codec = it->second.peer.codec;
        parameters->payload_type = it->second.rtp_payload_type;
        parameters->clock_rate = it->second.rtp_clock_rate;
        parameters->ssrc = it->second.rtp_ssrc;
        return true;
    }

    void FillStats(WebrtcStats *stats) const override {
        if (stats == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        stats->ice_ready = stats->ice_ready || net_engine_ != nullptr;
        stats->dtls_ready = stats->dtls_ready ||
                            !local_fingerprint_.value.empty();
        stats->srtp_ready = stats->srtp_ready || SrtpSession::Available();
        for (const auto &item : peers_) {
            const PeerRuntime &runtime = item.second;
            if (runtime.ice != nullptr && runtime.ice->connected()) {
                ++stats->selected_ice_pairs;
            }
        }
    }

private:
    struct PeerRuntime {
        WebrtcPeerInfo peer;
        std::string offer_sdp;
        std::string answer_sdp;
        WebrtcSdpOffer offer;
        DtlsFingerprint remote_fingerprint;
        std::string local_ice_ufrag;
        std::string local_ice_pwd;
        std::unique_ptr<IceTransport> ice;
        std::unique_ptr<DtlsTransport> dtls;
        SrtpSession outbound_srtp;
        SrtpSession inbound_srtp;
        NetTimerId dtls_timer_id = 0;
        uint64_t protected_rtp_packets = 0;
        uint64_t protected_rtp_bytes = 0;
        uint8_t rtp_payload_type = 0;
        uint32_t rtp_clock_rate = media_mux::kRtpClockRate;
        uint32_t rtp_ssrc = 0;
        bool has_remote_candidate = false;
        bool ice_connected = false;
        bool dtls_connected = false;
    };

    static void OnUdpPacket(void *user, UdpSocketId socket_id,
                            NetAddress peer, const uint8_t *data,
                            size_t size) {
        if (user == nullptr) {
            return;
        }
        const uintptr_t engine_id = reinterpret_cast<uintptr_t>(user);
        DispatchUdpPacket(engine_id, socket_id, std::move(peer), data, size);
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

    bool StartIceTransportLocked(const std::string &peer_id,
                                 const std::string &local_ufrag,
                                 const std::string &local_password,
                                 std::unique_ptr<IceTransport> *ice,
                                 uint16_t *local_port) {
        if (net_engine_ == nullptr || ice == nullptr || local_port == nullptr ||
            local_ufrag.empty() || local_password.empty()) {
            return false;
        }

        UdpCallbacks callbacks;
        callbacks.user = reinterpret_cast<void *>(engine_id_);
        callbacks.on_read = &NativeWebrtcEngine::OnUdpPacket;

        const uint32_t port_count =
            options_.max_peers == 0 ? 1U : options_.max_peers;
        for (uint32_t i = 0; i < port_count; ++i) {
            const uint32_t offset = (next_port_offset_ + i) % port_count;
            const uint16_t port =
                PortWithOffset(options_.local_port_base, offset);
            if (port == 0) {
                continue;
            }
            std::unique_ptr<IceTransport> candidate(
                new IceTransport(peer_id));
            if (!candidate->Start(net_engine_, callbacks, "0.0.0.0", port,
                                  local_ufrag, local_password)) {
                continue;
            }
            const NetAddress local_address = candidate->local_address();
            if (local_address.port == 0) {
                candidate->Stop();
                continue;
            }
            next_port_offset_ = (offset + 1) % port_count;
            *local_port = local_address.port;
            *ice = std::move(candidate);
            return true;
        }
        return false;
    }

    void HandleUdpPacket(UdpSocketId socket_id, NetAddress peer,
                         const uint8_t *data, size_t size) {
        if (data == nullptr || size == 0) {
            return;
        }
        if (IsStunPacket(data, size)) {
            HandleIcePacket(socket_id, std::move(peer), data, size);
            return;
        }
        if (DtlsTransport::IsDtlsPacket(data, size)) {
            HandleDtlsUdpPacket(socket_id, data, size);
            return;
        }
        if (IsRtcpPacket(data, size)) {
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
            PeerRuntime *runtime = FindPeerBySocketLocked(socket_id, &peer_id);
            if (runtime == nullptr || runtime->ice == nullptr) {
                return;
            }
            if (!runtime->ice->HandleUdpPacket(std::move(peer), data, size,
                                               &connected_now)) {
                return;
            }
            if (connected_now) {
                runtime->ice_connected = true;
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
        {
            std::lock_guard<std::mutex> guard(mutex_);
            PeerRuntime *runtime = FindPeerBySocketLocked(socket_id, &peer_id);
            if (runtime == nullptr || runtime->ice == nullptr ||
                !runtime->ice->connected()) {
                return;
            }
            DtlsProcessResult result;
            if (!ProcessDtlsPacketLocked(runtime, data, size, &result,
                                         &connected_now) ||
                !SendDtlsResultLocked(runtime, result)) {
                failed = FailPeerLocked(peer_id, &callbacks);
            } else {
                callbacks = callbacks_;
            }
        }
        if (failed) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kFailed);
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
        bool request_key_frame = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            PeerRuntime *runtime = FindPeerBySocketLocked(socket_id, &peer_id);
            if (runtime == nullptr ||
                !HandleSrtcpPacketLocked(runtime, data, size,
                                         &request_key_frame)) {
                return;
            }
            callbacks = callbacks_;
        }
        if (request_key_frame &&
            callbacks.OnPeerKeyFrameRequested != nullptr) {
            callbacks.OnPeerKeyFrameRequested(callbacks.user, peer_id.c_str());
        }
    }

    bool ProcessDtlsPacketLocked(PeerRuntime *runtime, const uint8_t *data,
                                 size_t size, DtlsProcessResult *result,
                                 bool *connected_now) {
        if (runtime == nullptr || runtime->dtls == nullptr ||
            result == nullptr || connected_now == nullptr) {
            return false;
        }
        *connected_now = false;
        if (!runtime->dtls->ProcessPacket(data, size, result)) {
            return false;
        }
        if (result->state == DtlsState::kConnected &&
            !runtime->dtls_connected) {
            if (!StartSrtpLocked(runtime, result->srtp_keys)) {
                return false;
            }
            runtime->dtls_connected = true;
            CancelDtlsTimerLocked(runtime);
            *connected_now = true;
        } else if (result->state == DtlsState::kConnecting) {
            if (!ArmDtlsTimerLocked(runtime->peer.peer_id, runtime)) {
                return false;
            }
        }
        return true;
    }

    void HandleDtlsTimeout(const std::string &peer_id) {
        WebrtcEngineCallbacks callbacks;
        bool connected_now = false;
        bool failed = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end() || it->second.dtls == nullptr) {
                return;
            }
            it->second.dtls_timer_id = 0;
            DtlsProcessResult result;
            if (!it->second.dtls->HandleTimeout(&result) ||
                !SendDtlsResultLocked(&it->second, result)) {
                failed = FailPeerLocked(peer_id, &callbacks);
            } else if (result.state == DtlsState::kConnected &&
                       !it->second.dtls_connected) {
                if (!StartSrtpLocked(&it->second, result.srtp_keys)) {
                    failed = FailPeerLocked(peer_id, &callbacks);
                } else {
                    it->second.dtls_connected = true;
                    connected_now = true;
                    callbacks = callbacks_;
                }
            } else if (result.state == DtlsState::kConnecting) {
                if (!ArmDtlsTimerLocked(peer_id, &it->second)) {
                    failed = FailPeerLocked(peer_id, &callbacks);
                }
            }
        }
        if (failed) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kFailed);
            return;
        }
        if (connected_now) {
            NotifyPeerState(callbacks, peer_id, WebrtcPeerState::kConnected);
        }
    }

    bool HandleSrtcpPacketLocked(PeerRuntime *runtime, const uint8_t *data,
                                 size_t size, bool *request_key_frame) {
        if (runtime == nullptr || request_key_frame == nullptr ||
            !runtime->inbound_srtp.ready() || data == nullptr || size == 0) {
            return false;
        }
        std::vector<uint8_t> plain_rtcp;
        if (!runtime->inbound_srtp.UnprotectRtcp(data, size, &plain_rtcp)) {
            return false;
        }
        RtcpFeedback feedback;
        if (!SrtpSession::ParseRtcpFeedback(plain_rtcp.data(),
                                            plain_rtcp.size(), &feedback)) {
            *request_key_frame = false;
            return true;
        }
        *request_key_frame = SrtpSession::IsKeyFrameRequest(feedback.type);
        return true;
    }

    bool SendDtlsResultLocked(PeerRuntime *runtime,
                              const DtlsProcessResult &result) {
        if (result.outgoing_dtls.empty()) {
            return true;
        }
        return runtime != nullptr && runtime->ice != nullptr &&
               runtime->ice->connected() &&
               runtime->ice->SendToSelected(result.outgoing_dtls.data(),
                                            result.outgoing_dtls.size());
    }

    bool ArmDtlsTimerLocked(const std::string &peer_id,
                            PeerRuntime *runtime) {
        if (net_engine_ == nullptr || runtime == nullptr ||
            runtime->dtls == nullptr ||
            runtime->dtls->state() != DtlsState::kConnecting) {
            return true;
        }
        uint32_t timeout_ms = 0;
        if (!runtime->dtls->GetHandshakeTimeoutMs(&timeout_ms)) {
            return true;
        }
        CancelDtlsTimerLocked(runtime);
        const uintptr_t engine_id = engine_id_;
        const NetTimerId timer_id = net_engine_->RunOnIoAfter(
            timeout_ms, [engine_id, peer_id]() {
                DispatchDtlsTimeout(engine_id, peer_id);
            });
        if (timer_id == 0) {
            return false;
        }
        runtime->dtls_timer_id = timer_id;
        return true;
    }

    void CancelDtlsTimerLocked(PeerRuntime *runtime) {
        if (runtime == nullptr || runtime->dtls_timer_id == 0) {
            return;
        }
        if (net_engine_ != nullptr) {
            (void)net_engine_->CancelIoTimer(runtime->dtls_timer_id);
        }
        runtime->dtls_timer_id = 0;
    }

    bool StartSrtpLocked(PeerRuntime *runtime, const DtlsSrtpKeys &keys) {
        if (runtime == nullptr) {
            return false;
        }
        const SrtpCryptoSuite suite = ToSrtpCryptoSuite(keys.suite);
        if (suite == SrtpCryptoSuite::kNone ||
            keys.local_master_key.empty() ||
            keys.remote_master_key.empty()) {
            return false;
        }
        if (!runtime->outbound_srtp.Start(SrtpDirection::kOutbound, suite,
                                          keys.local_master_key)) {
            return false;
        }
        if (!runtime->inbound_srtp.Start(SrtpDirection::kInbound, suite,
                                         keys.remote_master_key)) {
            runtime->outbound_srtp.Close();
            return false;
        }
        return true;
    }

    PeerRuntime *FindPeerBySocketLocked(UdpSocketId socket_id,
                                        std::string *peer_id) {
        for (auto &item : peers_) {
            if (item.second.ice != nullptr &&
                item.second.ice->socket_id() == socket_id) {
                if (peer_id != nullptr) {
                    *peer_id = item.first;
                }
                return &item.second;
            }
        }
        return nullptr;
    }

    bool FailPeerLocked(const std::string &peer_id,
                        WebrtcEngineCallbacks *callbacks) {
        auto it = peers_.find(peer_id);
        if (it == peers_.end()) {
            return false;
        }
        ClosePeerRuntimeLocked(&it->second);
        peers_.erase(it);
        if (callbacks != nullptr) {
            *callbacks = callbacks_;
        }
        return true;
    }

    void CloseAllPeersLocked() {
        for (auto &item : peers_) {
            ClosePeerRuntimeLocked(&item.second);
        }
    }

    void ClosePeerRuntimeLocked(PeerRuntime *runtime) {
        if (runtime == nullptr) {
            return;
        }
        CancelDtlsTimerLocked(runtime);
        runtime->outbound_srtp.Close();
        runtime->inbound_srtp.Close();
        if (runtime->dtls != nullptr) {
            runtime->dtls->Close();
        }
        if (runtime->ice != nullptr) {
            runtime->ice->Stop();
        }
        runtime->dtls_connected = false;
        runtime->ice_connected = false;
    }

    static void NotifyPeerState(const WebrtcEngineCallbacks &callbacks,
                                const std::string &peer_id,
                                WebrtcPeerState state) {
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer_id.c_str(),
                                         state);
        }
    }

    NetEngine *net_engine_ = nullptr;
    uintptr_t engine_id_ = 0;
    mutable std::mutex mutex_;
    WebrtcEngineCallbacks callbacks_;
    WebrtcOptions options_;
    DtlsFingerprint local_fingerprint_;
    std::map<std::string, PeerRuntime> peers_;
    uint32_t next_port_offset_ = 0;
};

}  // namespace

std::unique_ptr<IWebrtcEngine> CreateEngine(bool use_fake_engine,
                                           NetEngine *net_engine) {
    (void)use_fake_engine;
    return std::unique_ptr<IWebrtcEngine>(new NativeWebrtcEngine(net_engine));
}

}  // namespace webrtc_internal
}  // namespace live_stream

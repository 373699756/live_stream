#include "webrtc.h"

#include "infra/log.h"
#include "media/media_source_registry.h"
#include "media/media_streams.h"
#include "socket_io.h"
#include "runtime.h"
#include "webrtc_callback_guard.h"
#include "webrtc_peer_event.h"
#include "webrtc_peer_host.h"
#include "webrtc_peer_table.h"
#include "webrtc_peer_video_sender.h"
#include "webrtc_sdp.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

constexpr int64_t kPeerSetupTimeoutMs = 10000;

enum class WebrtcPhase {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

bool IsValidOptions(const WebrtcOptions &options) {
    if (options.max_peers == 0 || options.session_timeout_ms == 0 ||
        options.send_queue_capacity == 0 || options.send_workers == 0 ||
        options.local_port_base == 0) {
        return false;
    }
    for (const auto &server : options.ice_servers) {
        if (!webrtc_internal::IsValidIceServerUrl(server.url)) {
            return false;
        }
    }
    return true;
}

bool IsValidStream(StreamId stream_id) {
    return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
}

bool IsWebrtcCodecSupported(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

WebrtcPeerInfo CreatePeerError(const std::string &last_error) {
    WebrtcPeerInfo peer;
    peer.last_error = last_error;
    return peer;
}

}  // namespace

class WebrtcImpl : public IWebrtc {
public:
    WebrtcImpl(WebrtcOptions options,
               event::Loop *socket_loop)
        : options_(std::move(options)),
          media_streams_(MediaSourceRegistry::Streams()),
          socket_io_(Runtime::SocketIo()),
          net_loop_(socket_loop),
          event_(Runtime::EventCenter()),
          callback_guard_(new WebrtcCallbackGuard()),
          peer_video_sender_(media_streams_, net_loop_, callback_guard_,
                             &mutex_, &peer_table_, &peer_host_, &stats_),
          peer_event_(event_, &mutex_, &peer_table_) {
        {
            std::lock_guard<std::mutex> guard(callback_guard_->mutex);
            callback_guard_->service = this;
        }
    }

    ~WebrtcImpl() override { Release(); }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ == WebrtcPhase::kStarted) {
            return true;
        }
        if (phase_ == WebrtcPhase::kStopped) {
            phase_ = WebrtcPhase::kInitialized;
        }
        if (phase_ != WebrtcPhase::kInitialized) {
            return false;
        }
        if (!options_.enabled) {
            phase_ = WebrtcPhase::kStarted;
            return true;
        }
        phase_ = WebrtcPhase::kStarted;
        return true;
    }

    void Stop() override {
        std::vector<std::string> peer_ids;
        std::vector<WebrtcPeerVideoSender::ClosedVideo> closed_video;
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        {
            std::unique_lock<std::mutex> guard(mutex_);
            if (phase_ != WebrtcPhase::kStarted) {
                return;
            }
            phase_ = WebrtcPhase::kStopped;
            peer_ids = peer_table_.MarkAllClosing();
            peer_host = peer_host_;
        }

        closed_video = peer_video_sender_.CloseAllPeerVideo();
        peer_video_sender_.ReleaseClosedPeerVideo(
            &closed_video, SubscriptionClose::kStreamStopped);
        if (peer_host) {
            for (const std::string &peer_id : peer_ids) {
                (void)peer_host->ClosePeer(peer_id);
            }
            peer_host->Stop();
        }

        std::lock_guard<std::mutex> guard(mutex_);
        peer_table_.Clear();
        if (peer_host_ == peer_host) {
            peer_host_.reset();
        }
    }

    bool ApplyOptions(const WebrtcOptions &options) override {
        if (!IsValidOptions(options)) {
            return false;
        }

        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (options.session_timeout_ms != options_.session_timeout_ms ||
                options.send_queue_capacity != options_.send_queue_capacity ||
                options.send_workers != options_.send_workers ||
                options.local_port_base != options_.local_port_base) {
                return false;
            }
            peer_host = peer_host_;
        }

        if (peer_host && !peer_host->ApplyOptions(options)) {
            return false;
        }

        std::vector<std::string> peer_ids;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            options_ = options;
            if (!options_.enabled) {
                peer_ids = peer_table_.OpenPeerIds();
            }
        }
        for (const std::string &peer_id : peer_ids) {
            (void)ClosePeerByService(peer_id, "webrtc_disabled", false);
        }
        return true;
    }

    const char *Name() const { return Webrtc::Name(); }

    WebrtcPeerInfo CreatePeer(const WebrtcCreatePeerRequest &request) override {
        CloseStaleSetupPeers(TakeStalePeerIds());
        WebrtcPeerInfo peer;
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        std::vector<std::string> replaced_peer_ids;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (phase_ != WebrtcPhase::kStarted) {
                return CreatePeerError("service_not_started");
            }
            if (!options_.enabled) {
                return CreatePeerError("webrtc_disabled");
            }
            if (!peer_host_ || !peer_host_->Available()) {
                return CreatePeerError("peer_host_unavailable");
            }
            if (!IsValidStream(request.stream_id)) {
                return CreatePeerError("invalid_stream");
            }
            if (!IsStreamAvailableLocked(request.stream_id)) {
                return CreatePeerError("stream_unavailable");
            }

            const Codec codec =
                media_streams_->GetStreamCodec(request.stream_id);
            if (!IsWebrtcCodecSupported(codec)) {
                return CreatePeerError("unsupported_codec");
            }
            replaced_peer_ids = peer_table_.TakePeerIdsForClient(
                request.session_id, request.client_id);
            if (peer_table_.ActivePeers() >= options_.max_peers) {
                return CreatePeerError("peer_limit_reached");
            }
            peer = peer_table_.CreatePeer(request, codec);
            peer_host = peer_host_;
        }

        for (const std::string &peer_id : replaced_peer_ids) {
            peer_video_sender_.ClosePeerVideo(
                peer_id, SubscriptionClose::kUnsubscribed);
            (void)peer_host->ClosePeer(peer_id);
        }

        if (!peer_host->CreatePeer(peer)) {
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.RemovePeer(peer.peer_id);
            return CreatePeerError("peer_host_create_failed");
        }

        bool close_peer_host = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const WebrtcPeerInfo current_peer = peer_table_.GetPeer(peer.peer_id);
            if (phase_ != WebrtcPhase::kStarted ||
                current_peer.peer_id.empty() ||
                current_peer.state == WebrtcPeerState::kClosing ||
                current_peer.state == WebrtcPeerState::kClosed ||
                current_peer.state == WebrtcPeerState::kFailed) {
                close_peer_host = true;
            } else {
                ++stats_.total_peers;
            }
        }
        if (close_peer_host) {
            (void)peer_host->ClosePeer(peer.peer_id);
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.RemovePeer(peer.peer_id);
            return CreatePeerError("peer_create_interrupted");
        }
        RequestKeyframe(peer.stream_id, KeyframeRequestSource::kNewClient);
        return peer;
    }

    WebrtcAnswer HandleOffer(const WebrtcOfferRequest &request) override {
        WebrtcAnswer result;
        result.peer_id = request.peer_id;
        WebrtcPeerInfo peer;
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        std::vector<WebrtcIceCandidate> pending_candidates;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (phase_ != WebrtcPhase::kStarted || request.peer_id.empty() ||
                request.sdp.empty()) {
                result.state = WebrtcPeerState::kFailed;
                result.error = "invalid_offer";
                return result;
            }
            if (!peer_table_.BeginOffer(request.peer_id, peer)) {
                result.state = WebrtcPeerState::kFailed;
                result.error = "peer_not_found";
                return result;
            }
            peer_host = peer_host_;
        }

        const std::string answer = peer_host->HandleOffer(peer, request.sdp);

        if (answer.empty()) {
            (void)ClosePeerByService(request.peer_id, "sdp_not_ready",
                                     true);
            result.state = WebrtcPeerState::kFailed;
            result.error = "sdp_not_ready";
            return result;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_table_.CompleteOffer(request.peer_id, peer,
                                          pending_candidates)) {
                ++stats_.offers;
            }
        }
        if (peer.peer_id.empty()) {
            (void)peer_host->ClosePeer(request.peer_id);
            result.state = WebrtcPeerState::kFailed;
            result.error = "peer_not_found";
            return result;
        }
        for (const WebrtcIceCandidate &candidate : pending_candidates) {
            (void)peer_host->AddIceCandidate(candidate);
        }
        result.sdp = answer;
        result.state = peer.state;
        return result;
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        bool queued = false;
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (phase_ != WebrtcPhase::kStarted || candidate.peer_id.empty() ||
                candidate.candidate.empty() || candidate.sdp_mline_index < 0) {
                return false;
            }
            if (!peer_table_.AddOrQueueCandidate(candidate, queued)) {
                return false;
            }
            if (queued) {
                ++stats_.remote_candidates;
                return true;
            }
            peer_host = peer_host_;
        }

        if (!peer_host->AddIceCandidate(candidate)) {
            return false;
        }

        std::lock_guard<std::mutex> guard(mutex_);
        peer_table_.Touch(candidate.peer_id);
        ++stats_.remote_candidates;
        return true;
    }

    bool ClosePeer(const std::string &peer_id) override {
        return ClosePeerByService(peer_id, "http_delete", false);
    }

    WebrtcPeerInfo GetPeer(const std::string &peer_id) const override {
        return BuildPeerInfo(peer_id);
    }

    std::vector<WebrtcPeerInfo> GetPeers() const override {
        std::vector<WebrtcPeerInfo> peers;
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peers = peer_table_.OpenPeers();
            peer_host = peer_host_;
        }
        for (WebrtcPeerInfo &peer : peers) {
            if (peer_host) {
                (void)peer_host->FillPeerInfo(peer.peer_id, peer);
            }
            peer_video_sender_.FillPeerVideoInfo(&peer);
        }
        return peers;
    }

    WebrtcStats GetStats() const override {
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        WebrtcStats result;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            result = stats_;
            result.enabled = options_.enabled;
            result.active_peers = peer_table_.ActivePeers();
            result.local_port_base = options_.local_port_base;
            result.max_peers = options_.max_peers;
            result.ice_servers =
                static_cast<uint32_t>(options_.ice_servers.size());
            result.public_ip = options_.public_ip;
            peer_host = peer_host_;
        }
        result.signaling_ready = peer_host && peer_host->Available();
        if (peer_host) {
            peer_host->FillStats(result);
        }
        return result;
    }

private:
    static void OnPeerHostPeerStateChanged(void *user, const char *peer_id,
                                           WebrtcPeerState state,
                                           const char *last_error) {
        if (user == nullptr || peer_id == nullptr) {
            return;
        }
        WebrtcCallbackGuard *guard =
            static_cast<WebrtcCallbackGuard *>(user);
        WebrtcImpl *service = EnterWebrtcCallback(guard);
        if (service == nullptr) {
            return;
        }
        service->HandlePeerHostPeerStateChanged(
            peer_id, state, last_error == nullptr ? "" : last_error);
        LeaveWebrtcCallback(guard);
    }

    static void OnPeerHostKeyframeRequest(void *user, const char *peer_id) {
        if (user == nullptr || peer_id == nullptr) {
            return;
        }
        WebrtcCallbackGuard *guard =
            static_cast<WebrtcCallbackGuard *>(user);
        WebrtcImpl *service = EnterWebrtcCallback(guard);
        if (service == nullptr) {
            return;
        }
        service->HandlePeerKeyframeRequest(peer_id);
        LeaveWebrtcCallback(guard);
    }

    bool Prepare() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!IsValidOptions(options_)) {
            return false;
        }
        if (socket_io_ == nullptr || net_loop_ == nullptr ||
            media_streams_ == nullptr) {
            return false;
        }
        if (phase_ == WebrtcPhase::kInitialized ||
            phase_ == WebrtcPhase::kStarted) {
            return true;
        }
        std::unique_ptr<webrtc_internal::IWebrtcPeerHost> peer_host =
            webrtc_internal::CreateWebrtcPeerHost(socket_io_, net_loop_);
        webrtc_internal::WebrtcPeerHostCallbacks callbacks;
        callbacks.user = callback_guard_.get();
        callbacks.OnPeerStateChanged =
            &WebrtcImpl::OnPeerHostPeerStateChanged;
        callbacks.OnPeerKeyframeRequest =
            &WebrtcImpl::OnPeerHostKeyframeRequest;
        if (!peer_host || !peer_host->Start(options_, callbacks)) {
            return false;
        }
        peer_host_ = std::shared_ptr<webrtc_internal::IWebrtcPeerHost>(
            std::move(peer_host));
        phase_ = WebrtcPhase::kInitialized;
        return true;
    }

    void Release() {
        CloseCallbacks();
        WaitCallbacks();
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        std::vector<WebrtcPeerVideoSender::ClosedVideo> closed_video;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (phase_ == WebrtcPhase::kCreated) {
                return;
            }
            phase_ = WebrtcPhase::kDeinitialized;
            peer_table_.Clear();
            peer_host = std::move(peer_host_);
        }
        closed_video = peer_video_sender_.CloseAllPeerVideo();
        peer_video_sender_.ReleaseClosedPeerVideo(
            &closed_video, SubscriptionClose::kStreamStopped);
        if (peer_host) {
            peer_host->Stop();
        }
    }

    void CloseCallbacks() {
        CloseWebrtcCallbacks(callback_guard_.get());
    }

    void WaitCallbacks() {
        WaitWebrtcCallbacks(callback_guard_.get());
    }

    void HandlePeerHostPeerStateChanged(const std::string &peer_id,
                                        WebrtcPeerState state,
                                        const std::string &last_error) {
        webrtc_internal::PeerHostStateUpdate update;
        WebrtcPeerInfo peer_before;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peer_before = peer_table_.GetPeer(peer_id);
            update =
                peer_table_.ApplyPeerHostState(peer_id, state, last_error);
        }

        if (state == WebrtcPeerState::kConnected) {
            if (!peer_video_sender_.OpenPeerVideo(peer_id)) {
                ClosePeerByService(peer_id, "media_subscription_open_failed",
                                   true);
                return;
            }
        } else if (state == WebrtcPeerState::kClosing ||
                   state == WebrtcPeerState::kClosed ||
                   state == WebrtcPeerState::kFailed) {
            peer_video_sender_.ClosePeerVideo(
                peer_id, SubscriptionClose::kUnsubscribed);
        }

        if (update.need_keyframe) {
            RequestKeyframe(update.stream_id, KeyframeRequestSource::kRecovery);
        }
        peer_event_.Publish(peer_before, state, last_error);
    }

    void HandlePeerKeyframeRequest(const std::string &peer_id) {
        StreamId stream_id = StreamId::kMain;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!peer_table_.GetOpenPeerStream(peer_id, stream_id)) {
                return;
            }
        }
        RequestKeyframe(stream_id, KeyframeRequestSource::kPacketLoss);
    }

    std::vector<std::string> TakeStalePeerIds() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ != WebrtcPhase::kStarted) {
            return std::vector<std::string>();
        }
        return peer_table_.FindStaleSetupPeerIds(kPeerSetupTimeoutMs);
    }

    void CloseStaleSetupPeers(const std::vector<std::string> &peer_ids) {
        for (const std::string &peer_id : peer_ids) {
            (void)ClosePeerByService(peer_id, "setup_timeout", true);
        }
    }

    std::shared_ptr<webrtc_internal::IWebrtcPeerHost> PeerHostSnapshot() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return peer_host_;
    }

    bool ClosePeerByService(const std::string &peer_id,
                            const std::string &last_error,
                            bool failed) {
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_id.empty()) {
                return false;
            }
            peer = peer_table_.GetPeer(peer_id);
            if (peer.peer_id.empty()) {
                return false;
            }
            peer_host = peer_host_;
        }
        if (peer_host) {
            (void)peer_host->FillPeerInfo(peer_id, peer);
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.UpdatePeerInfo(peer);
            if (!peer_table_.MarkClosing(peer_id, last_error)) {
                return false;
            }
        }

        peer_video_sender_.ClosePeerVideo(
            peer_id, SubscriptionClose::kUnsubscribed);
        if (peer_host) {
            (void)peer_host->ClosePeer(peer_id);
        }

        const WebrtcPeerState final_state =
            failed ? WebrtcPeerState::kFailed : WebrtcPeerState::kClosed;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.ApplyPeerHostState(peer_id, final_state,
                                                 last_error);
        }
        peer_event_.Publish(peer, final_state, last_error);
        return true;
    }

    WebrtcPeerInfo BuildPeerInfo(const std::string &peer_id) const {
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peer = peer_table_.GetPeer(peer_id);
            peer_host = peer_host_;
        }
        if (!peer.peer_id.empty() && peer_host) {
            (void)peer_host->FillPeerInfo(peer_id, peer);
        }
        peer_video_sender_.FillPeerVideoInfo(&peer);
        return peer;
    }

    bool IsStreamAvailableLocked(StreamId stream_id) const {
        return media_streams_->IsStreamAvailable(stream_id);
    }

    void RequestKeyframe(StreamId stream_id, KeyframeRequestSource source) {
        (void)media_streams_->RequestKeyframe(stream_id, source);
    }

    WebrtcOptions options_;
    MediaStreams *media_streams_ = nullptr;
    ISocketIo *socket_io_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    event::EventCenter *event_ = nullptr;
    std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host_;
    std::shared_ptr<WebrtcCallbackGuard> callback_guard_;
    mutable std::mutex mutex_;
    webrtc_internal::WebrtcPeerTable peer_table_;
    WebrtcStats stats_{};
    WebrtcPeerVideoSender peer_video_sender_;
    WebrtcPeerEvent peer_event_;
    WebrtcPhase phase_ = WebrtcPhase::kCreated;
};

std::unique_ptr<IWebrtc>
CreateWebrtc(const WebrtcOptions &options,
             event::Loop *socket_loop) {
    return std::unique_ptr<IWebrtc>(
        new WebrtcImpl(options, socket_loop));
}

const char *Webrtc::Name() { return "webrtc"; }

}  // namespace live_stream

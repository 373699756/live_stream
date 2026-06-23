#include "webrtc.h"

#include "infra/log.h"
#include "net.h"
#include "webrtc_callback_guard.h"
#include "webrtc_engine.h"
#include "webrtc_peer_table.h"
#include "webrtc_rtp_sender.h"
#include "webrtc_sdp.h"

#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

constexpr int64_t kPeerSetupTimeoutMs = 10000;
constexpr uint32_t kWebrtcDrainIntervalMs = 10;
constexpr uint32_t kWebrtcMaxFramesPerDrain = 8;
constexpr uint32_t kWebrtcRtpMtuBytes = 1200;

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

bool IsValidOptions(const WebrtcOptions &options) {
    if (options.max_peers == 0 || options.session_timeout_ms == 0 ||
        options.send_queue_capacity == 0 || options.send_worker_count == 0 ||
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
               WebrtcDependencies dependencies)
        : options_(std::move(options)),
          media_streams_(dependencies.media_streams),
          net_engine_(dependencies.net_engine),
          net_loop_(dependencies.net_loop),
          event_(dependencies.event),
          callback_guard_(new WebrtcCallbackGuard()),
          rtp_sender_(kWebrtcRtpMtuBytes) {
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
        if (state_ == ServiceState::kStarted) {
            return true;
        }
        if (state_ == ServiceState::kStopped) {
            state_ = ServiceState::kInitialized;
        }
        if (state_ != ServiceState::kInitialized) {
            return false;
        }
        if (!options_.enabled) {
            state_ = ServiceState::kStarted;
            return true;
        }
        if (media_streams_ == nullptr) {
            return false;
        }
        state_ = ServiceState::kStarted;
        return true;
    }

    void Stop() override {
        std::vector<std::string> peer_ids;
        std::vector<ClosingSubscription> closing_subscriptions;
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::unique_lock<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted) {
                return;
            }
            state_ = ServiceState::kStopped;
            peer_ids = peer_table_.MarkAllClosing();
            MarkAllPeerSubscriptionsClosingLocked();
            subscription_condition_.wait(guard, [this]() {
                return NoPeerSubscriptionDrainingLocked();
            });
            closing_subscriptions = TakeAllPeerSubscriptionsLocked();
            engine = engine_;
        }

        ReleasePeerSubscriptions(&closing_subscriptions,
                                 SubscriptionClose::kStreamStopped);
        if (engine) {
            for (const std::string &peer_id : peer_ids) {
                (void)engine->ClosePeer(peer_id);
            }
        }

        std::lock_guard<std::mutex> guard(mutex_);
        peer_table_.Clear();
    }

    bool ApplyOptions(const WebrtcOptions &options) override {
        if (!IsValidOptions(options)) {
            return false;
        }

        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (options.session_timeout_ms != options_.session_timeout_ms ||
                options.send_queue_capacity != options_.send_queue_capacity ||
                options.send_worker_count != options_.send_worker_count ||
                options.local_port_base != options_.local_port_base) {
                return false;
            }
            engine = engine_;
        }

        if (engine && !engine->ApplyOptions(options)) {
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
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        std::vector<std::string> replaced_peer_ids;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted) {
                return CreatePeerError("service_not_started");
            }
            if (!options_.enabled) {
                return CreatePeerError("webrtc_disabled");
            }
            if (!engine_ || !engine_->Available()) {
                return CreatePeerError("engine_unavailable");
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
            if (peer_table_.ActivePeerCount() >= options_.max_peers) {
                return CreatePeerError("peer_limit_reached");
            }
            peer = peer_table_.CreatePeer(request, codec);
            engine = engine_;
        }

        for (const std::string &peer_id : replaced_peer_ids) {
            ClosePeerSubscription(peer_id, SubscriptionClose::kUnsubscribed);
            if (engine) {
                (void)engine->ClosePeer(peer_id);
            }
        }

        if (!engine || !engine->CreatePeer(peer)) {
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.RemovePeer(peer.peer_id);
            return CreatePeerError("engine_create_failed");
        }

        bool close_engine_peer = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const WebrtcPeerInfo current_peer = peer_table_.GetPeer(peer.peer_id);
            if (state_ != ServiceState::kStarted ||
                current_peer.peer_id.empty() ||
                current_peer.state == WebrtcPeerState::kClosing ||
                current_peer.state == WebrtcPeerState::kClosed ||
                current_peer.state == WebrtcPeerState::kFailed) {
                close_engine_peer = true;
            } else {
                ++stats_.total_peers;
            }
        }
        if (close_engine_peer) {
            (void)engine->ClosePeer(peer.peer_id);
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
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        std::vector<WebrtcIceCandidate> pending_candidates;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || request.peer_id.empty() ||
                request.sdp.empty()) {
                result.state = WebrtcPeerState::kFailed;
                result.error = "invalid_offer";
                return result;
            }
            if (!peer_table_.BeginOffer(request.peer_id, &peer)) {
                result.state = WebrtcPeerState::kFailed;
                result.error = "peer_not_found";
                return result;
            }
            engine = engine_;
        }

        if (!engine) {
            result.state = WebrtcPeerState::kFailed;
            result.error = "engine_unavailable";
            return result;
        }
        const std::string answer = engine->HandleOffer(peer, request.sdp);

        if (answer.empty()) {
            (void)ClosePeerByService(request.peer_id, "sdp_not_ready",
                                     true);
            result.state = WebrtcPeerState::kFailed;
            result.error = "sdp_not_ready";
            return result;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_table_.CompleteOffer(request.peer_id, &peer,
                                          &pending_candidates)) {
                ++stats_.offers;
            }
        }
        if (peer.peer_id.empty()) {
            (void)engine->ClosePeer(request.peer_id);
            result.state = WebrtcPeerState::kFailed;
            result.error = "peer_not_found";
            return result;
        }
        for (const WebrtcIceCandidate &candidate : pending_candidates) {
            (void)engine->AddIceCandidate(candidate);
        }
        result.sdp = answer;
        result.state = peer.state;
        return result;
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        bool queued = false;
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || candidate.peer_id.empty() ||
                candidate.candidate.empty() || candidate.sdp_mline_index < 0) {
                return false;
            }
            if (!peer_table_.AddOrQueueCandidate(candidate, &queued)) {
                return false;
            }
            if (queued) {
                ++stats_.remote_candidates;
                return true;
            }
            engine = engine_;
        }

        if (!engine || !engine->AddIceCandidate(candidate)) {
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
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peers = peer_table_.OpenPeers();
            engine = engine_;
        }
        for (WebrtcPeerInfo &peer : peers) {
            if (engine) {
                (void)engine->FillPeerInfo(peer.peer_id, &peer);
            }
            FillPeerSubscriptionInfo(&peer);
        }
        return peers;
    }

    WebrtcStats GetStats() const override {
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        WebrtcStats result;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            result = stats_;
            result.enabled = options_.enabled;
            result.active_peers = peer_table_.ActivePeerCount();
            result.local_port_base = options_.local_port_base;
            result.max_peers = options_.max_peers;
            result.ice_server_count =
                static_cast<uint32_t>(options_.ice_servers.size());
            result.public_ip = options_.public_ip;
            engine = engine_;
        }
        result.signaling_ready = engine && engine->Available();
        if (engine) {
            engine->FillStats(&result);
        }
        return result;
    }

private:
    static void DispatchPeerDrain(
        const std::shared_ptr<WebrtcCallbackGuard> &callback_guard,
        const std::string &peer_id) {
        WebrtcCallbackGuard *guard = callback_guard.get();
        WebrtcImpl *service = EnterWebrtcCallback(guard);
        if (service == nullptr) {
            return;
        }
        service->DrainPeerFrames(peer_id);
        LeaveWebrtcCallback(guard);
    }

    static void OnEnginePeerStateChanged(void *user, const char *peer_id,
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
        service->HandleEnginePeerStateChanged(
            peer_id, state, last_error == nullptr ? "" : last_error);
        LeaveWebrtcCallback(guard);
    }

    static void OnEngineKeyframeRequest(void *user, const char *peer_id) {
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
        if (state_ == ServiceState::kInitialized ||
            state_ == ServiceState::kStarted || state_ == ServiceState::kStopped) {
            return true;
        }
        std::unique_ptr<webrtc_internal::IWebrtcEngine> engine =
            webrtc_internal::CreateWebrtcEngine(net_engine_, net_loop_);
        webrtc_internal::WebrtcEngineCallbacks callbacks;
        callbacks.user = callback_guard_.get();
        callbacks.OnPeerStateChanged = &WebrtcImpl::OnEnginePeerStateChanged;
        callbacks.OnPeerKeyframeRequest =
            &WebrtcImpl::OnEngineKeyframeRequest;
        if (!engine || !engine->Start(options_, callbacks)) {
            return false;
        }
        engine_ = std::shared_ptr<webrtc_internal::IWebrtcEngine>(
            std::move(engine));
        state_ = ServiceState::kInitialized;
        return true;
    }

    void Release() {
        CloseServiceCallbacks();
        WaitServiceCallbacks();
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        std::vector<ClosingSubscription> closing_subscriptions;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ == ServiceState::kCreated) {
                return;
            }
            state_ = ServiceState::kDeinitialized;
            closing_subscriptions = TakeAllPeerSubscriptionsLocked();
            peer_table_.Clear();
            engine = std::move(engine_);
        }
        ReleasePeerSubscriptions(&closing_subscriptions,
                                 SubscriptionClose::kStreamStopped);
        if (engine) {
            engine->Stop();
        }
    }

    void CloseServiceCallbacks() {
        CloseWebrtcCallbacks(callback_guard_.get());
    }

    void WaitServiceCallbacks() {
        WaitWebrtcCallbacks(callback_guard_.get());
    }

    void HandleEnginePeerStateChanged(const std::string &peer_id,
                                      WebrtcPeerState state,
                                      const std::string &last_error) {
        webrtc_internal::EnginePeerStateUpdate update;
        WebrtcPeerInfo peer_before;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peer_before = peer_table_.GetPeer(peer_id);
            update =
                peer_table_.ApplyEngineState(peer_id, state, last_error);
        }

        if (state == WebrtcPeerState::kConnected) {
            if (!OpenPeerSubscription(peer_id)) {
                ClosePeerByService(peer_id, "media_subscription_open_failed",
                                   true);
                return;
            }
        } else if (state == WebrtcPeerState::kClosing ||
                   state == WebrtcPeerState::kClosed ||
                   state == WebrtcPeerState::kFailed) {
            ClosePeerSubscription(peer_id, SubscriptionClose::kUnsubscribed);
        }

        if (update.need_keyframe) {
            RequestKeyframe(update.stream_id, KeyframeRequestSource::kRecovery);
        }
        PublishPeerEvent(peer_before, state, last_error);
    }

    void HandlePeerKeyframeRequest(const std::string &peer_id) {
        StreamId stream_id = StreamId::kMain;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!peer_table_.GetOpenPeerStream(peer_id, &stream_id)) {
                return;
            }
        }
        RequestKeyframe(stream_id, KeyframeRequestSource::kPacketLoss);
    }

    std::vector<std::string> TakeStalePeerIds() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != ServiceState::kStarted) {
            return std::vector<std::string>();
        }
        return peer_table_.FindStaleSetupPeerIds(kPeerSetupTimeoutMs);
    }

    void CloseStaleSetupPeers(const std::vector<std::string> &peer_ids) {
        for (const std::string &peer_id : peer_ids) {
            (void)ClosePeerByService(peer_id, "setup_timeout", true);
        }
    }

    std::shared_ptr<webrtc_internal::IWebrtcEngine> EngineSnapshot() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return engine_;
    }

    bool ClosePeerByService(const std::string &peer_id,
                            const std::string &last_error,
                            bool failed) {
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
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
            engine = engine_;
        }
        if (engine) {
            (void)engine->FillPeerInfo(peer_id, &peer);
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.UpdatePeerInfo(peer);
            if (!peer_table_.MarkClosing(peer_id, last_error)) {
                return false;
            }
        }

        ClosePeerSubscription(peer_id, SubscriptionClose::kUnsubscribed);
        if (engine) {
            (void)engine->ClosePeer(peer_id);
        }

        const WebrtcPeerState final_state =
            failed ? WebrtcPeerState::kFailed : WebrtcPeerState::kClosed;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.ApplyEngineState(peer_id, final_state,
                                               last_error);
        }
        PublishPeerEvent(peer, final_state, last_error);
        return true;
    }

    WebrtcPeerInfo BuildPeerInfo(const std::string &peer_id) const {
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peer = peer_table_.GetPeer(peer_id);
            engine = engine_;
        }
        if (!peer.peer_id.empty() && engine) {
            (void)engine->FillPeerInfo(peer_id, &peer);
        }
        FillPeerSubscriptionInfo(&peer);
        return peer;
    }

    void FillPeerSubscriptionInfo(WebrtcPeerInfo *peer) const {
        if (peer == nullptr || peer->peer_id.empty() ||
            media_streams_ == nullptr) {
            return;
        }
        FrameSubscriptionId subscription_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const auto iter = peer_subscriptions_.find(peer->peer_id);
            if (iter != peer_subscriptions_.end()) {
                subscription_id = iter->second.subscription_id;
            }
        }
        peer->subscription_id = subscription_id;
        if (subscription_id == 0) {
            return;
        }
        const SubscriptionInfo subscription_info =
            media_streams_->GetSubscriptionInfo(subscription_id);
        peer->subscription_open = subscription_info.open;
        if (!subscription_info.open) {
            return;
        }
        peer->subscription_generation = subscription_info.generation;
        peer->subscription_pending_frames = subscription_info.pending_frames;
        peer->subscription_waiting_keyframe =
            subscription_info.waiting_for_keyframe;
        peer->subscription_slow = subscription_info.slow;
        peer->subscription_close_reason =
            SubscriptionCloseName(subscription_info.close_reason);
    }

    bool IsStreamAvailableLocked(StreamId stream_id) const {
        return media_streams_ != nullptr &&
               media_streams_->IsStreamAvailable(stream_id);
    }

    void RequestKeyframe(StreamId stream_id, KeyframeRequestSource source) {
        if (media_streams_ == nullptr) {
            return;
        }
        (void)media_streams_->RequestKeyframe(stream_id, source);
    }

    struct PeerSubscription {
        FrameSubscriptionId subscription_id = 0;
        uint64_t generation = 0;
        MediaStreamInfo track;
        std::vector<MediaFrame> start_frames;
        event::TimerId drain_timer_id = 0;
        bool draining = false;
        bool closing = false;
    };

    struct ClosingSubscription {
        FrameSubscriptionId subscription_id = 0;
        event::TimerId drain_timer_id = 0;
        std::vector<MediaFrame> start_frames;
    };

    bool OpenPeerSubscription(const std::string &peer_id) {
        if (media_streams_ == nullptr) {
            return false;
        }

        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peer = peer_table_.GetPeer(peer_id);
            if (state_ != ServiceState::kStarted || peer.peer_id.empty() ||
                peer.state != WebrtcPeerState::kConnected) {
                return false;
            }
            if (peer_subscriptions_.find(peer_id) != peer_subscriptions_.end()) {
                return true;
            }
        }

        SubscriptionOptions subscription_options;
        subscription_options.stream_id = peer.stream_id;
        subscription_options.keyframe_first = true;
        const FrameSubscriptionId subscription_id =
            media_streams_->SubscribeFrames(subscription_options);
        if (subscription_id == 0) {
            return false;
        }

        SubscriptionStart start_data =
            media_streams_->GetSubscriptionStart(subscription_id);
        if (!start_data.track_ready ||
            start_data.stream_info.codec != peer.codec) {
            (void)media_streams_->UnsubscribeFrames(
                subscription_id, SubscriptionClose::kUnsubscribed);
            return false;
        }

        PeerSubscription subscription;
        subscription.subscription_id = subscription_id;
        subscription.generation = start_data.generation;
        subscription.track = start_data.stream_info;
        subscription.start_frames.swap(start_data.gop_frames);

        bool subscription_opened = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const WebrtcPeerInfo current_peer = peer_table_.GetPeer(peer_id);
            if (state_ == ServiceState::kStarted &&
                current_peer.state == WebrtcPeerState::kConnected &&
                current_peer.stream_id == peer.stream_id &&
                current_peer.codec == peer.codec &&
                peer_subscriptions_.find(peer_id) == peer_subscriptions_.end()) {
                peer_subscriptions_[peer_id] = std::move(subscription);
                rtp_sender_.AddPeer(current_peer);
                subscription_opened = true;
            }
        }

        if (!subscription_opened) {
            (void)media_streams_->UnsubscribeFrames(
                subscription_id, SubscriptionClose::kUnsubscribed);
            return false;
        }

        ArmPeerDrainTimer(peer_id);
        DrainPeerFrames(peer_id);
        return true;
    }

    void ArmPeerDrainTimer(const std::string &peer_id) {
        if (net_loop_ == nullptr) {
            return;
        }
        std::shared_ptr<WebrtcCallbackGuard> callback_guard =
            callback_guard_;
        event::TimerId timer_id = 0;
        const event::EventStatus timer_status = net_loop_->RunEvery(
            kWebrtcDrainIntervalMs, [callback_guard, peer_id]() {
                WebrtcImpl::DispatchPeerDrain(callback_guard, peer_id);
            }, &timer_id);
        if (timer_status != event::EventStatus::kOk || timer_id == 0) {
            ClosePeerSubscription(peer_id, SubscriptionClose::kUnsubscribed);
            return;
        }

        bool keep_timer = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = peer_subscriptions_.find(peer_id);
            if (state_ == ServiceState::kStarted &&
                iter != peer_subscriptions_.end() &&
                !iter->second.closing &&
                iter->second.drain_timer_id == 0) {
                iter->second.drain_timer_id = timer_id;
                keep_timer = true;
            }
        }
        if (!keep_timer) {
            (void)net_loop_->CancelTimer(timer_id);
        }
    }

    void DrainPeerFrames(const std::string &peer_id) {
        if (!BeginPeerDrain(peer_id)) {
            return;
        }
        if (!FlushPeerStartFrames(peer_id)) {
            EndPeerDrain(peer_id);
            return;
        }

        FrameSubscriptionId subscription_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = peer_subscriptions_.find(peer_id);
            if (state_ == ServiceState::kStarted &&
                iter != peer_subscriptions_.end() &&
                !iter->second.closing) {
                subscription_id = iter->second.subscription_id;
            }
        }
        if (subscription_id == 0) {
            EndPeerDrain(peer_id);
            return;
        }

        for (uint32_t i = 0; i < kWebrtcMaxFramesPerDrain; ++i) {
            SubscriptionFrame subscription_frame;
            if (!media_streams_->PopSubscriptionFrame(subscription_id,
                                                      &subscription_frame)) {
                break;
            }
            SendPeerEncodedFrame(peer_id, subscription_frame.frame);
        }

        const SubscriptionInfo subscription_info =
            media_streams_->GetSubscriptionInfo(subscription_id);
        if (subscription_info.open && subscription_info.slow) {
            std::lock_guard<std::mutex> guard(mutex_);
            ++stats_.dropped_frames;
        }
        EndPeerDrain(peer_id);
    }

    bool BeginPeerDrain(const std::string &peer_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = peer_subscriptions_.find(peer_id);
        if (state_ != ServiceState::kStarted ||
            iter == peer_subscriptions_.end() || iter->second.draining ||
            iter->second.closing) {
            return false;
        }
        const WebrtcPeerInfo peer = peer_table_.GetPeer(peer_id);
        if (peer.state != WebrtcPeerState::kConnected) {
            return false;
        }
        iter->second.draining = true;
        return true;
    }

    void EndPeerDrain(const std::string &peer_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = peer_subscriptions_.find(peer_id);
        if (iter != peer_subscriptions_.end()) {
            iter->second.draining = false;
        }
        subscription_condition_.notify_all();
    }

    bool FlushPeerStartFrames(const std::string &peer_id) {
        while (true) {
            MediaFrame frame;
            bool has_frame = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                auto iter = peer_subscriptions_.find(peer_id);
                if (state_ != ServiceState::kStarted ||
                    iter == peer_subscriptions_.end() ||
                    iter->second.closing) {
                    return false;
                }
                if (!iter->second.start_frames.empty()) {
                    frame = std::move(iter->second.start_frames.front());
                    iter->second.start_frames.erase(
                        iter->second.start_frames.begin());
                    has_frame = true;
                }
            }
            if (!has_frame) {
                return true;
            }
            SendPeerEncodedFrame(peer_id, frame);
        }
    }

    void SendPeerEncodedFrame(const std::string &peer_id,
                              const MediaFrame &frame) {
        WebrtcPeerInfo peer;
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || engine_ == nullptr ||
                peer_subscriptions_.find(peer_id) == peer_subscriptions_.end()) {
                ++stats_.dropped_frames;
                return;
            }
            const auto subscription_iter = peer_subscriptions_.find(peer_id);
            if (subscription_iter == peer_subscriptions_.end() ||
                subscription_iter->second.closing) {
                ++stats_.dropped_frames;
                return;
            }
            peer = peer_table_.GetPeer(peer_id);
            if (peer.state != WebrtcPeerState::kConnected ||
                frame.stream_id != peer.stream_id || frame.codec != peer.codec) {
                ++stats_.dropped_frames;
                return;
            }
            engine = engine_;
        }

        webrtc_internal::WebrtcRtpSenderContext context;
        context.engine = engine;
        context.mutex = &mutex_;
        context.service_stats = &stats_;
        (void)rtp_sender_.SendFrame(peer, frame, context);
    }

    ClosingSubscription TakePeerSubscription(const std::string &peer_id) {
        ClosingSubscription closing_subscription;
        std::unique_lock<std::mutex> guard(mutex_);
        auto iter = peer_subscriptions_.find(peer_id);
        if (iter == peer_subscriptions_.end()) {
            return closing_subscription;
        }
        iter->second.closing = true;
        subscription_condition_.wait(guard, [this, &peer_id]() {
            auto subscription_iter = peer_subscriptions_.find(peer_id);
            return subscription_iter == peer_subscriptions_.end() ||
                   !subscription_iter->second.draining;
        });
        return TakePeerSubscriptionLocked(peer_id);
    }

    ClosingSubscription TakePeerSubscriptionLocked(const std::string &peer_id) {
        ClosingSubscription closing_subscription;
        auto iter = peer_subscriptions_.find(peer_id);
        if (iter == peer_subscriptions_.end()) {
            return closing_subscription;
        }
        closing_subscription.subscription_id = iter->second.subscription_id;
        closing_subscription.drain_timer_id = iter->second.drain_timer_id;
        closing_subscription.start_frames.swap(iter->second.start_frames);
        peer_subscriptions_.erase(iter);
        rtp_sender_.RemovePeer(peer_id);
        return closing_subscription;
    }

    void MarkAllPeerSubscriptionsClosingLocked() {
        for (auto &item : peer_subscriptions_) {
            item.second.closing = true;
        }
    }

    bool NoPeerSubscriptionDrainingLocked() const {
        for (const auto &item : peer_subscriptions_) {
            if (item.second.draining) {
                return false;
            }
        }
        return true;
    }

    std::vector<ClosingSubscription> TakeAllPeerSubscriptionsLocked() {
        std::vector<ClosingSubscription> closing_subscriptions;
        for (auto &item : peer_subscriptions_) {
            ClosingSubscription closing_subscription;
            closing_subscription.subscription_id =
                item.second.subscription_id;
            closing_subscription.drain_timer_id = item.second.drain_timer_id;
            closing_subscription.start_frames.swap(item.second.start_frames);
            closing_subscriptions.push_back(std::move(closing_subscription));
        }
        peer_subscriptions_.clear();
        rtp_sender_.Clear();
        return closing_subscriptions;
    }

    void ClosePeerSubscription(const std::string &peer_id,
                               SubscriptionClose reason) {
        ClosingSubscription closing_subscription =
            TakePeerSubscription(peer_id);
        ReleasePeerSubscription(&closing_subscription, reason);
    }

    void ReleasePeerSubscriptions(
        std::vector<ClosingSubscription> *closing_subscriptions,
        SubscriptionClose reason) {
        if (closing_subscriptions == nullptr) {
            return;
        }
        for (ClosingSubscription &closing_subscription :
             *closing_subscriptions) {
            ReleasePeerSubscription(&closing_subscription, reason);
        }
        closing_subscriptions->clear();
    }

    void ReleasePeerSubscription(ClosingSubscription *closing_subscription,
                                 SubscriptionClose reason) {
        if (closing_subscription == nullptr) {
            return;
        }
        if (net_loop_ != nullptr &&
            closing_subscription->drain_timer_id != 0) {
            (void)net_loop_->CancelTimer(
                closing_subscription->drain_timer_id);
        }
        if (media_streams_ != nullptr &&
            closing_subscription->subscription_id != 0) {
            (void)media_streams_->UnsubscribeFrames(
                closing_subscription->subscription_id, reason);
        }
        ClearEncodedFrames(&closing_subscription->start_frames);
        closing_subscription->subscription_id = 0;
        closing_subscription->drain_timer_id = 0;
    }

    void PublishPeerEvent(const WebrtcPeerInfo &peer,
                          WebrtcPeerState next_state,
                          const std::string &message) {
        if (event_ == nullptr || peer.peer_id.empty()) {
            return;
        }
        event::Event webrtc_event;
        webrtc_event.source = Webrtc::Name();
        webrtc_event.target = peer.peer_id;
        webrtc_event.message = message;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            webrtc_event.value =
                static_cast<int32_t>(peer_table_.ActivePeerCount());
        }
        if (next_state == WebrtcPeerState::kConnected &&
            peer.state != WebrtcPeerState::kConnected) {
            webrtc_event.type = event::EventType::kWebRtcClientConnected;
            static_cast<void>(event_->Publish(webrtc_event));
            return;
        }
        if ((next_state == WebrtcPeerState::kClosing ||
             next_state == WebrtcPeerState::kClosed ||
             next_state == WebrtcPeerState::kFailed) &&
            peer.state == WebrtcPeerState::kConnected) {
            webrtc_event.type = event::EventType::kWebRtcClientDisconnected;
            static_cast<void>(event_->Publish(webrtc_event));
        }
    }

    static void ClearEncodedFrames(std::vector<MediaFrame> *frames) {
        if (frames == nullptr) {
            return;
        }
        frames->clear();
    }

    WebrtcOptions options_;
    MediaStreams *media_streams_ = nullptr;
    INetEngine *net_engine_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    event::Dispatcher *event_ = nullptr;
    ServiceState state_ = ServiceState::kCreated;
    std::shared_ptr<webrtc_internal::IWebrtcEngine> engine_;
    std::shared_ptr<WebrtcCallbackGuard> callback_guard_;
    mutable std::mutex mutex_;
    std::condition_variable subscription_condition_;
    webrtc_internal::WebrtcPeerTable peer_table_;
    std::map<std::string, PeerSubscription> peer_subscriptions_;
    webrtc_internal::WebrtcRtpSender rtp_sender_;
    WebrtcStats stats_{};
};

std::unique_ptr<IWebrtc>
CreateWebrtc(const WebrtcOptions &options,
             const WebrtcDependencies &dependencies) {
    return std::unique_ptr<IWebrtc>(
        new WebrtcImpl(options, dependencies));
}

const char *Webrtc::Name() { return "webrtc"; }

}  // namespace live_stream

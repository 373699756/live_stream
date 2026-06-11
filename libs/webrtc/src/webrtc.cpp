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
constexpr uint32_t kWebrtcReaderDrainIntervalMs = 10;
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
          net_executor_(dependencies.net_executor),
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
        std::vector<PeerSubscriptionResources> subscription_resources;
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
            subscription_resources = TakeAllPeerSubscriptionsLocked();
            engine = engine_;
        }

        ReleasePeerSubscriptions(&subscription_resources,
                           FrameSubscriptionCloseReason::kStreamStopped);
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
            ClosePeerSubscription(peer_id, FrameSubscriptionCloseReason::kUnsubscribed);
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
        RequestKeyFrame(peer.stream_id, KeyFrameRequestType::kNewSubscriber);
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
        return BuildPeerDiagnostics(peer_id);
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
                (void)engine->FillPeerDiagnostics(peer.peer_id, &peer);
            }
            FillPeerSubscriptionDiagnostics(&peer);
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

    static void OnEngineKeyFrameRequested(void *user, const char *peer_id) {
        if (user == nullptr || peer_id == nullptr) {
            return;
        }
        WebrtcCallbackGuard *guard =
            static_cast<WebrtcCallbackGuard *>(user);
        WebrtcImpl *service = EnterWebrtcCallback(guard);
        if (service == nullptr) {
            return;
        }
        service->HandleEngineKeyFrameRequested(peer_id);
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
            webrtc_internal::CreateWebrtcEngine(net_engine_, net_executor_);
        webrtc_internal::WebrtcEngineCallbacks callbacks;
        callbacks.user = callback_guard_.get();
        callbacks.OnPeerStateChanged = &WebrtcImpl::OnEnginePeerStateChanged;
        callbacks.OnPeerKeyFrameRequested =
            &WebrtcImpl::OnEngineKeyFrameRequested;
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
        std::vector<PeerSubscriptionResources> subscription_resources;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ == ServiceState::kCreated) {
                return;
            }
            state_ = ServiceState::kDeinitialized;
            subscription_resources = TakeAllPeerSubscriptionsLocked();
            peer_table_.Clear();
            engine = std::move(engine_);
        }
        ReleasePeerSubscriptions(&subscription_resources,
                           FrameSubscriptionCloseReason::kStreamStopped);
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
        {
            std::lock_guard<std::mutex> guard(mutex_);
            update =
                peer_table_.ApplyEngineState(peer_id, state, last_error);
        }

        if (state == WebrtcPeerState::kConnected) {
            if (!AttachPeerSubscription(peer_id)) {
                ClosePeerByService(peer_id, "media_reader_attach_failed",
                                   true);
                return;
            }
        } else if (state == WebrtcPeerState::kClosing ||
                   state == WebrtcPeerState::kClosed ||
                   state == WebrtcPeerState::kFailed) {
            ClosePeerSubscription(peer_id, FrameSubscriptionCloseReason::kUnsubscribed);
        }

        if (update.request_key_frame) {
            RequestKeyFrame(update.stream_id, KeyFrameRequestType::kRecovery);
        }
    }

    void HandleEngineKeyFrameRequested(const std::string &peer_id) {
        StreamId stream_id = StreamId::kMain;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!peer_table_.GetOpenPeerStream(peer_id, &stream_id)) {
                return;
            }
        }
        RequestKeyFrame(stream_id, KeyFrameRequestType::kPacketLoss);
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
            (void)engine->FillPeerDiagnostics(peer_id, &peer);
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            (void)peer_table_.UpdateDiagnostics(peer);
            if (!peer_table_.MarkClosing(peer_id, last_error)) {
                return false;
            }
        }

        ClosePeerSubscription(peer_id, FrameSubscriptionCloseReason::kUnsubscribed);
        if (engine) {
            (void)engine->ClosePeer(peer_id);
        }

        std::lock_guard<std::mutex> guard(mutex_);
        const WebrtcPeerState final_state =
            failed ? WebrtcPeerState::kFailed : WebrtcPeerState::kClosed;
        (void)peer_table_.ApplyEngineState(peer_id, final_state, last_error);
        return true;
    }

    WebrtcPeerInfo BuildPeerDiagnostics(const std::string &peer_id) const {
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peer = peer_table_.GetPeer(peer_id);
            engine = engine_;
        }
        if (!peer.peer_id.empty() && engine) {
            (void)engine->FillPeerDiagnostics(peer_id, &peer);
        }
        FillPeerSubscriptionDiagnostics(&peer);
        return peer;
    }

    void FillPeerSubscriptionDiagnostics(WebrtcPeerInfo *peer) const {
        if (peer == nullptr || peer->peer_id.empty() ||
            media_streams_ == nullptr) {
            return;
        }
        FrameSubscriptionId reader_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const auto iter = peer_subscriptions_.find(peer->peer_id);
            if (iter != peer_subscriptions_.end()) {
                reader_id = iter->second.reader_id;
            }
        }
        peer->reader_id = reader_id;
        if (reader_id == 0) {
            return;
        }
        const FrameSubscriptionInfo reader_status =
            media_streams_->GetFrameSubscriptionInfo(reader_id);
        peer->reader_attached = reader_status.attached;
        if (!reader_status.attached) {
            return;
        }
        peer->reader_generation = reader_status.subscription_generation;
        peer->reader_pending_frames = reader_status.pending_frames;
        peer->reader_waiting_keyframe = reader_status.waiting_for_keyframe;
        peer->reader_slow = reader_status.slow_subscriber;
        peer->reader_close_reason =
            FrameSubscriptionCloseReasonName(reader_status.close_reason);
    }

    bool IsStreamAvailableLocked(StreamId stream_id) const {
        return media_streams_ != nullptr &&
               media_streams_->IsStreamAvailable(stream_id);
    }

    void RequestKeyFrame(StreamId stream_id, KeyFrameRequestType reason) {
        if (media_streams_ == nullptr) {
            return;
        }
        (void)media_streams_->RequestKeyFrame(stream_id, reason);
    }

    struct PeerSubscription {
        FrameSubscriptionId reader_id = 0;
        uint64_t reader_generation = 0;
        MediaStreamInfo track;
        std::vector<EncodedFrame> start_frames;
        NetTimerId drain_timer_id = 0;
        bool draining = false;
        bool closing = false;
    };

    struct PeerSubscriptionResources {
        FrameSubscriptionId reader_id = 0;
        NetTimerId drain_timer_id = 0;
        std::vector<EncodedFrame> start_frames;
    };

    bool AttachPeerSubscription(const std::string &peer_id) {
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

        FrameSubscriptionOptions subscription_options;
        subscription_options.stream_id = peer.stream_id;
        subscription_options.keyframe_first = true;
        subscription_options.subscriber_name = Webrtc::Name();
        const FrameSubscriptionId reader_id =
            media_streams_->SubscribeFrames(subscription_options);
        if (reader_id == 0) {
            return false;
        }

        FrameSubscriptionStartData start_data =
            media_streams_->GetFrameSubscriptionStartData(reader_id);
        if (!start_data.track_ready ||
            start_data.stream_info.codec != peer.codec) {
            (void)media_streams_->UnsubscribeFrames(
                reader_id, FrameSubscriptionCloseReason::kUnsubscribed);
            FrameSubscriptionStartDataUnref(&start_data);
            return false;
        }

        PeerSubscription reader;
        reader.reader_id = reader_id;
        reader.reader_generation = start_data.subscription_generation;
        reader.track = start_data.stream_info;
        reader.start_frames.swap(start_data.gop_frames);
        FrameSubscriptionStartDataUnref(&start_data);

        bool attached = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const WebrtcPeerInfo current_peer = peer_table_.GetPeer(peer_id);
            if (state_ == ServiceState::kStarted &&
                current_peer.state == WebrtcPeerState::kConnected &&
                current_peer.stream_id == peer.stream_id &&
                current_peer.codec == peer.codec &&
                peer_subscriptions_.find(peer_id) == peer_subscriptions_.end()) {
                peer_subscriptions_[peer_id] = std::move(reader);
                rtp_sender_.AddPeer(current_peer);
                attached = true;
            }
        }

        if (!attached) {
            ClearEncodedFrames(&reader.start_frames);
            (void)media_streams_->UnsubscribeFrames(
                reader_id, FrameSubscriptionCloseReason::kUnsubscribed);
            return false;
        }

        ArmPeerDrainTimer(peer_id);
        DrainPeerFrames(peer_id);
        return true;
    }

    void ArmPeerDrainTimer(const std::string &peer_id) {
        if (net_executor_ == nullptr) {
            return;
        }
        std::shared_ptr<WebrtcCallbackGuard> callback_guard =
            callback_guard_;
        const NetTimerId timer_id = net_executor_->RunEvery(
            kWebrtcReaderDrainIntervalMs, [callback_guard, peer_id]() {
                WebrtcImpl::DispatchPeerDrain(callback_guard, peer_id);
            });
        if (timer_id == 0) {
            ClosePeerSubscription(peer_id, FrameSubscriptionCloseReason::kUnsubscribed);
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
            (void)net_executor_->CancelTimer(timer_id);
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

        FrameSubscriptionId reader_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = peer_subscriptions_.find(peer_id);
            if (state_ == ServiceState::kStarted &&
                iter != peer_subscriptions_.end() &&
                !iter->second.closing) {
                reader_id = iter->second.reader_id;
            }
        }
        if (reader_id == 0) {
            EndPeerDrain(peer_id);
            return;
        }

        for (uint32_t i = 0; i < kWebrtcMaxFramesPerDrain; ++i) {
            SubscribedFrame reader_frame;
            if (!media_streams_->PopSubscribedFrame(reader_id,
                                                    &reader_frame)) {
                break;
            }
            SendPeerEncodedFrame(peer_id, reader_frame.frame);
            SubscribedFrameUnref(&reader_frame);
        }

        const FrameSubscriptionInfo status =
            media_streams_->GetFrameSubscriptionInfo(reader_id);
        if (status.attached && status.slow_subscriber) {
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
            EncodedFrame frame;
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
                    (void)EncodedFrameMove(
                        &frame, &iter->second.start_frames.front());
                    iter->second.start_frames.erase(
                        iter->second.start_frames.begin());
                    has_frame = true;
                }
            }
            if (!has_frame) {
                return true;
            }
            SendPeerEncodedFrame(peer_id, frame);
            EncodedFrameUnref(&frame);
        }
    }

    void SendPeerEncodedFrame(const std::string &peer_id,
                            const EncodedFrame &frame) {
        WebrtcPeerInfo peer;
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || engine_ == nullptr ||
                peer_subscriptions_.find(peer_id) == peer_subscriptions_.end()) {
                ++stats_.dropped_frames;
                return;
            }
            const auto reader_iter = peer_subscriptions_.find(peer_id);
            if (reader_iter == peer_subscriptions_.end() ||
                reader_iter->second.closing) {
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

    PeerSubscriptionResources TakePeerSubscription(const std::string &peer_id) {
        PeerSubscriptionResources resources;
        std::unique_lock<std::mutex> guard(mutex_);
        auto iter = peer_subscriptions_.find(peer_id);
        if (iter == peer_subscriptions_.end()) {
            return resources;
        }
        iter->second.closing = true;
        subscription_condition_.wait(guard, [this, &peer_id]() {
            auto reader_iter = peer_subscriptions_.find(peer_id);
            return reader_iter == peer_subscriptions_.end() ||
                   !reader_iter->second.draining;
        });
        return TakePeerSubscriptionLocked(peer_id);
    }

    PeerSubscriptionResources TakePeerSubscriptionLocked(const std::string &peer_id) {
        PeerSubscriptionResources resources;
        auto iter = peer_subscriptions_.find(peer_id);
        if (iter == peer_subscriptions_.end()) {
            return resources;
        }
        resources.reader_id = iter->second.reader_id;
        resources.drain_timer_id = iter->second.drain_timer_id;
        resources.start_frames.swap(iter->second.start_frames);
        peer_subscriptions_.erase(iter);
        rtp_sender_.RemovePeer(peer_id);
        return resources;
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

    std::vector<PeerSubscriptionResources> TakeAllPeerSubscriptionsLocked() {
        std::vector<PeerSubscriptionResources> readers;
        for (auto &item : peer_subscriptions_) {
            PeerSubscriptionResources resources;
            resources.reader_id = item.second.reader_id;
            resources.drain_timer_id = item.second.drain_timer_id;
            resources.start_frames.swap(item.second.start_frames);
            readers.push_back(std::move(resources));
        }
        peer_subscriptions_.clear();
        rtp_sender_.Clear();
        return readers;
    }

    void ClosePeerSubscription(const std::string &peer_id,
                         FrameSubscriptionCloseReason reason) {
        PeerSubscriptionResources resources = TakePeerSubscription(peer_id);
        ReleasePeerSubscription(&resources, reason);
    }

    void ReleasePeerSubscriptions(std::vector<PeerSubscriptionResources> *readers,
                            FrameSubscriptionCloseReason reason) {
        if (readers == nullptr) {
            return;
        }
        for (PeerSubscriptionResources &resources : *readers) {
            ReleasePeerSubscription(&resources, reason);
        }
        readers->clear();
    }

    void ReleasePeerSubscription(PeerSubscriptionResources *resources,
                           FrameSubscriptionCloseReason reason) {
        if (resources == nullptr) {
            return;
        }
        if (net_executor_ != nullptr &&
            resources->drain_timer_id != 0) {
            (void)net_executor_->CancelTimer(
                resources->drain_timer_id);
        }
        if (media_streams_ != nullptr &&
            resources->reader_id != 0) {
            (void)media_streams_->UnsubscribeFrames(
                resources->reader_id, reason);
        }
        ClearEncodedFrames(&resources->start_frames);
        resources->reader_id = 0;
        resources->drain_timer_id = 0;
    }

    static void ClearEncodedFrames(std::vector<EncodedFrame> *frames) {
        if (frames == nullptr) {
            return;
        }
        for (EncodedFrame &frame : *frames) {
            EncodedFrameUnref(&frame);
        }
        frames->clear();
    }

    WebrtcOptions options_;
    MediaStreams *media_streams_ = nullptr;
    INetEngine *net_engine_ = nullptr;
    INetExecutor *net_executor_ = nullptr;
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

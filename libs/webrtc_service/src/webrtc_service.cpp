#include "webrtc_service.h"

#include "infra/executor.h"
#include "infra/log.h"
#include "webrtc_engine.h"
#include "webrtc_frame_dispatcher.h"
#include "webrtc_peer_store.h"
#include "webrtc_sdp.h"
#include "webrtc_transport_net.h"

#include <mutex>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

constexpr int64_t kPeerSetupTimeoutMs = 10000;

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

bool IsValidOptions(const WebrtcServiceOptions &options) {
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

}  // namespace

class WebrtcServiceImpl : public IWebrtcService {
public:
    WebrtcServiceImpl(WebrtcServiceOptions options,
                      WebrtcServiceDependencies dependencies)
        : options_(std::move(options)), dependencies_(std::move(dependencies)) {}

    ~WebrtcServiceImpl() override { Release(); }

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
        if (dependencies_.stream_hub == nullptr) {
            return false;
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = options_.send_worker_count;
        executor_options.queue_capacity = options_.send_queue_capacity;
        if (!send_executor_->Start(executor_options)) {
            return false;
        }
        if (!SubscribeMediaLocked()) {
            send_executor_->Stop(infra::StopMode::kDiscard);
            return false;
        }
        if (transport_ && !transport_->Start("0.0.0.0", options_.local_port_base)) {
            UnsubscribeMediaLocked();
            send_executor_->Stop(infra::StopMode::kDiscard);
            return false;
        }
        frame_dispatcher_.Clear();
        send_task_posted_ = false;
        state_ = ServiceState::kStarted;
        return true;
    }

    void Stop() override {
        std::vector<std::string> peer_ids;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted) {
                return;
            }
            state_ = ServiceState::kStopped;
            UnsubscribeMediaLocked();
            if (transport_) {
                transport_->Stop();
            }
            frame_dispatcher_.Clear();
            send_task_posted_ = false;
            peer_ids = peer_store_.MarkAllClosing();
        }

        if (send_executor_) {
            send_executor_->Stop(infra::StopMode::kDiscard);
        }
        if (engine_) {
            for (const std::string &peer_id : peer_ids) {
                (void)engine_->ClosePeer(peer_id);
            }
        }

        std::lock_guard<std::mutex> guard(mutex_);
        peer_store_.Clear();
    }

    const char *Name() const override { return WebrtcService::Name(); }

    WebrtcPeerInfo CreatePeer(const WebrtcCreatePeerRequest &request) override {
        CloseEnginePeers(TakeStalePeerIds());
        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted) {
                return WebrtcPeerInfo();
            }
            if (!options_.enabled || !engine_ || !engine_->Available()) {
                return WebrtcPeerInfo();
            }
            if (!IsValidStream(request.stream_id) ||
                !IsStreamAvailableLocked(request.stream_id) ||
                peer_store_.ActivePeerCount() >= options_.max_peers) {
                return WebrtcPeerInfo();
            }

            const VideoCodec codec =
                dependencies_.stream_hub->GetStreamCodec(request.stream_id);
            peer = peer_store_.CreatePeer(request, codec);
        }

        if (!engine_ || !engine_->CreatePeer(peer)) {
            std::lock_guard<std::mutex> guard(mutex_);
            peer_store_.RemovePeer(peer.peer_id);
            return WebrtcPeerInfo();
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!peer_store_.Contains(peer.peer_id)) {
                return WebrtcPeerInfo();
            }
            ++stats_.total_peers;
        }
        RequestKeyFrame(peer.stream_id, KeyFrameReason::kNewClient);
        return peer;
    }

    WebrtcAnswer HandleOffer(const WebrtcOfferRequest &request) override {
        WebrtcPeerInfo peer;
        std::vector<WebrtcIceCandidate> pending_candidates;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || request.peer_id.empty() ||
                request.sdp.empty()) {
                return WebrtcAnswer();
            }
            if (!peer_store_.BeginOffer(request.peer_id, &peer)) {
                return WebrtcAnswer();
            }
        }

        if (!engine_) {
            return WebrtcAnswer();
        }
        const std::string answer = engine_->HandleOffer(peer, request.sdp);

        if (answer.empty()) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                peer_store_.RemovePeer(request.peer_id);
            }
            if (engine_ != nullptr) {
                (void)engine_->ClosePeer(request.peer_id);
            }
            return WebrtcAnswer();
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_store_.CompleteOffer(request.peer_id, &peer,
                                          &pending_candidates)) {
                ++stats_.offers;
            }
        }
        if (peer.peer_id.empty()) {
            if (engine_ != nullptr) {
                (void)engine_->ClosePeer(request.peer_id);
            }
            return WebrtcAnswer();
        }
        for (const WebrtcIceCandidate &candidate : pending_candidates) {
            if (engine_ != nullptr) {
                (void)engine_->AddIceCandidate(candidate);
            }
        }
        WebrtcAnswer result;
        result.peer_id = request.peer_id;
        result.sdp = answer;
        return result;
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        bool queued = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || candidate.peer_id.empty() ||
                candidate.candidate.empty() || candidate.sdp_mline_index < 0) {
                return false;
            }
            if (!peer_store_.AddOrQueueCandidate(candidate, &queued)) {
                return false;
            }
            if (queued) {
                ++stats_.remote_candidates;
                return true;
            }
        }

        if (!engine_ || !engine_->AddIceCandidate(candidate)) {
            return false;
        }

        std::lock_guard<std::mutex> guard(mutex_);
        peer_store_.Touch(candidate.peer_id);
        ++stats_.remote_candidates;
        return true;
    }

    bool ClosePeer(const std::string &peer_id) override {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_id.empty()) {
                return false;
            }
            if (!peer_store_.MarkClosing(peer_id)) {
                return false;
            }
        }

        if (engine_ != nullptr) {
            (void)engine_->ClosePeer(peer_id);
        }

        std::lock_guard<std::mutex> guard(mutex_);
        (void)peer_store_.RemovePeer(peer_id);
        return true;
    }

    WebrtcPeerInfo GetPeer(const std::string &peer_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        return peer_store_.GetPeer(peer_id);
    }

    WebrtcServiceStats GetStats() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        WebrtcServiceStats result = stats_;
        result.enabled = options_.enabled;
        result.backend_available = engine_ && engine_->Available();
        result.active_peers = peer_store_.ActivePeerCount();
        result.max_peers = options_.max_peers;
        return result;
    }

    const char *BackendName() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        return engine_ ? engine_->Name() : "none";
    }

    void OnFrame(const ParsedVideoFrame &frame) override {
        infra::Executor *executor = nullptr;
        bool post_send = false;
        const EncodedFrame &coded_frame = frame.coded_frame;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || !send_executor_) {
                ++stats_.dropped_frames;
                return;
            }
            if (!frame.has_nal_units || !coded_frame.buffer ||
                coded_frame.size == 0) {
                ++stats_.dropped_frames;
                return;
            }
            if (!peer_store_.HasConnectedPeer(coded_frame.stream_id)) {
                ++stats_.dropped_frames;
                return;
            }
            const webrtc_internal::WebrtcFrameQueueResult queue_result =
                frame_dispatcher_.Queue(frame);
            stats_.dropped_frames += queue_result.dropped_frames;
            if (!queue_result.queued) {
                return;
            }
            if (!send_task_posted_) {
                send_task_posted_ = true;
                executor = send_executor_.get();
                post_send = true;
            }
        }

        if (post_send && executor != nullptr &&
            !executor->Post([this]() { DrainPendingFrames(); })) {
            std::lock_guard<std::mutex> guard(mutex_);
            send_task_posted_ = false;
            ++stats_.dropped_frames;
        }
    }

    void OnSourceStateChanged(StreamId stream_id,
                              StreamState stream_state) override {
        (void)stream_id;
        (void)stream_state;
    }

private:
    static void OnEnginePeerStateChanged(void *user, const char *peer_id,
                                         WebrtcPeerState state) {
        if (user == nullptr || peer_id == nullptr) {
            return;
        }
        static_cast<WebrtcServiceImpl *>(user)->HandleEnginePeerStateChanged(
            peer_id, state);
    }

    static void OnEngineKeyFrameRequested(void *user, const char *peer_id) {
        if (user == nullptr || peer_id == nullptr) {
            return;
        }
        static_cast<WebrtcServiceImpl *>(user)->HandleEngineKeyFrameRequested(
            peer_id);
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
        engine_ = webrtc_internal::CreateEngine(dependencies_.use_fake_engine);
        webrtc_internal::WebrtcEngineCallbacks callbacks;
        callbacks.user = this;
        callbacks.OnPeerStateChanged = &WebrtcServiceImpl::OnEnginePeerStateChanged;
        callbacks.OnPeerKeyFrameRequested =
            &WebrtcServiceImpl::OnEngineKeyFrameRequested;
        if (!engine_->Start(options_, callbacks)) {
            engine_.reset();
            return false;
        }
        if (dependencies_.use_fake_engine && dependencies_.net_engine != nullptr) {
            transport_.reset(new webrtc_internal::NetWebrtcTransport(
                dependencies_.net_engine));
        }
        send_executor_.reset(new infra::Executor());
        state_ = ServiceState::kInitialized;
        return true;
    }

    void Release() {
        std::unique_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ == ServiceState::kCreated) {
                return;
            }
            state_ = ServiceState::kDeinitialized;
            UnsubscribeMediaLocked();
            if (transport_) {
                transport_->Stop();
                transport_.reset();
            }
            frame_dispatcher_.Clear();
            send_task_posted_ = false;
            peer_store_.Clear();
            engine = std::move(engine_);
        }
        if (send_executor_) {
            send_executor_->Stop(infra::StopMode::kDiscard);
            send_executor_.reset();
        }
        if (engine) {
            engine->Stop();
        }
    }

    void SendEncodedFrame(const ParsedVideoFrame &frame,
                          const std::vector<WebrtcPeerInfo> &peers) {
        webrtc_internal::IWebrtcEngine *engine = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || !engine_) {
                ++stats_.dropped_frames;
                return;
            }
            engine = engine_.get();
        }

        bool delivered = false;
        uint64_t sent_frames = 0;
        uint64_t dropped_frames = 0;
        for (const WebrtcPeerInfo &peer : peers) {
            if (engine->SendFrame(peer, frame)) {
                delivered = true;
                ++sent_frames;
            } else {
                ++dropped_frames;
            }
        }

        std::lock_guard<std::mutex> guard(mutex_);
        stats_.sent_frames += sent_frames;
        stats_.dropped_frames += dropped_frames;
        if (!delivered) {
            ++stats_.dropped_frames;
        }
    }

    void DrainPendingFrames() {
        while (true) {
            ParsedVideoFrame frame;
            std::vector<WebrtcPeerInfo> peers;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (state_ != ServiceState::kStarted ||
                    !frame_dispatcher_.TakeNext(&frame)) {
                    send_task_posted_ = false;
                    return;
                }
                peers = peer_store_.ConnectedPeers(frame.coded_frame.stream_id);
                if (peers.empty()) {
                    ++stats_.dropped_frames;
                    continue;
                }
            }
            SendEncodedFrame(frame, peers);
        }
    }

    void HandleEnginePeerStateChanged(const std::string &peer_id,
                                      WebrtcPeerState state) {
        webrtc_internal::EnginePeerStateUpdate update;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            update = peer_store_.ApplyEngineState(peer_id, state);
        }

        if (update.request_key_frame) {
            RequestKeyFrame(update.stream_id, KeyFrameReason::kRecovery);
        }
    }

    void HandleEngineKeyFrameRequested(const std::string &peer_id) {
        StreamId stream_id = StreamId::kMain;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!peer_store_.GetOpenPeerStream(peer_id, &stream_id)) {
                return;
            }
        }
        RequestKeyFrame(stream_id, KeyFrameReason::kPacketLoss);
    }

    std::vector<std::string> TakeStalePeerIds() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != ServiceState::kStarted) {
            return std::vector<std::string>();
        }
        return peer_store_.TakeStaleSetupPeerIds(kPeerSetupTimeoutMs);
    }

    void CloseEnginePeers(const std::vector<std::string> &peer_ids) {
        if (engine_ == nullptr) {
            return;
        }
        for (const std::string &peer_id : peer_ids) {
            (void)engine_->ClosePeer(peer_id);
        }
    }

    bool IsStreamAvailableLocked(StreamId stream_id) const {
        return dependencies_.stream_hub != nullptr &&
               dependencies_.stream_hub->IsStreamAvailable(stream_id);
    }

    bool SubscribeMediaLocked() {
        if (dependencies_.stream_hub == nullptr) {
            return false;
        }
        if (main_sink_id_ != 0 || sub_sink_id_ != 0) {
            return true;
        }

        FrameSubscribeOptions main_options;
        main_options.stream_id = StreamId::kMain;
        main_options.sink_name = WebrtcService::Name();
        main_sink_id_ =
            dependencies_.stream_hub->AttachFrameSink(main_options, this);

        FrameSubscribeOptions sub_options;
        sub_options.stream_id = StreamId::kSub;
        sub_options.sink_name = WebrtcService::Name();
        sub_sink_id_ =
            dependencies_.stream_hub->AttachFrameSink(sub_options, this);

        const bool subscribed =
            main_sink_id_ != 0 || sub_sink_id_ != 0;
        if (!subscribed) {
            UnsubscribeMediaLocked();
        }
        return subscribed;
    }

    void UnsubscribeMediaLocked() {
        if (dependencies_.stream_hub == nullptr) {
            main_sink_id_ = 0;
            sub_sink_id_ = 0;
            return;
        }
        if (main_sink_id_ != 0) {
            (void)dependencies_.stream_hub->DetachFrameSink(
                main_sink_id_);
            main_sink_id_ = 0;
        }
        if (sub_sink_id_ != 0) {
            (void)dependencies_.stream_hub->DetachFrameSink(
                sub_sink_id_);
            sub_sink_id_ = 0;
        }
    }

    void RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) {
        if (dependencies_.stream_hub == nullptr) {
            return;
        }
        (void)dependencies_.stream_hub->RequestKeyFrame(stream_id, reason);
    }

    WebrtcServiceOptions options_;
    WebrtcServiceDependencies dependencies_;
    ServiceState state_ = ServiceState::kCreated;
    std::unique_ptr<webrtc_internal::IWebrtcEngine> engine_;
    std::unique_ptr<webrtc_internal::NetWebrtcTransport> transport_;
    std::unique_ptr<infra::Executor> send_executor_;
    mutable std::mutex mutex_;
    webrtc_internal::WebrtcFrameDispatcher frame_dispatcher_;
    bool send_task_posted_ = false;
    webrtc_internal::WebrtcPeerStore peer_store_;
    WebrtcServiceStats stats_{};
    FrameSubscriptionId main_sink_id_ = 0;
    FrameSubscriptionId sub_sink_id_ = 0;
};

std::unique_ptr<IWebrtcService>
CreateWebrtcService(const WebrtcServiceOptions &options,
                    const WebrtcServiceDependencies &dependencies) {
    return std::unique_ptr<IWebrtcService>(
        new WebrtcServiceImpl(options, dependencies));
}

const char *WebrtcService::Name() { return "webrtc_service"; }

}  // namespace live_stream

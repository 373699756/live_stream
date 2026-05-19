#include "webrtc_service.h"

#include "infra/executor.h"
#include "infra/time.h"
#include "media_service.h"
#include "stream_codec.h"
#include "webrtc_engine.h"
#include "webrtc_sdp.h"
#include "webrtc_transport_net.h"

#include <map>
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

bool IsOpenPeerState(WebrtcPeerState state) {
    return state != WebrtcPeerState::kClosing &&
           state != WebrtcPeerState::kClosed && state != WebrtcPeerState::kFailed;
}

bool IsSetupPeerState(WebrtcPeerState state) {
    return state == WebrtcPeerState::kCreated ||
           state == WebrtcPeerState::kOfferReceived ||
           state == WebrtcPeerState::kConnecting;
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
        main_pending_frame_ = PendingFrameSlot{};
        sub_pending_frame_ = PendingFrameSlot{};
        send_task_posted_ = false;
        last_sent_stream_ = StreamId::kSub;
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
            main_pending_frame_ = PendingFrameSlot{};
            sub_pending_frame_ = PendingFrameSlot{};
            send_task_posted_ = false;
            last_sent_stream_ = StreamId::kSub;
            for (auto &item : peers_) {
                item.second.state = WebrtcPeerState::kClosing;
                peer_ids.push_back(item.first);
            }
            peer_activity_ms_.clear();
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
        peers_.clear();
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
                ActivePeerCountLocked() >= options_.max_peers) {
                return WebrtcPeerInfo();
            }

            peer.peer_id = NextPeerId();
            peer.stream_id = request.stream_id;
            if (dependencies_.media_service != nullptr) {
                peer.codec =
                    dependencies_.media_service->GetStreamCodec(request.stream_id);
            }
            peer.state = WebrtcPeerState::kCreated;
            peers_[peer.peer_id] = peer;
            peer_activity_ms_[peer.peer_id] = infra::Time::MonotonicMillis();
        }

        if (!engine_ || !engine_->CreatePeer(peer)) {
            std::lock_guard<std::mutex> guard(mutex_);
            peers_.erase(peer.peer_id);
            peer_activity_ms_.erase(peer.peer_id);
            return WebrtcPeerInfo();
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peers_.find(peer.peer_id) == peers_.end()) {
                return WebrtcPeerInfo();
            }
            ++stats_.total_peers;
        }
        RequestKeyFrame(peer.stream_id, KeyFrameReason::kNewClient);
        return peer;
    }

    WebrtcAnswer HandleOffer(const WebrtcOfferRequest &request) override {
        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || request.peer_id.empty() ||
                request.sdp.empty()) {
                return WebrtcAnswer();
            }
            auto it = peers_.find(request.peer_id);
            if (it == peers_.end() || !IsOpenPeerState(it->second.state)) {
                return WebrtcAnswer();
            }
            it->second.state = WebrtcPeerState::kOfferReceived;
            peer_activity_ms_[request.peer_id] = infra::Time::MonotonicMillis();
            peer = it->second;
        }

        if (!engine_) {
            return WebrtcAnswer();
        }
        const std::string answer = engine_->HandleOffer(peer, request.sdp);

        if (answer.empty()) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                peers_.erase(request.peer_id);
                peer_activity_ms_.erase(request.peer_id);
            }
            if (engine_ != nullptr) {
                (void)engine_->ClosePeer(request.peer_id);
            }
            return WebrtcAnswer();
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(request.peer_id);
            if (it == peers_.end()) {
                peer = WebrtcPeerInfo();
            } else {
                if (it->second.state != WebrtcPeerState::kConnected) {
                    it->second.state = WebrtcPeerState::kConnecting;
                }
                peer_activity_ms_[request.peer_id] =
                    infra::Time::MonotonicMillis();
                ++stats_.offers;
            }
        }
        if (peer.peer_id.empty()) {
            if (engine_ != nullptr) {
                (void)engine_->ClosePeer(request.peer_id);
            }
            return WebrtcAnswer();
        }
        WebrtcAnswer result;
        result.peer_id = request.peer_id;
        result.sdp = answer;
        return result;
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || candidate.peer_id.empty() ||
                candidate.candidate.empty() || candidate.sdp_mline_index < 0) {
                return false;
            }
            auto it = peers_.find(candidate.peer_id);
            if (it == peers_.end() || !IsOpenPeerState(it->second.state)) {
                return false;
            }
        }

        if (!engine_ || !engine_->AddIceCandidate(candidate)) {
            return false;
        }

        std::lock_guard<std::mutex> guard(mutex_);
        peer_activity_ms_[candidate.peer_id] = infra::Time::MonotonicMillis();
        ++stats_.remote_candidates;
        return true;
    }

    bool ClosePeer(const std::string &peer_id) override {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_id.empty()) {
                return false;
            }
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second.state = WebrtcPeerState::kClosing;
            } else {
                return false;
            }
        }

        if (engine_ != nullptr) {
            (void)engine_->ClosePeer(peer_id);
        }

        std::lock_guard<std::mutex> guard(mutex_);
        auto it = peers_.find(peer_id);
        if (it != peers_.end()) {
            it->second.state = WebrtcPeerState::kClosed;
            peers_.erase(it);
        }
        peer_activity_ms_.erase(peer_id);
        return true;
    }

    WebrtcPeerInfo GetPeer(const std::string &peer_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = peers_.find(peer_id);
        if (it == peers_.end()) {
            return WebrtcPeerInfo();
        }
        return it->second;
    }

    WebrtcServiceStats GetStats() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        WebrtcServiceStats result = stats_;
        result.enabled = options_.enabled;
        result.backend_available = engine_ && engine_->Available();
        result.active_peers = ActivePeerCountLocked();
        result.max_peers = options_.max_peers;
        return result;
    }

    const char *BackendName() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        return engine_ ? engine_->Name() : "none";
    }

    void OnFrame(const EncodedFrame &frame) override {
        infra::Executor *executor = nullptr;
        bool post_send = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || !send_executor_) {
                ++stats_.dropped_frames;
                return;
            }
            if (!frame.buffer || frame.size == 0) {
                ++stats_.dropped_frames;
                return;
            }
            if (!HasConnectedPeerLocked(frame.stream_id)) {
                ++stats_.dropped_frames;
                return;
            }
            QueuePendingFrameLocked(frame);
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
    struct PendingFrameSlot {
        EncodedFrame frame;
        bool ready = false;
    };

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
            main_pending_frame_ = PendingFrameSlot{};
            sub_pending_frame_ = PendingFrameSlot{};
            send_task_posted_ = false;
            last_sent_stream_ = StreamId::kSub;
            peers_.clear();
            peer_activity_ms_.clear();
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

    void SendEncodedFrame(const EncodedFrame &frame,
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
            EncodedFrame frame;
            std::vector<WebrtcPeerInfo> peers;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (state_ != ServiceState::kStarted || !TakePendingFrameLocked(&frame)) {
                    send_task_posted_ = false;
                    return;
                }
                CollectConnectedPeersLocked(frame.stream_id, &peers);
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
        StreamId stream_id = StreamId::kMain;
        bool request_key_frame = false;

        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end()) {
                return;
            }
            switch (state) {
                case WebrtcPeerState::kConnecting:
                    if (IsOpenPeerState(it->second.state) &&
                        it->second.state != WebrtcPeerState::kConnected) {
                        it->second.state = WebrtcPeerState::kConnecting;
                        peer_activity_ms_[peer_id] = infra::Time::MonotonicMillis();
                    }
                    return;
                case WebrtcPeerState::kConnected:
                    if (it->second.state != WebrtcPeerState::kConnected) {
                        it->second.state = WebrtcPeerState::kConnected;
                        stream_id = it->second.stream_id;
                        request_key_frame = true;
                    }
                    peer_activity_ms_[peer_id] = infra::Time::MonotonicMillis();
                    break;
                case WebrtcPeerState::kFailed:
                    it->second.state = WebrtcPeerState::kFailed;
                    peers_.erase(it);
                    peer_activity_ms_.erase(peer_id);
                    return;
                case WebrtcPeerState::kClosed:
                case WebrtcPeerState::kClosing:
                    it->second.state = WebrtcPeerState::kClosed;
                    peers_.erase(it);
                    peer_activity_ms_.erase(peer_id);
                    return;
                case WebrtcPeerState::kCreated:
                case WebrtcPeerState::kOfferReceived:
                    return;
            }
        }

        if (request_key_frame) {
            RequestKeyFrame(stream_id, KeyFrameReason::kRecovery);
        }
    }

    void HandleEngineKeyFrameRequested(const std::string &peer_id) {
        StreamId stream_id = StreamId::kMain;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end() || !IsOpenPeerState(it->second.state)) {
                return;
            }
            stream_id = it->second.stream_id;
        }
        RequestKeyFrame(stream_id, KeyFrameReason::kPacketLoss);
    }

    std::vector<std::string> TakeStalePeerIds() {
        std::vector<std::string> peer_ids;
        const int64_t now_ms = infra::Time::MonotonicMillis();
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != ServiceState::kStarted) {
            return peer_ids;
        }
        for (auto it = peers_.begin(); it != peers_.end();) {
            auto activity_it = peer_activity_ms_.find(it->first);
            if (activity_it == peer_activity_ms_.end()) {
                activity_it = peer_activity_ms_.insert(
                    std::make_pair(it->first, now_ms)).first;
            }
            if (IsSetupPeerState(it->second.state) &&
                now_ms - activity_it->second >= kPeerSetupTimeoutMs) {
                peer_ids.push_back(it->first);
                peer_activity_ms_.erase(it->first);
                it = peers_.erase(it);
            } else {
                ++it;
            }
        }
        return peer_ids;
    }

    void CloseEnginePeers(const std::vector<std::string> &peer_ids) {
        if (engine_ == nullptr) {
            return;
        }
        for (const std::string &peer_id : peer_ids) {
            (void)engine_->ClosePeer(peer_id);
        }
    }

    std::string NextPeerId() {
        std::string id = "webrtc-";
        id += std::to_string(next_peer_id_++);
        return id;
    }

    uint32_t ActivePeerCountLocked() const {
        uint32_t count = 0;
        for (const auto &item : peers_) {
            if (IsOpenPeerState(item.second.state)) {
                ++count;
            }
        }
        return count;
    }

    bool IsStreamAvailableLocked(StreamId stream_id) const {
        if (dependencies_.media_service == nullptr) {
            return true;
        }
        return dependencies_.media_service->IsStreamStarted(stream_id);
    }

    bool SubscribeMediaLocked() {
        if (dependencies_.media_service == nullptr) {
            return true;
        }
        if (main_subscription_id_ != 0 || sub_subscription_id_ != 0) {
            return true;
        }

        if (dependencies_.media_service->IsStreamStarted(StreamId::kMain)) {
            FrameSubscribeOptions main_options;
            main_options.stream_id = StreamId::kMain;
            main_options.sink_name = WebrtcService::Name();
            main_subscription_id_ =
                dependencies_.media_service->SubscribeFrames(main_options, this);
        }

        if (dependencies_.media_service->IsStreamStarted(StreamId::kSub)) {
            FrameSubscribeOptions sub_options;
            sub_options.stream_id = StreamId::kSub;
            sub_options.sink_name = WebrtcService::Name();
            sub_subscription_id_ =
                dependencies_.media_service->SubscribeFrames(sub_options, this);
        }

        const bool subscribed =
            main_subscription_id_ != 0 || sub_subscription_id_ != 0;
        if (!subscribed) {
            UnsubscribeMediaLocked();
        }
        return subscribed;
    }

    void UnsubscribeMediaLocked() {
        if (dependencies_.media_service == nullptr) {
            main_subscription_id_ = 0;
            sub_subscription_id_ = 0;
            return;
        }
        if (main_subscription_id_ != 0) {
            (void)dependencies_.media_service->UnsubscribeFrames(
                main_subscription_id_);
            main_subscription_id_ = 0;
        }
        if (sub_subscription_id_ != 0) {
            (void)dependencies_.media_service->UnsubscribeFrames(
                sub_subscription_id_);
            sub_subscription_id_ = 0;
        }
    }

    void RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) {
        if (dependencies_.media_service == nullptr) {
            return;
        }
        (void)dependencies_.media_service->RequestKeyFrame(stream_id, reason);
    }

    PendingFrameSlot *FindPendingSlotLocked(StreamId stream_id) {
        if (stream_id == StreamId::kMain) {
            return &main_pending_frame_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_pending_frame_;
        }
        return nullptr;
    }

    bool HasConnectedPeerLocked(StreamId stream_id) const {
        for (const auto &item : peers_) {
            if (item.second.stream_id == stream_id &&
                item.second.state == WebrtcPeerState::kConnected) {
                return true;
            }
        }
        return false;
    }

    void CollectConnectedPeersLocked(
        StreamId stream_id, std::vector<WebrtcPeerInfo> *peers) const {
        if (peers == nullptr) {
            return;
        }
        for (const auto &item : peers_) {
            if (item.second.stream_id == stream_id &&
                item.second.state == WebrtcPeerState::kConnected) {
                peers->push_back(item.second);
            }
        }
    }

    void QueuePendingFrameLocked(const EncodedFrame &frame) {
        PendingFrameSlot *slot = FindPendingSlotLocked(frame.stream_id);
        if (slot == nullptr) {
            ++stats_.dropped_frames;
            return;
        }
        if (slot->ready) {
            const bool existing_keyframe =
                stream_codec::IsKeyFrame(slot->frame.frame_type);
            const bool new_keyframe = stream_codec::IsKeyFrame(frame.frame_type);
            if (existing_keyframe && !new_keyframe) {
                ++stats_.dropped_frames;
                return;
            }
            ++stats_.dropped_frames;
        }
        slot->frame = frame;
        slot->ready = true;
    }

    bool TakePendingFrameLocked(EncodedFrame *frame) {
        if (frame == nullptr) {
            return false;
        }
        const StreamId preferred_stream = last_sent_stream_ == StreamId::kMain
                                              ? StreamId::kSub
                                              : StreamId::kMain;
        if (TakePendingFrameLocked(preferred_stream, frame)) {
            return true;
        }
        const StreamId fallback_stream = preferred_stream == StreamId::kMain
                                             ? StreamId::kSub
                                             : StreamId::kMain;
        return TakePendingFrameLocked(fallback_stream, frame);
    }

    bool TakePendingFrameLocked(StreamId stream_id, EncodedFrame *frame) {
        PendingFrameSlot *slot = FindPendingSlotLocked(stream_id);
        if (slot == nullptr || frame == nullptr || !slot->ready) {
            return false;
        }
        *frame = slot->frame;
        slot->ready = false;
        last_sent_stream_ = stream_id;
        return true;
    }

    WebrtcServiceOptions options_;
    WebrtcServiceDependencies dependencies_;
    ServiceState state_ = ServiceState::kCreated;
    std::unique_ptr<webrtc_internal::IWebrtcEngine> engine_;
    std::unique_ptr<webrtc_internal::NetWebrtcTransport> transport_;
    std::unique_ptr<infra::Executor> send_executor_;
    mutable std::mutex mutex_;
    PendingFrameSlot main_pending_frame_;
    PendingFrameSlot sub_pending_frame_;
    bool send_task_posted_ = false;
    StreamId last_sent_stream_ = StreamId::kSub;
    std::map<std::string, WebrtcPeerInfo> peers_;
    std::map<std::string, int64_t> peer_activity_ms_;
    WebrtcServiceStats stats_{};
    uint64_t next_peer_id_ = 1;
    FrameSubscriptionId main_subscription_id_ = 0;
    FrameSubscriptionId sub_subscription_id_ = 0;
};

std::unique_ptr<IWebrtcService>
CreateWebrtcService(const WebrtcServiceOptions &options,
                    const WebrtcServiceDependencies &dependencies) {
    return std::unique_ptr<IWebrtcService>(
        new WebrtcServiceImpl(options, dependencies));
}

const char *WebrtcService::Name() { return "webrtc_service"; }

}  // namespace live_stream

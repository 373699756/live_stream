#include "webrtc.h"

#include "infra/log.h"
#include "net.h"
#include "webrtc_engine.h"
#include "webrtc_peer_store.h"
#include "webrtc_rtp_sender.h"
#include "webrtc_sdp.h"

#include <condition_variable>
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

}  // namespace

class WebrtcServiceImpl;

struct WebrtcServiceCallbackGuard {
    std::mutex mutex;
    std::condition_variable condition;
    WebrtcServiceImpl *service = nullptr;
    uint32_t active_callbacks = 0;
    bool closing = false;
};

class WebrtcServiceImpl : public IWebrtc {
public:
    WebrtcServiceImpl(WebrtcOptions options,
                      WebrtcDependencies dependencies)
        : options_(std::move(options)),
          media_source_(dependencies.media_source),
          net_engine_(dependencies.net_engine),
          use_fake_engine_(dependencies.use_fake_engine),
          callback_guard_(new WebrtcServiceCallbackGuard()),
          rtp_sender_(kWebrtcRtpMtuBytes) {
        std::lock_guard<std::mutex> guard(callback_guard_->mutex);
        callback_guard_->service = this;
    }

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
        if (media_source_ == nullptr) {
            return false;
        }
        state_ = ServiceState::kStarted;
        return true;
    }

    void Stop() override {
        std::vector<std::string> peer_ids;
        std::vector<PeerReaderResources> reader_resources;
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::unique_lock<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted) {
                return;
            }
            state_ = ServiceState::kStopped;
            peer_ids = peer_store_.MarkAllClosing();
            MarkAllPeerReadersClosingLocked();
            reader_condition_.wait(guard, [this]() {
                return NoPeerReaderDrainingLocked();
            });
            reader_resources = TakeAllPeerReadersLocked();
            engine = engine_;
        }

        ReleasePeerReaders(&reader_resources,
                           MediaFrameReaderCloseReason::kDetached);
        if (engine) {
            for (const std::string &peer_id : peer_ids) {
                (void)engine->ClosePeer(peer_id);
            }
        }

        std::lock_guard<std::mutex> guard(mutex_);
        peer_store_.Clear();
    }

    const char *Name() const { return Webrtc::Name(); }

    WebrtcPeerInfo CreatePeer(const WebrtcCreatePeerRequest &request) override {
        CloseEnginePeers(TakeStalePeerIds());
        WebrtcPeerInfo peer;
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
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
                media_source_->GetStreamCodec(request.stream_id);
            peer = peer_store_.CreatePeer(request, codec);
            engine = engine_;
        }

        if (!engine || !engine->CreatePeer(peer)) {
            std::lock_guard<std::mutex> guard(mutex_);
            peer_store_.RemovePeer(peer.peer_id);
            return WebrtcPeerInfo();
        }

        bool close_engine_peer = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const WebrtcPeerInfo current_peer = peer_store_.GetPeer(peer.peer_id);
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
            return WebrtcPeerInfo();
        }
        RequestKeyFrame(peer.stream_id, KeyFrameReason::kNewClient);
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
            if (!peer_store_.BeginOffer(request.peer_id, &peer)) {
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
            {
                std::lock_guard<std::mutex> guard(mutex_);
                peer_store_.RemovePeer(request.peer_id);
            }
            (void)engine->ClosePeer(request.peer_id);
            result.state = WebrtcPeerState::kFailed;
            result.error = "sdp_not_ready";
            return result;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_store_.CompleteOffer(request.peer_id, &peer,
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
            if (!peer_store_.AddOrQueueCandidate(candidate, &queued)) {
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
        peer_store_.Touch(candidate.peer_id);
        ++stats_.remote_candidates;
        return true;
    }

    bool ClosePeer(const std::string &peer_id) override {
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer_id.empty()) {
                return false;
            }
            if (!peer_store_.MarkClosing(peer_id)) {
                return false;
            }
            engine = engine_;
        }

        ClosePeerReader(peer_id, MediaFrameReaderCloseReason::kDetached);
        if (engine) {
            (void)engine->ClosePeer(peer_id);
        }

        std::lock_guard<std::mutex> guard(mutex_);
        (void)peer_store_.RemovePeer(peer_id);
        return true;
    }

    WebrtcPeerInfo GetPeer(const std::string &peer_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        return peer_store_.GetPeer(peer_id);
    }

    WebrtcStats GetStats() const override {
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        WebrtcStats result;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            result = stats_;
            result.enabled = options_.enabled;
            result.active_peers = peer_store_.ActivePeerCount();
            result.max_peers = options_.max_peers;
            engine = engine_;
        }
        result.signaling_ready = engine && engine->Available();
        if (engine) {
            engine->FillStats(&result);
        }
        return result;
    }

private:
    static WebrtcServiceImpl *EnterServiceCallback(
        WebrtcServiceCallbackGuard *callback_guard) {
        if (callback_guard == nullptr) {
            return nullptr;
        }
        std::lock_guard<std::mutex> guard(callback_guard->mutex);
        if (callback_guard->closing || callback_guard->service == nullptr) {
            return nullptr;
        }
        ++callback_guard->active_callbacks;
        return callback_guard->service;
    }

    static void LeaveServiceCallback(
        WebrtcServiceCallbackGuard *callback_guard) {
        if (callback_guard == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> guard(callback_guard->mutex);
        if (callback_guard->active_callbacks == 0) {
            return;
        }
        --callback_guard->active_callbacks;
        if (callback_guard->active_callbacks == 0) {
            callback_guard->condition.notify_all();
        }
    }

    static void DispatchPeerDrain(
        const std::shared_ptr<WebrtcServiceCallbackGuard> &callback_guard,
        const std::string &peer_id) {
        WebrtcServiceCallbackGuard *guard = callback_guard.get();
        WebrtcServiceImpl *service = EnterServiceCallback(guard);
        if (service == nullptr) {
            return;
        }
        service->DrainPeerFrames(peer_id);
        LeaveServiceCallback(guard);
    }

    static void OnEnginePeerStateChanged(void *user, const char *peer_id,
                                         WebrtcPeerState state) {
        if (user == nullptr || peer_id == nullptr) {
            return;
        }
        WebrtcServiceCallbackGuard *guard =
            static_cast<WebrtcServiceCallbackGuard *>(user);
        WebrtcServiceImpl *service = EnterServiceCallback(guard);
        if (service == nullptr) {
            return;
        }
        service->HandleEnginePeerStateChanged(peer_id, state);
        LeaveServiceCallback(guard);
    }

    static void OnEngineKeyFrameRequested(void *user, const char *peer_id) {
        if (user == nullptr || peer_id == nullptr) {
            return;
        }
        WebrtcServiceCallbackGuard *guard =
            static_cast<WebrtcServiceCallbackGuard *>(user);
        WebrtcServiceImpl *service = EnterServiceCallback(guard);
        if (service == nullptr) {
            return;
        }
        service->HandleEngineKeyFrameRequested(peer_id);
        LeaveServiceCallback(guard);
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
            webrtc_internal::CreateEngine(use_fake_engine_, net_engine_);
        webrtc_internal::WebrtcEngineCallbacks callbacks;
        callbacks.user = callback_guard_.get();
        callbacks.OnPeerStateChanged = &WebrtcServiceImpl::OnEnginePeerStateChanged;
        callbacks.OnPeerKeyFrameRequested =
            &WebrtcServiceImpl::OnEngineKeyFrameRequested;
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
        std::vector<PeerReaderResources> reader_resources;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ == ServiceState::kCreated) {
                return;
            }
            state_ = ServiceState::kDeinitialized;
            reader_resources = TakeAllPeerReadersLocked();
            peer_store_.Clear();
            engine = std::move(engine_);
        }
        ReleasePeerReaders(&reader_resources,
                           MediaFrameReaderCloseReason::kDetached);
        if (engine) {
            engine->Stop();
        }
    }

    void CloseServiceCallbacks() {
        std::lock_guard<std::mutex> guard(callback_guard_->mutex);
        callback_guard_->closing = true;
        callback_guard_->service = nullptr;
    }

    void WaitServiceCallbacks() {
        std::unique_lock<std::mutex> guard(callback_guard_->mutex);
        callback_guard_->condition.wait(guard, [this]() {
            return callback_guard_->active_callbacks == 0;
        });
    }

    void HandleEnginePeerStateChanged(const std::string &peer_id,
                                      WebrtcPeerState state) {
        webrtc_internal::EnginePeerStateUpdate update;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            update = peer_store_.ApplyEngineState(peer_id, state);
        }

        if (state == WebrtcPeerState::kConnected) {
            if (!AttachPeerReader(peer_id)) {
                std::shared_ptr<webrtc_internal::IWebrtcEngine> engine =
                    EngineSnapshot();
                if (engine) {
                    (void)engine->ClosePeer(peer_id);
                }
                std::lock_guard<std::mutex> guard(mutex_);
                (void)peer_store_.RemovePeer(peer_id);
                return;
            }
        } else if (state == WebrtcPeerState::kClosing ||
                   state == WebrtcPeerState::kClosed ||
                   state == WebrtcPeerState::kFailed) {
            ClosePeerReader(peer_id, MediaFrameReaderCloseReason::kDetached);
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
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine =
            EngineSnapshot();
        if (!engine) {
            return;
        }
        for (const std::string &peer_id : peer_ids) {
            (void)engine->ClosePeer(peer_id);
        }
    }

    std::shared_ptr<webrtc_internal::IWebrtcEngine> EngineSnapshot() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return engine_;
    }

    bool IsStreamAvailableLocked(StreamId stream_id) const {
        return media_source_ != nullptr &&
               media_source_->IsStreamAvailable(stream_id);
    }

    void RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) {
        if (media_source_ == nullptr) {
            return;
        }
        (void)media_source_->RequestKeyFrame(stream_id, reason);
    }

    struct PeerReader {
        MediaFrameReaderId reader_id = 0;
        uint64_t reader_generation = 0;
        MediaTrack track;
        std::vector<MediaFrame> start_frames;
        NetTimerId drain_timer_id = 0;
        bool draining = false;
        bool closing = false;
    };

    struct PeerReaderResources {
        MediaFrameReaderId reader_id = 0;
        NetTimerId drain_timer_id = 0;
        std::vector<MediaFrame> start_frames;
    };

    bool AttachPeerReader(const std::string &peer_id) {
        if (media_source_ == nullptr) {
            return false;
        }

        WebrtcPeerInfo peer;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peer = peer_store_.GetPeer(peer_id);
            if (state_ != ServiceState::kStarted || peer.peer_id.empty() ||
                peer.state != WebrtcPeerState::kConnected) {
                return false;
            }
            if (peer_readers_.find(peer_id) != peer_readers_.end()) {
                return true;
            }
        }

        MediaFrameReaderOptions reader_options;
        reader_options.stream_id = peer.stream_id;
        reader_options.keyframe_first = true;
        reader_options.reader_name = Webrtc::Name();
        const MediaFrameReaderId reader_id =
            media_source_->AttachFrameReader(reader_options);
        if (reader_id == 0) {
            return false;
        }

        MediaFrameReaderStartData start_data =
            media_source_->GetFrameReaderStartData(reader_id);
        if (!start_data.stream_running || !start_data.track.ready ||
            start_data.track.codec != peer.codec) {
            (void)media_source_->DetachFrameReader(
                reader_id, MediaFrameReaderCloseReason::kDetached);
            MediaFrameReaderStartDataUnref(&start_data);
            return false;
        }

        PeerReader reader;
        reader.reader_id = reader_id;
        reader.reader_generation = start_data.reader_generation;
        reader.track = start_data.track;
        reader.start_frames.swap(start_data.gop_frames);
        MediaFrameReaderStartDataUnref(&start_data);

        bool attached = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const WebrtcPeerInfo current_peer = peer_store_.GetPeer(peer_id);
            if (state_ == ServiceState::kStarted &&
                current_peer.state == WebrtcPeerState::kConnected &&
                current_peer.stream_id == peer.stream_id &&
                current_peer.codec == peer.codec &&
                peer_readers_.find(peer_id) == peer_readers_.end()) {
                peer_readers_[peer_id] = std::move(reader);
                rtp_sender_.AddPeer(current_peer);
                attached = true;
            }
        }

        if (!attached) {
            ClearMediaFrames(&reader.start_frames);
            (void)media_source_->DetachFrameReader(
                reader_id, MediaFrameReaderCloseReason::kDetached);
            return false;
        }

        ArmPeerDrainTimer(peer_id);
        DrainPeerFrames(peer_id);
        return true;
    }

    void ArmPeerDrainTimer(const std::string &peer_id) {
        if (net_engine_ == nullptr) {
            return;
        }
        std::shared_ptr<WebrtcServiceCallbackGuard> callback_guard =
            callback_guard_;
        const NetTimerId timer_id = net_engine_->RunOnIoEvery(
            kWebrtcReaderDrainIntervalMs, [callback_guard, peer_id]() {
                WebrtcServiceImpl::DispatchPeerDrain(callback_guard, peer_id);
            });
        if (timer_id == 0) {
            ClosePeerReader(peer_id, MediaFrameReaderCloseReason::kDetached);
            return;
        }

        bool keep_timer = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = peer_readers_.find(peer_id);
            if (state_ == ServiceState::kStarted &&
                iter != peer_readers_.end() &&
                !iter->second.closing &&
                iter->second.drain_timer_id == 0) {
                iter->second.drain_timer_id = timer_id;
                keep_timer = true;
            }
        }
        if (!keep_timer) {
            (void)net_engine_->CancelIoTimer(timer_id);
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

        MediaFrameReaderId reader_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = peer_readers_.find(peer_id);
            if (state_ == ServiceState::kStarted &&
                iter != peer_readers_.end() &&
                !iter->second.closing) {
                reader_id = iter->second.reader_id;
            }
        }
        if (reader_id == 0) {
            EndPeerDrain(peer_id);
            return;
        }

        for (uint32_t i = 0; i < kWebrtcMaxFramesPerDrain; ++i) {
            MediaFrameReaderFrame reader_frame;
            if (!media_source_->PopFrameReaderFrame(reader_id,
                                                    &reader_frame)) {
                break;
            }
            SendPeerMediaFrame(peer_id, reader_frame.frame);
            MediaFrameReaderFrameUnref(&reader_frame);
        }

        const MediaFrameReaderStatus status =
            media_source_->GetFrameReaderStatus(reader_id);
        if (status.attached && status.slow_reader) {
            std::lock_guard<std::mutex> guard(mutex_);
            ++stats_.dropped_frames;
        }
        EndPeerDrain(peer_id);
    }

    bool BeginPeerDrain(const std::string &peer_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = peer_readers_.find(peer_id);
        if (state_ != ServiceState::kStarted ||
            iter == peer_readers_.end() || iter->second.draining ||
            iter->second.closing) {
            return false;
        }
        const WebrtcPeerInfo peer = peer_store_.GetPeer(peer_id);
        if (peer.state != WebrtcPeerState::kConnected) {
            return false;
        }
        iter->second.draining = true;
        return true;
    }

    void EndPeerDrain(const std::string &peer_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = peer_readers_.find(peer_id);
        if (iter != peer_readers_.end()) {
            iter->second.draining = false;
        }
        reader_condition_.notify_all();
    }

    bool FlushPeerStartFrames(const std::string &peer_id) {
        while (true) {
            MediaFrame frame;
            bool has_frame = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                auto iter = peer_readers_.find(peer_id);
                if (state_ != ServiceState::kStarted ||
                    iter == peer_readers_.end() ||
                    iter->second.closing) {
                    return false;
                }
                if (!iter->second.start_frames.empty()) {
                    (void)MediaFrameMove(
                        &frame, &iter->second.start_frames.front());
                    iter->second.start_frames.erase(
                        iter->second.start_frames.begin());
                    has_frame = true;
                }
            }
            if (!has_frame) {
                return true;
            }
            SendPeerMediaFrame(peer_id, frame);
            MediaFrameUnref(&frame);
        }
    }

    void SendPeerMediaFrame(const std::string &peer_id,
                            const MediaFrame &frame) {
        WebrtcPeerInfo peer;
        std::shared_ptr<webrtc_internal::IWebrtcEngine> engine;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_ != ServiceState::kStarted || engine_ == nullptr ||
                peer_readers_.find(peer_id) == peer_readers_.end()) {
                ++stats_.dropped_frames;
                return;
            }
            const auto reader_iter = peer_readers_.find(peer_id);
            if (reader_iter == peer_readers_.end() ||
                reader_iter->second.closing) {
                ++stats_.dropped_frames;
                return;
            }
            peer = peer_store_.GetPeer(peer_id);
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

    PeerReaderResources TakePeerReader(const std::string &peer_id) {
        PeerReaderResources resources;
        std::unique_lock<std::mutex> guard(mutex_);
        auto iter = peer_readers_.find(peer_id);
        if (iter == peer_readers_.end()) {
            return resources;
        }
        iter->second.closing = true;
        reader_condition_.wait(guard, [this, &peer_id]() {
            auto reader_iter = peer_readers_.find(peer_id);
            return reader_iter == peer_readers_.end() ||
                   !reader_iter->second.draining;
        });
        return TakePeerReaderLocked(peer_id);
    }

    PeerReaderResources TakePeerReaderLocked(const std::string &peer_id) {
        PeerReaderResources resources;
        auto iter = peer_readers_.find(peer_id);
        if (iter == peer_readers_.end()) {
            return resources;
        }
        resources.reader_id = iter->second.reader_id;
        resources.drain_timer_id = iter->second.drain_timer_id;
        resources.start_frames.swap(iter->second.start_frames);
        peer_readers_.erase(iter);
        rtp_sender_.RemovePeer(peer_id);
        return resources;
    }

    void MarkAllPeerReadersClosingLocked() {
        for (auto &item : peer_readers_) {
            item.second.closing = true;
        }
    }

    bool NoPeerReaderDrainingLocked() const {
        for (const auto &item : peer_readers_) {
            if (item.second.draining) {
                return false;
            }
        }
        return true;
    }

    std::vector<PeerReaderResources> TakeAllPeerReadersLocked() {
        std::vector<PeerReaderResources> readers;
        for (auto &item : peer_readers_) {
            PeerReaderResources resources;
            resources.reader_id = item.second.reader_id;
            resources.drain_timer_id = item.second.drain_timer_id;
            resources.start_frames.swap(item.second.start_frames);
            readers.push_back(std::move(resources));
        }
        peer_readers_.clear();
        rtp_sender_.Clear();
        return readers;
    }

    void ClosePeerReader(const std::string &peer_id,
                         MediaFrameReaderCloseReason reason) {
        PeerReaderResources resources = TakePeerReader(peer_id);
        ReleasePeerReader(&resources, reason);
    }

    void ReleasePeerReaders(std::vector<PeerReaderResources> *readers,
                            MediaFrameReaderCloseReason reason) {
        if (readers == nullptr) {
            return;
        }
        for (PeerReaderResources &resources : *readers) {
            ReleasePeerReader(&resources, reason);
        }
        readers->clear();
    }

    void ReleasePeerReader(PeerReaderResources *resources,
                           MediaFrameReaderCloseReason reason) {
        if (resources == nullptr) {
            return;
        }
        if (net_engine_ != nullptr &&
            resources->drain_timer_id != 0) {
            (void)net_engine_->CancelIoTimer(
                resources->drain_timer_id);
        }
        if (media_source_ != nullptr &&
            resources->reader_id != 0) {
            (void)media_source_->DetachFrameReader(
                resources->reader_id, reason);
        }
        ClearMediaFrames(&resources->start_frames);
        resources->reader_id = 0;
        resources->drain_timer_id = 0;
    }

    static void ClearMediaFrames(std::vector<MediaFrame> *frames) {
        if (frames == nullptr) {
            return;
        }
        for (MediaFrame &frame : *frames) {
            MediaFrameUnref(&frame);
        }
        frames->clear();
    }

    WebrtcOptions options_;
    IMediaFrameSource *media_source_ = nullptr;
    NetEngine *net_engine_ = nullptr;
    bool use_fake_engine_ = false;
    ServiceState state_ = ServiceState::kCreated;
    std::shared_ptr<webrtc_internal::IWebrtcEngine> engine_;
    std::shared_ptr<WebrtcServiceCallbackGuard> callback_guard_;
    mutable std::mutex mutex_;
    std::condition_variable reader_condition_;
    webrtc_internal::WebrtcPeerStore peer_store_;
    std::map<std::string, PeerReader> peer_readers_;
    webrtc_internal::WebrtcRtpSender rtp_sender_;
    WebrtcStats stats_{};
};

std::unique_ptr<IWebrtc>
CreateWebrtc(const WebrtcOptions &options,
                    const WebrtcDependencies &dependencies) {
    return std::unique_ptr<IWebrtc>(
        new WebrtcServiceImpl(options, dependencies));
}

const char *Webrtc::Name() { return "webrtc"; }

}  // namespace live_stream

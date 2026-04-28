#include "webrtc_service.h"

#include "infra/executor.h"
#include "infra/sync.h"
#include "media_service.h"
#include "webrtc_engine.h"
#include "webrtc_sdp.h"
#include "webrtc_transport_netframe.h"

#include <map>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

bool IsValidOptions(const WebrtcServiceOptions& options) {
    if (options.max_peers == 0 || options.session_timeout_ms == 0 ||
        options.send_queue_capacity == 0 || options.send_worker_count == 0 ||
        options.local_port_base == 0) {
        return false;
    }
    for (const auto& server : options.ice_servers) {
        if (!webrtc_internal::IsValidIceServerUrl(server.url)) {
            return false;
        }
    }
    return true;
}

bool IsValidStream(infra::StreamId stream_id) {
    return stream_id == infra::StreamId::kMain ||
           stream_id == infra::StreamId::kSub;
}

bool IsOpenPeerState(WebrtcPeerState state) {
    return state != WebrtcPeerState::kClosing &&
           state != WebrtcPeerState::kClosed &&
           state != WebrtcPeerState::kFailed;
}

}  // namespace

class WebrtcServiceImpl : public IWebrtcService {
 public:
    WebrtcServiceImpl(WebrtcServiceOptions options,
                      WebrtcServiceDependencies dependencies)
        : options_(std::move(options)),
          dependencies_(std::move(dependencies)) {}

    ~WebrtcServiceImpl() override { Deinit(); }

    infra::Status Init() override {
        infra::MutexGuard guard(&mutex_);
        if (!IsValidOptions(options_)) {
            return infra::Status::kInvalidParam;
        }
        if (state_ == ServiceState::kInitialized ||
            state_ == ServiceState::kStarted ||
            state_ == ServiceState::kStopped) {
            return infra::Status::kOk;
        }
        engine_ = webrtc_internal::CreateEngine(dependencies_.use_fake_engine);
        const infra::Status err = engine_->Init(options_);
        if (err != infra::Status::kOk) {
            engine_.reset();
            return err;
        }
        if (dependencies_.net_engine != nullptr) {
            transport_.reset(
                new webrtc_internal::NetframeWebrtcTransport(
                    dependencies_.net_engine));
        }
        send_executor_.reset(new infra::Executor());
        state_ = ServiceState::kInitialized;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        infra::MutexGuard guard(&mutex_);
        if (state_ == ServiceState::kStarted) {
            return infra::Status::kOk;
        }
        if (state_ == ServiceState::kStopped) {
            state_ = ServiceState::kInitialized;
        }
        if (state_ != ServiceState::kInitialized) {
            return infra::Status::kBusy;
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = options_.send_worker_count;
        executor_options.queue_capacity = options_.send_queue_capacity;
        const infra::Status executor_error =
            send_executor_->Start(executor_options);
        if (executor_error != infra::Status::kOk) {
            return executor_error;
        }
        const infra::Status media_error = SubscribeMediaLocked();
        if (media_error != infra::Status::kOk) {
            send_executor_->Stop(infra::StopMode::kDiscard);
            return media_error;
        }
        if (transport_) {
            const infra::Status transport_error =
                transport_->Start("0.0.0.0", options_.local_port_base);
            if (transport_error != infra::Status::kOk &&
                transport_error != infra::Status::kNotSupported) {
                UnsubscribeMediaLocked();
                send_executor_->Stop(infra::StopMode::kDiscard);
                return transport_error;
            }
        }
        state_ = ServiceState::kStarted;
        return infra::Status::kOk;
    }

    void Stop() override {
        bool should_stop = false;
        {
            infra::MutexGuard guard(&mutex_);
            if (state_ == ServiceState::kStarted) {
                state_ = ServiceState::kStopped;
                should_stop = true;
            }
        }
        if (!should_stop) {
            return;
        }
        {
            infra::MutexGuard guard(&mutex_);
            UnsubscribeMediaLocked();
            if (transport_) {
                transport_->Stop();
            }
        }
        if (send_executor_) {
            send_executor_->Stop(infra::StopMode::kDiscard);
        }
        infra::MutexGuard guard(&mutex_);
        CloseAllPeersLocked();
    }

    void Deinit() override {
        {
            infra::MutexGuard guard(&mutex_);
            if (state_ == ServiceState::kCreated) {
                return;
            }
            state_ = ServiceState::kDeinitialized;
        }
        if (send_executor_) {
            send_executor_->Stop(infra::StopMode::kDiscard);
        }
        infra::MutexGuard guard(&mutex_);
        UnsubscribeMediaLocked();
        if (transport_) {
            transport_->Stop();
            transport_.reset();
        }
        CloseAllPeersLocked();
        send_executor_.reset();
        if (engine_) {
            engine_->Deinit();
            engine_.reset();
        }
    }

    const char* Name() const override { return WebrtcService::Name(); }

    infra::Result<WebrtcPeerInfo> CreatePeer(
        const WebrtcCreatePeerRequest& request) override {
        infra::MutexGuard guard(&mutex_);
        if (state_ != ServiceState::kStarted) {
            return infra::Result<WebrtcPeerInfo>::Fail(infra::Status::kBusy);
        }
        if (!options_.enabled || !engine_ || !engine_->Available()) {
            return infra::Result<WebrtcPeerInfo>::Fail(infra::Status::kNotSupported);
        }
        if (!IsValidStream(request.stream_id)) {
            return infra::Result<WebrtcPeerInfo>::Fail(infra::Status::kInvalidParam);
        }
        if (ActivePeerCountLocked() >= options_.max_peers) {
            return infra::Result<WebrtcPeerInfo>::Fail(infra::Status::kNoMemory);
        }

        WebrtcPeerInfo peer;
        peer.peer_id = NextPeerId();
        peer.stream_id = request.stream_id;
        peer.state = WebrtcPeerState::kCreated;

        const infra::Status err = engine_->CreatePeer(peer);
        if (err != infra::Status::kOk) {
            return infra::Result<WebrtcPeerInfo>::Fail(err);
        }
        peers_[peer.peer_id] = peer;
        ++stats_.total_peers;
        RequestKeyFrameLocked(peer.stream_id);
        return infra::Result<WebrtcPeerInfo>::Ok(peer);
    }

    infra::Result<WebrtcAnswer> HandleOffer(
        const WebrtcOfferRequest& request) override {
        infra::MutexGuard guard(&mutex_);
        if (state_ != ServiceState::kStarted) {
            return infra::Result<WebrtcAnswer>::Fail(infra::Status::kBusy);
        }
        if (request.peer_id.empty() || request.sdp.empty()) {
            return infra::Result<WebrtcAnswer>::Fail(infra::Status::kInvalidParam);
        }
        auto it = peers_.find(request.peer_id);
        if (it == peers_.end() || !IsOpenPeerState(it->second.state)) {
            return infra::Result<WebrtcAnswer>::Fail(infra::Status::kNotFound);
        }
        it->second.state = WebrtcPeerState::kOfferReceived;
        const infra::Result<std::string> answer =
            engine_->HandleOffer(it->second, request.sdp);
        if (!answer.IsOk()) {
            it->second.state = WebrtcPeerState::kFailed;
            return infra::Result<WebrtcAnswer>::Fail(answer.status);
        }
        it->second.state = WebrtcPeerState::kConnected;
        ++stats_.offers;
        RequestKeyFrameLocked(it->second.stream_id);
        WebrtcAnswer result;
        result.peer_id = request.peer_id;
        result.sdp = answer.value;
        return infra::Result<WebrtcAnswer>::Ok(std::move(result));
    }

    infra::Status AddIceCandidate(
        const WebrtcIceCandidate& candidate) override {
        infra::MutexGuard guard(&mutex_);
        if (state_ != ServiceState::kStarted) {
            return infra::Status::kBusy;
        }
        if (candidate.peer_id.empty() || candidate.candidate.empty() ||
            candidate.sdp_mline_index < 0) {
            return infra::Status::kInvalidParam;
        }
        auto it = peers_.find(candidate.peer_id);
        if (it == peers_.end() || !IsOpenPeerState(it->second.state)) {
            return infra::Status::kNotFound;
        }
        WebrtcIceCandidate normalized = candidate;
        normalized.candidate =
            webrtc_internal::ReplaceHostCandidateIp(candidate.candidate,
                                                    options_.public_ip);
        const infra::Status err = engine_->AddIceCandidate(normalized);
        if (err == infra::Status::kOk) {
            ++stats_.remote_candidates;
        }
        return err;
    }

    infra::Status ClosePeer(const std::string& peer_id) override {
        infra::MutexGuard guard(&mutex_);
        if (peer_id.empty()) {
            return infra::Status::kInvalidParam;
        }
        auto it = peers_.find(peer_id);
        if (it == peers_.end()) {
            return infra::Status::kNotFound;
        }
        it->second.state = WebrtcPeerState::kClosing;
        if (engine_) {
            (void)engine_->ClosePeer(peer_id);
        }
        it->second.state = WebrtcPeerState::kClosed;
        peers_.erase(it);
        return infra::Status::kOk;
    }

    infra::Result<WebrtcPeerInfo> GetPeer(
        const std::string& peer_id) const override {
        infra::MutexGuard guard(&mutex_);
        auto it = peers_.find(peer_id);
        if (it == peers_.end()) {
            return infra::Result<WebrtcPeerInfo>::Fail(infra::Status::kNotFound);
        }
        return infra::Result<WebrtcPeerInfo>::Ok(it->second);
    }

    WebrtcServiceStats GetStats() const override {
        infra::MutexGuard guard(&mutex_);
        WebrtcServiceStats result = stats_;
        result.enabled = options_.enabled;
        result.backend_available = engine_ && engine_->Available();
        result.active_peers = ActivePeerCountLocked();
        result.max_peers = options_.max_peers;
        return result;
    }

    const char* BackendName() const override {
        infra::MutexGuard guard(&mutex_);
        return engine_ ? engine_->Name() : "none";
    }

    void OnFrame(const infra::EncodedFrame& frame) override {
        std::vector<WebrtcPeerInfo> peers;
        infra::Executor* executor = nullptr;
        infra::MutexGuard guard(&mutex_);
        if (state_ != ServiceState::kStarted || !send_executor_) {
            ++stats_.dropped_frames;
            return;
        }
        if (!frame.buffer || frame.size == 0) {
            ++stats_.dropped_frames;
            return;
        }
        for (const auto& item : peers_) {
            const WebrtcPeerInfo& peer = item.second;
            if (peer.stream_id == frame.stream_id &&
                peer.state == WebrtcPeerState::kConnected) {
                peers.push_back(peer);
            }
        }
        if (peers.empty()) {
            ++stats_.dropped_frames;
            return;
        }
        executor = send_executor_.get();

        const infra::Status post_error =
            executor->Post([this, frame, peers]() {
                SendEncodedFrame(frame, peers);
            });
        if (post_error != infra::Status::kOk) {
            ++stats_.dropped_frames;
        }
    }

    void OnSourceStateChanged(infra::StreamId stream_id,
                              StreamState stream_state) override {
        (void)stream_id;
        (void)stream_state;
    }

 private:
    void SendEncodedFrame(const infra::EncodedFrame& frame,
                          const std::vector<WebrtcPeerInfo>& peers) {
        infra::MutexGuard guard(&mutex_);
        bool delivered = false;
        if (state_ != ServiceState::kStarted || !engine_) {
            ++stats_.dropped_frames;
            return;
        }
        for (const WebrtcPeerInfo& peer : peers) {
            auto it = peers_.find(peer.peer_id);
            if (it == peers_.end() ||
                it->second.state != WebrtcPeerState::kConnected ||
                it->second.stream_id != frame.stream_id) {
                continue;
            }
            const infra::Status err = engine_->SendFrame(it->second, frame);
            if (err == infra::Status::kOk) {
                delivered = true;
                ++stats_.sent_frames;
            } else {
                ++stats_.dropped_frames;
            }
        }
        if (!delivered) {
            ++stats_.dropped_frames;
        }
    }

    std::string NextPeerId() {
        std::string id = "webrtc-";
        id += std::to_string(next_peer_id_++);
        return id;
    }

    uint32_t ActivePeerCountLocked() const {
        uint32_t count = 0;
        for (const auto& item : peers_) {
            if (IsOpenPeerState(item.second.state)) {
                ++count;
            }
        }
        return count;
    }

    infra::Status SubscribeMediaLocked() {
        if (dependencies_.media_service == nullptr) {
            return infra::Status::kOk;
        }
        if (main_subscription_id_ != 0 || sub_subscription_id_ != 0) {
            return infra::Status::kOk;
        }

        FrameSubscribeOptions main_options;
        main_options.stream_id = infra::StreamId::kMain;
        main_options.sink_name = WebrtcService::Name();
        auto main_result =
            dependencies_.media_service->SubscribeFrames(main_options, this);
        if (!main_result.IsOk()) {
            return main_result.status;
        }
        main_subscription_id_ = main_result.value;

        FrameSubscribeOptions sub_options;
        sub_options.stream_id = infra::StreamId::kSub;
        sub_options.sink_name = WebrtcService::Name();
        auto sub_result =
            dependencies_.media_service->SubscribeFrames(sub_options, this);
        if (!sub_result.IsOk()) {
            (void)dependencies_.media_service->UnsubscribeFrames(
                main_subscription_id_);
            main_subscription_id_ = 0;
            return sub_result.status;
        }
        sub_subscription_id_ = sub_result.value;
        return infra::Status::kOk;
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

    void RequestKeyFrameLocked(infra::StreamId stream_id) {
        if (dependencies_.media_service == nullptr) {
            return;
        }
        (void)dependencies_.media_service->RequestKeyFrame(
            stream_id, KeyFrameReason::kNewClient);
    }

    void CloseAllPeersLocked() {
        if (engine_) {
            for (auto& item : peers_) {
                item.second.state = WebrtcPeerState::kClosing;
                (void)engine_->ClosePeer(item.first);
                item.second.state = WebrtcPeerState::kClosed;
            }
        }
        peers_.clear();
    }

    WebrtcServiceOptions options_;
    WebrtcServiceDependencies dependencies_;
    ServiceState state_ = ServiceState::kCreated;
    std::unique_ptr<webrtc_internal::IWebrtcEngine> engine_;
    std::unique_ptr<webrtc_internal::NetframeWebrtcTransport> transport_;
    std::unique_ptr<infra::Executor> send_executor_;
    mutable infra::Mutex mutex_;
    std::map<std::string, WebrtcPeerInfo> peers_;
    WebrtcServiceStats stats_{};
    uint64_t next_peer_id_ = 1;
    FrameSubscriptionId main_subscription_id_ = 0;
    FrameSubscriptionId sub_subscription_id_ = 0;
};

std::unique_ptr<IWebrtcService> CreateWebrtcService(
    const WebrtcServiceOptions& options,
    const WebrtcServiceDependencies& dependencies) {
    return std::unique_ptr<IWebrtcService>(
        new WebrtcServiceImpl(options, dependencies));
}

const char* WebrtcService::Name() {
    return "webrtc_service";
}

}  // namespace live_stream

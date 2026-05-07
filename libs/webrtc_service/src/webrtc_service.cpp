#include "webrtc_service.h"

#include "infra/executor.h"
#include "media_service.h"
#include "webrtc_engine.h"
#include "webrtc_sdp.h"
#include "webrtc_transport_netframe.h"

#include <map>
#include <mutex>
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

} // namespace

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
    if (transport_) {
      if (!transport_->Start("0.0.0.0", options_.local_port_base)) {
        UnsubscribeMediaLocked();
        send_executor_->Stop(infra::StopMode::kDiscard);
        return false;
      }
    }
    state_ = ServiceState::kStarted;
    return true;
  }

  void Stop() override {
    bool should_stop = false;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (state_ == ServiceState::kStarted) {
        state_ = ServiceState::kStopped;
        should_stop = true;
      }
    }
    if (!should_stop) {
      return;
    }
    {
      std::lock_guard<std::mutex> guard(mutex_);
      UnsubscribeMediaLocked();
      if (transport_) {
        transport_->Stop();
      }
    }
    if (send_executor_) {
      send_executor_->Stop(infra::StopMode::kDiscard);
    }
    std::lock_guard<std::mutex> guard(mutex_);
    CloseAllPeersLocked();
  }

  const char *Name() const override { return WebrtcService::Name(); }

  WebrtcPeerInfo CreatePeer(const WebrtcCreatePeerRequest &request) override {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != ServiceState::kStarted) {
      return WebrtcPeerInfo();
    }
    if (!options_.enabled || !engine_ || !engine_->Available()) {
      return WebrtcPeerInfo();
    }
    if (!IsValidStream(request.stream_id)) {
      return WebrtcPeerInfo();
    }
    if (!IsStreamAvailableLocked(request.stream_id)) {
      return WebrtcPeerInfo();
    }
    if (ActivePeerCountLocked() >= options_.max_peers) {
      return WebrtcPeerInfo();
    }

    WebrtcPeerInfo peer;
    peer.peer_id = NextPeerId();
    peer.stream_id = request.stream_id;
    if (dependencies_.media_service != nullptr) {
      peer.codec =
          dependencies_.media_service->GetStreamCodec(request.stream_id);
    }
    peer.state = WebrtcPeerState::kCreated;

    if (!engine_->CreatePeer(peer)) {
      return WebrtcPeerInfo();
    }
    peers_[peer.peer_id] = peer;
    ++stats_.total_peers;
    RequestKeyFrameLocked(peer.stream_id);
    return peer;
  }

  WebrtcAnswer HandleOffer(const WebrtcOfferRequest &request) override {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != ServiceState::kStarted) {
      return WebrtcAnswer();
    }
    if (request.peer_id.empty() || request.sdp.empty()) {
      return WebrtcAnswer();
    }
    auto it = peers_.find(request.peer_id);
    if (it == peers_.end() || !IsOpenPeerState(it->second.state)) {
      return WebrtcAnswer();
    }
    it->second.state = WebrtcPeerState::kOfferReceived;
    const std::string answer = engine_->HandleOffer(it->second, request.sdp);
    if (answer.empty()) {
      it->second.state = WebrtcPeerState::kFailed;
      return WebrtcAnswer();
    }
    it->second.state = WebrtcPeerState::kConnected;
    ++stats_.offers;
    RequestKeyFrameLocked(it->second.stream_id);
    WebrtcAnswer result;
    result.peer_id = request.peer_id;
    result.sdp = answer;
    return result;
  }

  bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != ServiceState::kStarted) {
      return false;
    }
    if (candidate.peer_id.empty() || candidate.candidate.empty() ||
        candidate.sdp_mline_index < 0) {
      return false;
    }
    auto it = peers_.find(candidate.peer_id);
    if (it == peers_.end() || !IsOpenPeerState(it->second.state)) {
      return false;
    }
    if (!engine_->AddIceCandidate(candidate)) {
      return false;
    }
    { ++stats_.remote_candidates; }
    return true;
  }

  bool ClosePeer(const std::string &peer_id) override {
    std::lock_guard<std::mutex> guard(mutex_);
    if (peer_id.empty()) {
      return false;
    }
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
      return false;
    }
    it->second.state = WebrtcPeerState::kClosing;
    if (engine_) {
      (void)engine_->ClosePeer(peer_id);
    }
    it->second.state = WebrtcPeerState::kClosed;
    peers_.erase(it);
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
    std::vector<WebrtcPeerInfo> peers;
    infra::Executor *executor = nullptr;
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != ServiceState::kStarted || !send_executor_) {
      ++stats_.dropped_frames;
      return;
    }
    if (!frame.buffer || frame.size == 0) {
      ++stats_.dropped_frames;
      return;
    }
    for (const auto &item : peers_) {
      const WebrtcPeerInfo &peer = item.second;
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

    if (!executor->Post(
            [this, frame, peers]() { SendEncodedFrame(frame, peers); })) {
      ++stats_.dropped_frames;
    }
  }

  void OnSourceStateChanged(StreamId stream_id,
                            StreamState stream_state) override {
    (void)stream_id;
    (void)stream_state;
  }

private:
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
    if (!engine_->Start(options_)) {
      engine_.reset();
      return false;
    }
    if (dependencies_.use_fake_engine && dependencies_.net_engine != nullptr) {
      transport_.reset(new webrtc_internal::NetframeWebrtcTransport(
          dependencies_.net_engine));
    }
    send_executor_.reset(new infra::Executor());
    state_ = ServiceState::kInitialized;
    return true;
  }

  void Release() {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (state_ == ServiceState::kCreated) {
        return;
      }
      state_ = ServiceState::kDeinitialized;
    }
    if (send_executor_) {
      send_executor_->Stop(infra::StopMode::kDiscard);
    }
    std::lock_guard<std::mutex> guard(mutex_);
    UnsubscribeMediaLocked();
    if (transport_) {
      transport_->Stop();
      transport_.reset();
    }
    CloseAllPeersLocked();
    send_executor_.reset();
    if (engine_) {
      engine_->Stop();
      engine_.reset();
    }
  }

  void SendEncodedFrame(const EncodedFrame &frame,
                        const std::vector<WebrtcPeerInfo> &peers) {
    std::lock_guard<std::mutex> guard(mutex_);
    bool delivered = false;
    if (state_ != ServiceState::kStarted || !engine_) {
      ++stats_.dropped_frames;
      return;
    }
    for (const WebrtcPeerInfo &peer : peers) {
      auto it = peers_.find(peer.peer_id);
      if (it == peers_.end() ||
          it->second.state != WebrtcPeerState::kConnected ||
          it->second.stream_id != frame.stream_id) {
        continue;
      }
      if (engine_->SendFrame(it->second, frame)) {
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

    FrameSubscribeOptions main_options;
    main_options.stream_id = StreamId::kMain;
    main_options.sink_name = WebrtcService::Name();
    const FrameSubscriptionId main_result =
        dependencies_.media_service->SubscribeFrames(main_options, this);
    if (main_result == 0) {
      return false;
    }
    main_subscription_id_ = main_result;

    if (!dependencies_.media_service->IsStreamStarted(StreamId::kSub)) {
      return true;
    }
    FrameSubscribeOptions sub_options;
    sub_options.stream_id = StreamId::kSub;
    sub_options.sink_name = WebrtcService::Name();
    const FrameSubscriptionId sub_result =
        dependencies_.media_service->SubscribeFrames(sub_options, this);
    if (sub_result == 0) {
      (void)dependencies_.media_service->UnsubscribeFrames(
          main_subscription_id_);
      main_subscription_id_ = 0;
      return false;
    }
    sub_subscription_id_ = sub_result;
    return true;
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

  void RequestKeyFrameLocked(StreamId stream_id) {
    if (dependencies_.media_service == nullptr) {
      return;
    }
    (void)dependencies_.media_service->RequestKeyFrame(
        stream_id, KeyFrameReason::kNewClient);
  }

  void CloseAllPeersLocked() {
    if (engine_) {
      for (auto &item : peers_) {
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
  mutable std::mutex mutex_;
  std::map<std::string, WebrtcPeerInfo> peers_;
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

} // namespace live_stream

#include "webrtc_service.h"

#include "infra/executor.h"
#include "infra/sync.h"

#include <map>
#include <sstream>
#include <utility>

namespace live_stream {

namespace {

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

bool StartsWith(const std::string& text, const char* prefix) {
    const std::string expected(prefix);
    return text.compare(0, expected.size(), expected) == 0;
}

bool IsValidIceServerUrl(const std::string& url) {
    if (url.empty()) {
        return false;
    }
    if (StartsWith(url, "stun:") || StartsWith(url, "stun://") ||
        StartsWith(url, "turn:") || StartsWith(url, "turn://")) {
        return true;
    }
    return false;
}

bool IsValidOptions(const WebrtcServiceOptions& options) {
    if (options.max_peers == 0 || options.session_timeout_ms == 0 ||
        options.send_queue_capacity == 0 || options.local_port_base == 0) {
        return false;
    }
    for (const auto& server : options.ice_servers) {
        if (!IsValidIceServerUrl(server.url)) {
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

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
            case '"':
                escaped.push_back('\\');
                escaped.push_back(ch);
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string BuildCandidateJson(const WebrtcIceCandidate& candidate) {
    std::ostringstream json;
    json << "{\"candidate\":\"" << JsonEscape(candidate.candidate)
         << "\",\"sdpMid\":\"" << JsonEscape(candidate.sdp_mid)
         << "\",\"sdpMLineIndex\":" << candidate.sdp_mline_index << "}";
    return json.str();
}

std::string ReplaceHostCandidateIp(const std::string& candidate,
                                   const std::string& public_ip) {
    if (candidate.empty() || public_ip.empty() ||
        candidate.find(" typ relay") != std::string::npos) {
        return candidate;
    }

    size_t cursor = candidate.find("candidate:");
    if (cursor == std::string::npos) {
        return candidate;
    }
    int spaces = 0;
    while (cursor < candidate.size() && spaces < 4) {
        if (candidate[cursor] == ' ') {
            ++spaces;
        }
        ++cursor;
    }
    if (spaces < 4 || cursor >= candidate.size()) {
        return candidate;
    }

    const size_t ip_start = cursor;
    while (cursor < candidate.size() && candidate[cursor] != ' ') {
        ++cursor;
    }
    if (ip_start == cursor) {
        return candidate;
    }
    return candidate.substr(0, ip_start) + public_ip +
           candidate.substr(cursor);
}

class IWebrtcEngine {
 public:
    virtual ~IWebrtcEngine() = default;

    virtual const char* Name() const = 0;
    virtual bool Available() const = 0;
    virtual infra::Status Init(const WebrtcServiceOptions& options) = 0;
    virtual void Deinit() = 0;
    virtual infra::Status CreatePeer(const WebrtcPeerInfo& peer) = 0;
    virtual infra::Result<std::string> HandleOffer(
        const WebrtcPeerInfo& peer,
        const std::string& offer_sdp) = 0;
    virtual infra::Status AddIceCandidate(
        const WebrtcIceCandidate& candidate) = 0;
    virtual infra::Status ClosePeer(const std::string& peer_id) = 0;
    virtual infra::Status SendFrame(const WebrtcPeerInfo& peer,
                                   const infra::EncodedFrame& frame) = 0;
};

class MetaRtcEngine : public IWebrtcEngine {
 public:
    const char* Name() const override { return "metaRTC"; }
    bool Available() const override { return false; }

    infra::Status Init(const WebrtcServiceOptions& options) override {
        (void)options;
        return infra::Status::kOk;
    }

    void Deinit() override {}

    infra::Status CreatePeer(const WebrtcPeerInfo& peer) override {
        (void)peer;
        return infra::Status::kNotSupported;
    }

    infra::Result<std::string> HandleOffer(
        const WebrtcPeerInfo& peer,
        const std::string& offer_sdp) override {
        (void)peer;
        (void)offer_sdp;
        return infra::Result<std::string>::Fail(infra::Status::kNotSupported);
    }

    infra::Status AddIceCandidate(
        const WebrtcIceCandidate& candidate) override {
        const std::string candidate_json = BuildCandidateJson(candidate);
        (void)candidate_json;
        return infra::Status::kNotSupported;
    }

    infra::Status ClosePeer(const std::string& peer_id) override {
        (void)peer_id;
        return infra::Status::kOk;
    }

    infra::Status SendFrame(const WebrtcPeerInfo& peer,
                           const infra::EncodedFrame& frame) override {
        (void)peer;
        (void)frame;
        return infra::Status::kNotSupported;
    }
};

std::unique_ptr<IWebrtcEngine> CreateEngine() {
    return std::unique_ptr<IWebrtcEngine>(new MetaRtcEngine());
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
        engine_ = CreateEngine();
        const infra::Status err = engine_->Init(options_);
        if (err != infra::Status::kOk) {
            engine_.reset();
            return err;
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
        executor_options.worker_count = 1;
        executor_options.queue_capacity = options_.send_queue_capacity;
        const infra::Status executor_error =
            send_executor_->Start(executor_options);
        if (executor_error != infra::Status::kOk) {
            return executor_error;
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
            ReplaceHostCandidateIp(candidate.candidate, options_.public_ip);
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
        infra::MutexGuard guard(&mutex_);
        if (state_ != ServiceState::kStarted || !send_executor_) {
            ++stats_.dropped_frames;
            return;
        }
        if (!frame.buffer || frame.size == 0) {
            ++stats_.dropped_frames;
            return;
        }

        const infra::Status post_error =
            send_executor_->Post([this, frame]() { SendEncodedFrame(frame); });
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
    void SendEncodedFrame(const infra::EncodedFrame& frame) {
        infra::MutexGuard guard(&mutex_);
        bool delivered = false;
        for (auto& item : peers_) {
            const WebrtcPeerInfo& peer = item.second;
            if (peer.stream_id != frame.stream_id ||
                peer.state != WebrtcPeerState::kConnected) {
                continue;
            }
            const infra::Status err = engine_->SendFrame(peer, frame);
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
    std::unique_ptr<IWebrtcEngine> engine_;
    std::unique_ptr<infra::Executor> send_executor_;
    mutable infra::Mutex mutex_;
    std::map<std::string, WebrtcPeerInfo> peers_;
    WebrtcServiceStats stats_{};
    uint64_t next_peer_id_ = 1;
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

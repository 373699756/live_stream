#include "webrtc_peer_video_sender.h"

#include "webrtc_callback_guard.h"

#include <cstdint>
#include <utility>

namespace live_stream {
namespace {

constexpr uint32_t kWebrtcSendIntervalMs = 10;
constexpr uint32_t kWebrtcMaxFramesPerSend = 8;
constexpr uint32_t kWebrtcRtpMtuBytes = 1200;

}  // namespace

WebrtcPeerVideoSender::WebrtcPeerVideoSender(
    MediaStreams *media_streams,
    event::Loop *net_loop,
    std::shared_ptr<WebrtcCallbackGuard> callback_guard,
    std::mutex *mutex,
    webrtc_internal::WebrtcPeerTable *peer_table,
    std::shared_ptr<webrtc_internal::IWebrtcPeerHost> *peer_host,
    WebrtcStats *stats)
    : media_streams_(media_streams),
      net_loop_(net_loop),
      callback_guard_(std::move(callback_guard)),
      mutex_(mutex),
      peer_table_(peer_table),
      peer_host_(peer_host),
      stats_(stats),
      rtp_sender_(kWebrtcRtpMtuBytes) {}

bool WebrtcPeerVideoSender::OpenPeerVideo(const std::string &peer_id) {
    WebrtcPeerInfo peer;
    {
        std::lock_guard<std::mutex> guard(*mutex_);
        peer = peer_table_->GetPeer(peer_id);
        if (peer.peer_id.empty() ||
            peer.state != WebrtcPeerState::kConnected) {
            return false;
        }
        if (peer_video_.find(peer_id) != peer_video_.end()) {
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

    PeerVideo video;
    video.subscription_id = subscription_id;
    video.generation = start_data.generation;
    video.track = start_data.stream_info;
    for (MediaFrame &frame : start_data.gop_frames) {
        video.start_frames.push_back(std::move(frame));
    }

    bool video_opened = false;
    {
        std::lock_guard<std::mutex> guard(*mutex_);
        const WebrtcPeerInfo current_peer = peer_table_->GetPeer(peer_id);
        if (current_peer.state == WebrtcPeerState::kConnected &&
            current_peer.stream_id == peer.stream_id &&
            current_peer.codec == peer.codec &&
            peer_video_.find(peer_id) == peer_video_.end()) {
            peer_video_[peer_id] = std::move(video);
            rtp_sender_.AddPeer(current_peer);
            video_opened = true;
        }
    }

    if (!video_opened) {
        (void)media_streams_->UnsubscribeFrames(
            subscription_id, SubscriptionClose::kUnsubscribed);
        return false;
    }

    ArmPeerSendTimer(peer_id);
    SendPeerFrames(peer_id);
    return true;
}

void WebrtcPeerVideoSender::ClosePeerVideo(const std::string &peer_id,
                                           SubscriptionClose reason) {
    ClosedVideo closed_video = TakePeerVideo(peer_id);
    ReleaseClosedPeerVideo(&closed_video, reason);
}

std::vector<WebrtcPeerVideoSender::ClosedVideo>
WebrtcPeerVideoSender::CloseAllPeerVideo() {
    std::vector<ClosedVideo> closed_video;
    std::unique_lock<std::mutex> guard(*mutex_);
    for (auto &item : peer_video_) {
        item.second.closing = true;
    }
    video_condition_.wait(guard, [this]() {
        for (const auto &item : peer_video_) {
            if (item.second.sending) {
                return false;
            }
        }
        return true;
    });
    for (auto &item : peer_video_) {
        ClosedVideo closed;
        closed.subscription_id = item.second.subscription_id;
        closed.send_timer_id = item.second.send_timer_id;
        closed.start_frames.swap(item.second.start_frames);
        closed_video.push_back(std::move(closed));
    }
    peer_video_.clear();
    rtp_sender_.Clear();
    return closed_video;
}

void WebrtcPeerVideoSender::ReleaseClosedPeerVideo(
    std::vector<ClosedVideo> *closed_video,
    SubscriptionClose reason) {
    if (closed_video == nullptr) {
        return;
    }
    for (ClosedVideo &closed : *closed_video) {
        ReleaseClosedPeerVideo(&closed, reason);
    }
    closed_video->clear();
}

void WebrtcPeerVideoSender::FillPeerVideoInfo(WebrtcPeerInfo *peer) const {
    if (peer == nullptr || peer->peer_id.empty()) {
        return;
    }
    FrameSubscriptionId subscription_id = 0;
    {
        std::lock_guard<std::mutex> guard(*mutex_);
        const auto iter = peer_video_.find(peer->peer_id);
        if (iter != peer_video_.end()) {
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
    peer->subscription_waiting_keyframe = subscription_info.wait_keyframe;
    peer->subscription_slow = subscription_info.slow;
    peer->subscription_close_reason =
        SubscriptionCloseName(subscription_info.close_reason);
}

void WebrtcPeerVideoSender::DispatchPeerVideoSend(
    const std::shared_ptr<WebrtcCallbackGuard> &callback_guard,
    WebrtcPeerVideoSender *sender,
    const std::string &peer_id) {
    WebrtcCallbackGuard *guard = callback_guard.get();
    if (EnterWebrtcCallback(guard) == nullptr) {
        return;
    }
    if (sender != nullptr) {
        sender->SendPeerFrames(peer_id);
    }
    LeaveWebrtcCallback(guard);
}

void WebrtcPeerVideoSender::ArmPeerSendTimer(
    const std::string &peer_id) {
    std::shared_ptr<WebrtcCallbackGuard> callback_guard = callback_guard_;
    event::TimerId timer_id = 0;
    const event::EventStatus timer_status = net_loop_->RunEvery(
        kWebrtcSendIntervalMs, [callback_guard, this, peer_id]() {
            WebrtcPeerVideoSender::DispatchPeerVideoSend(
                callback_guard, this, peer_id);
        },
        &timer_id);
    if (timer_status != event::EventStatus::kOk || timer_id == 0) {
        ClosePeerVideo(peer_id, SubscriptionClose::kUnsubscribed);
        return;
    }

    bool keep_timer = false;
    {
        std::lock_guard<std::mutex> guard(*mutex_);
        auto iter = peer_video_.find(peer_id);
        if (iter != peer_video_.end() && !iter->second.closing &&
            iter->second.send_timer_id == 0) {
            iter->second.send_timer_id = timer_id;
            keep_timer = true;
        }
    }
    if (!keep_timer) {
        (void)net_loop_->CancelTimer(timer_id);
    }
}

void WebrtcPeerVideoSender::SendPeerFrames(const std::string &peer_id) {
    if (!BeginPeerSend(peer_id)) {
        return;
    }
    uint32_t sent_frames = 0;
    if (!SendPeerStartFrames(peer_id, &sent_frames)) {
        EndPeerSend(peer_id);
        return;
    }

    FrameSubscriptionId subscription_id = 0;
    {
        std::lock_guard<std::mutex> guard(*mutex_);
        auto iter = peer_video_.find(peer_id);
        if (iter != peer_video_.end() && !iter->second.closing) {
            subscription_id = iter->second.subscription_id;
        }
    }
    if (subscription_id == 0) {
        EndPeerSend(peer_id);
        return;
    }

    while (sent_frames < kWebrtcMaxFramesPerSend) {
        SubscriptionFrame subscription_frame;
        if (!media_streams_->PullFrame(subscription_id,
                                       &subscription_frame)) {
            break;
        }
        SendPeerFrame(peer_id, subscription_frame.frame);
        ++sent_frames;
    }

    const SubscriptionInfo subscription_info =
        media_streams_->GetSubscriptionInfo(subscription_id);
    if (subscription_info.open && subscription_info.slow) {
        std::lock_guard<std::mutex> guard(*mutex_);
        ++stats_->dropped_frames;
    }
    EndPeerSend(peer_id);
}

bool WebrtcPeerVideoSender::BeginPeerSend(const std::string &peer_id) {
    std::lock_guard<std::mutex> guard(*mutex_);
    auto iter = peer_video_.find(peer_id);
    if (iter == peer_video_.end() || iter->second.sending ||
        iter->second.closing) {
        return false;
    }
    const WebrtcPeerInfo peer = peer_table_->GetPeer(peer_id);
    if (peer.state != WebrtcPeerState::kConnected) {
        return false;
    }
    iter->second.sending = true;
    return true;
}

void WebrtcPeerVideoSender::EndPeerSend(const std::string &peer_id) {
    std::lock_guard<std::mutex> guard(*mutex_);
    auto iter = peer_video_.find(peer_id);
    if (iter != peer_video_.end()) {
        iter->second.sending = false;
    }
    video_condition_.notify_all();
}

bool WebrtcPeerVideoSender::SendPeerStartFrames(
    const std::string &peer_id,
    uint32_t *sent_frames) {
    if (sent_frames == nullptr) {
        return false;
    }
    while (*sent_frames < kWebrtcMaxFramesPerSend) {
        MediaFrame frame;
        bool has_frame = false;
        {
            std::lock_guard<std::mutex> guard(*mutex_);
            auto iter = peer_video_.find(peer_id);
            if (iter == peer_video_.end() || iter->second.closing) {
                return false;
            }
            if (!iter->second.start_frames.empty()) {
                frame = std::move(iter->second.start_frames.front());
                iter->second.start_frames.pop_front();
                has_frame = true;
            }
        }
        if (!has_frame) {
            return true;
        }
        SendPeerFrame(peer_id, frame);
        ++(*sent_frames);
    }
    std::lock_guard<std::mutex> guard(*mutex_);
    auto iter = peer_video_.find(peer_id);
    return iter != peer_video_.end() && !iter->second.closing &&
           iter->second.start_frames.empty();
}

void WebrtcPeerVideoSender::SendPeerFrame(const std::string &peer_id,
                                          const MediaFrame &frame) {
    WebrtcPeerInfo peer;
    std::shared_ptr<webrtc_internal::IWebrtcPeerHost> peer_host;
    {
        std::lock_guard<std::mutex> guard(*mutex_);
        const auto video_iter = peer_video_.find(peer_id);
        if (peer_host_ == nullptr || peer_host_->get() == nullptr ||
            video_iter == peer_video_.end() || video_iter->second.closing) {
            ++stats_->dropped_frames;
            return;
        }
        peer = peer_table_->GetPeer(peer_id);
        if (peer.state != WebrtcPeerState::kConnected ||
            frame.stream_id != peer.stream_id || frame.codec != peer.codec) {
            ++stats_->dropped_frames;
            return;
        }
        peer_host = *peer_host_;
    }

    webrtc_internal::WebrtcRtpSenderContext context{
        *peer_host, *mutex_, *stats_};
    (void)rtp_sender_.SendFrame(peer, frame, context);
}

WebrtcPeerVideoSender::ClosedVideo
WebrtcPeerVideoSender::TakePeerVideo(const std::string &peer_id) {
    ClosedVideo closed_video;
    std::unique_lock<std::mutex> guard(*mutex_);
    auto iter = peer_video_.find(peer_id);
    if (iter == peer_video_.end()) {
        return closed_video;
    }
    iter->second.closing = true;
    video_condition_.wait(guard, [this, &peer_id]() {
        auto video_iter = peer_video_.find(peer_id);
        return video_iter == peer_video_.end() ||
               !video_iter->second.sending;
    });
    return TakePeerVideoLocked(peer_id);
}

WebrtcPeerVideoSender::ClosedVideo
WebrtcPeerVideoSender::TakePeerVideoLocked(const std::string &peer_id) {
    ClosedVideo closed_video;
    auto iter = peer_video_.find(peer_id);
    if (iter == peer_video_.end()) {
        return closed_video;
    }
    closed_video.subscription_id = iter->second.subscription_id;
    closed_video.send_timer_id = iter->second.send_timer_id;
    closed_video.start_frames.swap(iter->second.start_frames);
    peer_video_.erase(iter);
    rtp_sender_.RemovePeer(peer_id);
    return closed_video;
}

void WebrtcPeerVideoSender::ReleaseClosedPeerVideo(
    ClosedVideo *closed_video,
    SubscriptionClose reason) {
    if (closed_video == nullptr) {
        return;
    }
    if (closed_video->send_timer_id != 0) {
        (void)net_loop_->CancelTimer(closed_video->send_timer_id);
    }
    if (closed_video->subscription_id != 0) {
        (void)media_streams_->UnsubscribeFrames(
            closed_video->subscription_id, reason);
    }
    ClearMediaFrames(&closed_video->start_frames);
    closed_video->subscription_id = 0;
    closed_video->send_timer_id = 0;
}

void WebrtcPeerVideoSender::ClearMediaFrames(
    std::deque<MediaFrame> *frames) {
    if (frames != nullptr) {
        frames->clear();
    }
}

}  // namespace live_stream

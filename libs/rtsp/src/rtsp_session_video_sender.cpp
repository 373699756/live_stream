#include "rtsp_session_video_sender.h"

#include "event.h"
#include "media/media_streams.h"
#include "rtp.h"
#include "rtsp_muxer.h"
#include "socket_io.h"

#include <cstdint>
#include <utility>

namespace live_stream {
namespace {

constexpr uint32_t kRtspSendIntervalMs = 10;
constexpr uint32_t kRtspMaxFramesPerSend = 8;

uint32_t FirstStartFrameRtpTimestamp(
    const SubscriptionStart &start_data) {
    if (start_data.gop_frames.empty()) {
        return 0;
    }
    return rtp::RtpTimestampFromPtsUs(
        start_data.gop_frames.front().pts_us);
}

}  // namespace

RtspSessionVideoSender::RtspSessionVideoSender(MediaStreams *media_streams,
                                               event::Loop *net_loop,
                                               ISocketIo *socket_io,
                                               std::mutex *mutex,
                                               RtspStats *stats,
                                               uint32_t rtp_mtu_bytes)
    : media_streams_(media_streams),
      net_loop_(net_loop),
      socket_io_(socket_io),
      mutex_(mutex),
      stats_(stats),
      rtp_sender_(rtp_mtu_bytes) {}

int RtspSessionVideoSender::StartMediaStream(RtspSession &session) {
    if (media_streams_ == nullptr ||
        !media_streams_->IsStreamAvailable(session.stream_id)) {
        return 404;
    }
    CloseSubscription(session, SubscriptionClose::kUnsubscribed);
    // PLAY 才创建长期 subscription，keyframe_first 让媒体链路优先给关键帧，
    // 并把当前 GOP 作为 start frames 返回给本 session。
    SubscriptionOptions subscription_options;
    subscription_options.stream_id = session.stream_id;
    subscription_options.keyframe_first = true;
    const FrameSubscriptionId subscription_id =
        media_streams_->SubscribeFrames(subscription_options);
    if (subscription_id == 0) {
        return 455;
    }
    SubscriptionStart start_data =
        media_streams_->GetSubscriptionStart(subscription_id);
    if (!start_data.stream_info.track_ready) {
        // subscription 创建成功但启动数据不可用，必须立刻 unsubscribe，
        // 避免空 subscription 长期占用 media_streams。
        media_streams_->UnsubscribeFrames(
            subscription_id, SubscriptionClose::kUnsubscribed);
        return 455;
    }
    if (!RtspMuxer::IsCodecSupported(start_data.stream_info.codec)) {
        media_streams_->UnsubscribeFrames(
            subscription_id, SubscriptionClose::kUnsubscribed);
        return 415;
    }
    const uint32_t play_rtp_timestamp =
        FirstStartFrameRtpTimestamp(start_data);
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        session.StartPlaying();
        session.SetSubscription(subscription_id,
                                start_data.generation,
                                start_data.stream_info);
        session.SetPlayRtpTimestamp(play_rtp_timestamp);
        session.SetStartFrames(&start_data.gop_frames);
    }
    return 200;
}

void RtspSessionVideoSender::StartMediaSend(
    const std::shared_ptr<RtspSession> &session) {
    StartSessionSendTimer(session);
}

void RtspSessionVideoSender::CloseSubscription(RtspSession &session,
                                               SubscriptionClose reason) {
    FrameSubscriptionId subscription_id = 0;
    event::TimerId send_timer_id = 0;
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        subscription_id = session.subscription_id;
        send_timer_id = session.drain_timer_id;
        session.ClearSubscription();
        session.ClearDrainTimer();
    }
    // 先取消发送 timer，再 unsubscribe subscription。timer 若已在执行，
    // EventLoop 的 cancelled 标记会阻止下一次触发；subscription_id 清零后
    // 本次执行也会快速退出。
    if (send_timer_id != 0) {
        (void)net_loop_->CancelTimer(send_timer_id);
    }
    if (subscription_id != 0) {
        (void)media_streams_->UnsubscribeFrames(subscription_id, reason);
    }
}

void RtspSessionVideoSender::StartSessionSendTimer(
    const std::shared_ptr<RtspSession> &session) {
    // 发送 timer 运行在 socket_io loop 上，周期性从 media_streams subscription 拉帧；
    // 每次发送有帧数上限，避免单个 RTSP 客户端长期占住 IO 线程。
    event::TimerId timer_id = 0;
    const event::EventStatus timer_status = net_loop_->RunEvery(
        kRtspSendIntervalMs, [this, session]() {
            SendSessionFrames(session);
        },
        &timer_id);
    if (timer_status != event::EventStatus::kOk || timer_id == 0) {
        FrameSubscriptionId subscription_id = 0;
        {
            std::lock_guard<std::mutex> lock(*mutex_);
            subscription_id = session->subscription_id;
            session->ClearSubscription();
        }
        // timer 创建失败时不能继续保留 subscription，否则没有发送循环消费帧队列。
        (void)media_streams_->UnsubscribeFrames(
            subscription_id, SubscriptionClose::kUnsubscribed);
        return;
    }
    std::lock_guard<std::mutex> lock(*mutex_);
    session->SetDrainTimer(timer_id);
}

void RtspSessionVideoSender::SendSessionFrames(
    const std::shared_ptr<RtspSession> &session) {
    uint32_t sent_frames = 0;
    if (!SendSessionStartFrames(session, &sent_frames)) {
        return;
    }
    FrameSubscriptionId subscription_id = 0;
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (session->state != RtspSessionState::kPlaying ||
            !session->IsSubscribed()) {
            return;
        }
        subscription_id = session->subscription_id;
    }
    while (sent_frames < kRtspMaxFramesPerSend) {
        SubscriptionFrame subscribed_frame;
        if (!media_streams_->PullFrame(subscription_id,
                                       &subscribed_frame)) {
            break;
        }
        // Pull 出来的 frame 带引用，发送路径只在本次调用内使用。
        SendFrame(session, subscribed_frame.frame);
        ++sent_frames;
    }
}

bool RtspSessionVideoSender::SendSessionStartFrames(
    const std::shared_ptr<RtspSession> &session,
    uint32_t *sent_frames) {
    if (sent_frames == nullptr) {
        return false;
    }
    while (*sent_frames < kRtspMaxFramesPerSend) {
        MediaFrame frame;
        bool has_frame = false;
        {
            std::lock_guard<std::mutex> lock(*mutex_);
            if (session->state != RtspSessionState::kPlaying) {
                return false;
            }
            if (!session->start_frames.empty()) {
                // Move 后锁外发送可以缩短 RTSP mutex
                // 持有时间，避免发送慢客户端时阻塞其它控制请求。
                frame = std::move(session->start_frames.front());
                session->start_frames.pop_front();
                has_frame = true;
            }
        }
        if (!has_frame) {
            return true;
        }
        SendFrame(session, frame);
        ++(*sent_frames);
    }
    std::lock_guard<std::mutex> lock(*mutex_);
    return session->state == RtspSessionState::kPlaying &&
           session->start_frames.empty();
}

void RtspSessionVideoSender::SendFrame(
    const std::shared_ptr<RtspSession> &session,
    const MediaFrame &frame) {
    bool should_send = false;
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        should_send = session->state == RtspSessionState::kPlaying &&
                      session->IsSubscribed() &&
                      frame.stream_id == session->stream_id &&
                      frame.codec == session->stream_info.codec;
    }
    if (should_send) {
        // SendFrame 内部会再次读取 transport 和统计字段；这里先过滤 stream/codec，
        // 防止旧 subscription 或错误码流的数据进入当前 session。
        rtp_sender_.SendFrame(*session, frame, RtpSendRefs());
    }
}

RtspRtpSendRefs RtspSessionVideoSender::RtpSendRefs() {
    return RtspRtpSendRefs{*socket_io_, *mutex_, *stats_};
}

}  // namespace live_stream

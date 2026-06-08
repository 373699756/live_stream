#ifndef LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_
#define LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_

#include <cstdint>

namespace live_stream {

struct CorrectedTimestamp {
    // 输出是从本轮 stream 起点开始的单调相对时间戳，单位微秒。
    int64_t dts_us = 0;
    int64_t pts_us = 0;
};

enum class TimestampCorrectionReset {
    kNone = 0,
    // 设备原始 DTS 回退，通常来自编码器重启或 SDK 时间基重置。
    kRollback,
    // 设备原始 DTS 正向跳变超过直播链路可接受范围，需要重建缓存。
    kJump,
};

struct TimestampCorrectionResult {
    CorrectedTimestamp timestamp;
    TimestampCorrectionReset reset = TimestampCorrectionReset::kNone;
    int64_t raw_delta_us = 0;
};

class TimestampCorrector {
public:
    void Reset();
    CorrectedTimestamp Correct(int64_t dts_us, int64_t pts_us);
    TimestampCorrectionResult CorrectWithReset(int64_t dts_us,
                                               int64_t pts_us);
    int64_t last_dts_us() const { return last_dts_us_; }

private:
    // 直播预览遇到超过 3 秒的 DTS 跳变时，继续沿用上一帧间隔平滑输出，
    // 同时把 reset 信号交给 media_source 清理 GOP/HLS/reader 缓存。
    static constexpr int64_t kMaxLiveTimestampJumpUs = 3 * 1000 * 1000;

    bool initialized_ = false;
    int64_t last_raw_dts_us_ = 0;
    int64_t last_raw_pts_us_ = 0;
    int64_t last_dts_us_ = 0;
    int64_t last_pts_us_ = 0;
    int64_t last_delta_us_ = 33333;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_

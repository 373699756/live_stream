#ifndef LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_
#define LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_

#include <cstdint>

namespace live_stream {

struct CorrectedTimestamp {
    int64_t dts_us = 0;
    int64_t pts_us = 0;
};

enum class TimestampCorrectionReset {
    kNone = 0,
    kRollback,
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

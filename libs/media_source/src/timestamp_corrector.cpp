#include "timestamp_corrector.h"

#include <cstdlib>

namespace live_stream {

void TimestampCorrector::Reset() {
    initialized_ = false;
    last_raw_dts_us_ = 0;
    last_raw_pts_us_ = 0;
    last_dts_us_ = 0;
    last_pts_us_ = 0;
    last_delta_us_ = 33333;
}

CorrectedTimestamp TimestampCorrector::Correct(int64_t dts_us,
                                               int64_t pts_us) {
    return CorrectWithReset(dts_us, pts_us).timestamp;
}

TimestampCorrectionResult TimestampCorrector::CorrectWithReset(
    int64_t dts_us, int64_t pts_us) {
    if (pts_us == 0) {
        pts_us = dts_us;
    }
    if (dts_us < 0) {
        dts_us = 0;
    }
    if (pts_us < 0) {
        pts_us = dts_us;
    }

    TimestampCorrectionResult result;
    if (!initialized_) {
        initialized_ = true;
        last_raw_dts_us_ = dts_us;
        last_raw_pts_us_ = pts_us;
        last_dts_us_ = 0;
        int64_t pts_dts_diff_us = pts_us - dts_us;
        if (std::llabs(pts_dts_diff_us) > 500000 ||
            pts_dts_diff_us < 0) {
            pts_dts_diff_us = 0;
        }
        last_pts_us_ = pts_dts_diff_us;
        result.timestamp = CorrectedTimestamp{0, pts_dts_diff_us};
        return result;
    }

    const int64_t raw_delta_us = dts_us - last_raw_dts_us_;
    result.raw_delta_us = raw_delta_us;
    int64_t corrected_delta_us = raw_delta_us;
    if (raw_delta_us < 0) {
        result.reset = TimestampCorrectionReset::kRollback;
        corrected_delta_us = last_delta_us_;
    } else if (raw_delta_us > kMaxLiveTimestampJumpUs) {
        result.reset = TimestampCorrectionReset::kJump;
        corrected_delta_us = last_delta_us_;
    } else if (raw_delta_us == 0) {
        corrected_delta_us = 1;
    } else {
        last_delta_us_ = raw_delta_us;
    }

    if (corrected_delta_us <= 0) {
        corrected_delta_us = 1;
    }

    int64_t corrected_dts_us = last_dts_us_ + corrected_delta_us;
    int64_t pts_dts_diff_us = pts_us - dts_us;
    if (std::llabs(pts_dts_diff_us) > 500000) {
        pts_dts_diff_us = 0;
    }
    int64_t corrected_pts_us = corrected_dts_us + pts_dts_diff_us;
    if (corrected_pts_us < corrected_dts_us) {
        corrected_pts_us = corrected_dts_us;
    }
    if (corrected_pts_us < last_pts_us_) {
        corrected_pts_us = last_pts_us_;
    }

    last_raw_dts_us_ = dts_us;
    last_raw_pts_us_ = pts_us;
    last_dts_us_ = corrected_dts_us;
    last_pts_us_ = corrected_pts_us;
    result.timestamp = CorrectedTimestamp{corrected_dts_us,
                                          corrected_pts_us};
    return result;
}

}  // namespace live_stream

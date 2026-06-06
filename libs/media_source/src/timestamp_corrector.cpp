#include "timestamp_corrector.h"

namespace live_stream {

void TimestampCorrector::Reset() {
    initialized_ = false;
    base_dts_us_ = 0;
    last_dts_us_ = 0;
    last_pts_us_ = 0;
}

CorrectedTimestamp TimestampCorrector::Correct(int64_t dts_us,
                                               int64_t pts_us) {
    if (pts_us == 0) {
        pts_us = dts_us;
    }
    if (!initialized_) {
        initialized_ = true;
        base_dts_us_ = dts_us;
    }

    int64_t relative_dts_us = dts_us - base_dts_us_;
    int64_t relative_pts_us = pts_us - base_dts_us_;
    if (relative_dts_us < 0) {
        relative_dts_us = 0;
    }
    if (relative_pts_us < relative_dts_us) {
        relative_pts_us = relative_dts_us;
    }

    if (relative_dts_us <= last_dts_us_) {
        relative_dts_us = last_dts_us_ + 1;
    }
    if (relative_pts_us < relative_dts_us) {
        relative_pts_us = relative_dts_us;
    }
    if (relative_pts_us < last_pts_us_) {
        relative_pts_us = last_pts_us_;
    }

    last_dts_us_ = relative_dts_us;
    last_pts_us_ = relative_pts_us;
    return CorrectedTimestamp{relative_dts_us, relative_pts_us};
}

}  // namespace live_stream

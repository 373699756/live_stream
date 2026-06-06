#ifndef LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_
#define LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_

#include <cstdint>

namespace live_stream {

struct CorrectedTimestamp {
    int64_t dts_us = 0;
    int64_t pts_us = 0;
};

class TimestampCorrector {
public:
    void Reset();
    CorrectedTimestamp Correct(int64_t dts_us, int64_t pts_us);

private:
    bool initialized_ = false;
    int64_t base_dts_us_ = 0;
    int64_t last_dts_us_ = 0;
    int64_t last_pts_us_ = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_TIMESTAMP_CORRECTOR_H_

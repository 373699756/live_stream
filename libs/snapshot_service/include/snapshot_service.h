#ifndef LIVE_STREAM_SNAPSHOT_SERVICE_H_
#define LIVE_STREAM_SNAPSHOT_SERVICE_H_

#include "infra/status.h"
#include "infra/media_buffer.h"
#include "infra/service.h"
#include "infra/stream_types.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"

#include <cstdint>
#include <memory>

namespace live_stream {

struct SnapshotConfig {
    int32_t snap_pipe = 2;
    int32_t snap_vpss_group = 2;
    int32_t snap_vpss_channel = 0;
    int32_t jpeg_venc_channel = 1;
    VideoSize size;
    uint32_t frame_count = 1;
    uint32_t repeat_send_times = 1;
    bool load_ccm = true;
    bool zero_shutter_lag = false;
};

struct CaptureRequest {
    infra::StreamId stream_id = infra::StreamId::kSnapshot;
    uint32_t timeout_ms = 3000;
    uint32_t jpeg_quality = 90;
    bool include_thumbnail = true;
};

struct SnapshotFrame {
    std::shared_ptr<infra::IMediaBuffer> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;

    infra::BufferSlice PayloadSlice() const {
        return infra::BufferSlice{buffer, offset, size};
    }
};

class SnapshotService : public infra::IService {
 public:
    SnapshotService();
    explicit SnapshotService(const SnapshotConfig& config);
    ~SnapshotService() override;

    infra::Status Init() override;
    infra::Status Start() override;
    void Stop() override;
    void Deinit() override;
    const char* Name() const override;

    static const char* StaticName();

    infra::Status BindMedia(const MediaChannels& channels);
    infra::Result<SnapshotFrame> Capture(const CaptureRequest& request);
    bool IsCapturing() const;

 private:
    struct Impl;
    Impl* impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_SNAPSHOT_SERVICE_H_

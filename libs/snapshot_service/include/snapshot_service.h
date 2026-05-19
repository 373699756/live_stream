#ifndef LIVE_STREAM_SNAPSHOT_SERVICE_H_
#define LIVE_STREAM_SNAPSHOT_SERVICE_H_

#include "media/media_buffer.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"
#include "media/stream_types.h"

#include <cstdint>
#include <memory>

namespace live_stream {

class IConfigService;
class MediaService;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

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
    StreamId stream_id = StreamId::kMain;
    uint32_t timeout_ms = 3000;
    uint32_t jpeg_quality = 90;
    bool include_thumbnail = true;
};

struct SnapshotFrame {
    std::shared_ptr<IMediaBuffer> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;

    BufferSlice PayloadSlice() const { return BufferSlice{buffer, offset, size}; }

    bool HasValidPayload() const {
        return size != 0 && IsValidBufferSlice(PayloadSlice());
    }

    const uint8_t *PayloadData() const {
        return HasValidPayload() ? BufferSliceData(PayloadSlice()) : nullptr;
    }
};

struct SnapshotServiceOptions {
    SnapshotConfig default_config;
    IConfigService *config_service = nullptr;
    MediaService *media_service = nullptr;
    hisisdk::IHisiSdk *sdk = nullptr;
};

struct SnapshotServiceStats {
    uint64_t config_apply_count = 0;
    uint64_t config_apply_failed_count = 0;
    uint64_t capture_count = 0;
    uint64_t capture_failed_count = 0;
    uint32_t jpeg_quality = 90;
    uint32_t timeout_ms = 3000;
    bool enabled = true;
    bool capturing = false;
};

// ISnapshotView is the narrow interface consumed by HttpService (and other
// cross-module consumers). SnapshotService implements it.
class ISnapshotView {
public:
    virtual ~ISnapshotView() = default;
    virtual SnapshotServiceStats GetStats() const = 0;
    virtual SnapshotFrame Capture(const CaptureRequest &request) = 0;
};

class SnapshotService : public ISnapshotView {
public:
    SnapshotService();
    explicit SnapshotService(const SnapshotConfig &config);
    explicit SnapshotService(const SnapshotServiceOptions &options);
    ~SnapshotService();

    bool Start();
    void Stop();

    static const char *StaticName();

    bool BindMedia(const MediaChannels &channels);
    SnapshotFrame Capture(const CaptureRequest &request) override;
    bool IsCapturing() const;
    SnapshotServiceStats GetStats() const override;

private:
    struct Impl;
    Impl *impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_SNAPSHOT_SERVICE_H_

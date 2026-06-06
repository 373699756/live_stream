#ifndef LIVE_STREAM_SNAPSHOT_SNAPSHOT_H_
#define LIVE_STREAM_SNAPSHOT_SNAPSHOT_H_

#include "media/media_buffer.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"
#include "media/stream_types.h"

#include <cstdint>
#include <memory>

namespace live_stream {

class IConfig;
class IDeviceMedia;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

struct SnapshotConfig {
    int32_t snap_pipe = 2;
    int32_t snap_vpss_group = 2;
    int32_t snap_vpss_channel = 0;
    int32_t jpeg_venc_channel = 3;
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
    VideoBuffer *buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;

    SnapshotFrame() = default;

    SnapshotFrame(const SnapshotFrame &other)
        : buffer(VideoBufferRef(other.buffer)),
          offset(other.offset),
          size(other.size),
          width(other.width),
          height(other.height),
          pts_us(other.pts_us) {}

    SnapshotFrame &operator=(const SnapshotFrame &other) {
        if (this == &other) {
            return *this;
        }
        VideoBuffer *retained = VideoBufferRef(other.buffer);
        VideoBufferUnref(buffer);
        buffer = retained;
        offset = other.offset;
        size = other.size;
        width = other.width;
        height = other.height;
        pts_us = other.pts_us;
        return *this;
    }

    SnapshotFrame(SnapshotFrame &&other) noexcept
        : buffer(other.buffer),
          offset(other.offset),
          size(other.size),
          width(other.width),
          height(other.height),
          pts_us(other.pts_us) {
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
    }

    SnapshotFrame &operator=(SnapshotFrame &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        VideoBufferUnref(buffer);
        buffer = other.buffer;
        offset = other.offset;
        size = other.size;
        width = other.width;
        height = other.height;
        pts_us = other.pts_us;
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
        return *this;
    }

    ~SnapshotFrame() { VideoBufferUnref(buffer); }

    BufferSlice PayloadSlice() const { return BufferSlice{buffer, offset, size}; }

    bool HasValidPayload() const {
        return size != 0 && IsValidBufferSlice(PayloadSlice());
    }

    const uint8_t *PayloadData() const {
        return HasValidPayload() ? BufferSliceData(PayloadSlice()) : nullptr;
    }
};

struct SnapshotOptions {
    SnapshotConfig default_config;
    IConfig *config = nullptr;
    IDeviceMedia *device_media = nullptr;
    MediaChannels media_channels;
    hisisdk::IHisiSdk *sdk = nullptr;
};

struct SnapshotStats {
    uint64_t config_apply_count = 0;
    uint64_t config_apply_failed_count = 0;
    uint64_t capture_count = 0;
    uint64_t capture_failed_count = 0;
    uint32_t jpeg_quality = 90;
    uint32_t timeout_ms = 3000;
    bool enabled = true;
    bool capturing = false;
};

// ISnapshotView is the narrow interface consumed by http (and other
// cross-module consumers). Snapshot implements it.
class ISnapshotView {
public:
    virtual ~ISnapshotView() = default;
    virtual SnapshotStats GetStats() const = 0;
    virtual SnapshotFrame Capture(const CaptureRequest &request) = 0;
};

class Snapshot : public ISnapshotView {
public:
    Snapshot();
    explicit Snapshot(const SnapshotConfig &config);
    explicit Snapshot(const SnapshotOptions &options);
    ~Snapshot();

    bool Start();
    void Stop();

    static const char *StaticName();

    bool BindMedia(const MediaChannels &channels);
    SnapshotFrame Capture(const CaptureRequest &request) override;
    bool IsCapturing() const;
    SnapshotStats GetStats() const override;

private:
    struct Impl;
    Impl *impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_SNAPSHOT_SNAPSHOT_H_

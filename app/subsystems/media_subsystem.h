#ifndef LIVE_STREAM_APP_SUBSYSTEMS_MEDIA_SUBSYSTEM_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_MEDIA_SUBSYSTEM_H_

#include <memory>

#include "ai.h"
#include "device.h"
#include "media/media_streams.h"

namespace live_stream {

class CoreSubsystem;
struct DeviceRefs;

struct MediaRefs {
    DeviceMedia* device = nullptr;
    MediaStreams *media_streams = nullptr;
    Ai* ai = nullptr;
};

class MediaSubsystem {
public:
    static MediaSubsystem& Get();

    // DeviceMedia owns snapshot and overlay resources. Stop keeps AI before
    // device so inference and alert capture stop before hardware teardown.
    bool Start(CoreSubsystem &core_subsystem, const DeviceRefs &device_refs);
    void Stop();
    MediaRefs refs() const;

private:
    MediaSubsystem() = default;
    ~MediaSubsystem() = default;

    MediaSubsystem(const MediaSubsystem&) = delete;
    MediaSubsystem& operator=(const MediaSubsystem&) = delete;

    std::unique_ptr<DeviceMedia> device_;
    std::unique_ptr<MediaStreams> media_streams_;
    std::unique_ptr<Ai> ai_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_MEDIA_SUBSYSTEM_H_

#ifndef LIVE_STREAM_APP_MEDIA_SUBSYSTEM_H_
#define LIVE_STREAM_APP_MEDIA_SUBSYSTEM_H_

#include <memory>

#include "ai.h"
#include "device_media.h"
#include "region.h"
#include "snapshot.h"

namespace live_stream {

class CoreSubsystem;
struct DeviceRefs;

struct MediaRefs {
    IDeviceMedia* device_media = nullptr;
    Ai* ai = nullptr;
    Snapshot* snapshot = nullptr;
};

class MediaSubsystem {
public:
    static MediaSubsystem& Get();

    // Start order is device_media -> snapshot -> ai -> region. Stop keeps the
    // reverse order so SDK regions, AI capture, and snapshots release MPP
    // channel use before device_media tears down the hardware pipeline.
    bool Start(CoreSubsystem &core_subsystem, const DeviceRefs &device_refs);
    void Stop();
    MediaRefs refs() const;

private:
    MediaSubsystem() = default;
    ~MediaSubsystem() = default;

    MediaSubsystem(const MediaSubsystem&) = delete;
    MediaSubsystem& operator=(const MediaSubsystem&) = delete;

    std::unique_ptr<IDeviceMedia> device_media_;
    std::unique_ptr<Ai> ai_;
    std::unique_ptr<Region> region_;
    std::unique_ptr<Snapshot> snapshot_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_MEDIA_SUBSYSTEM_H_

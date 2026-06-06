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
    IDeviceMedia* media = nullptr;
    Ai* ai = nullptr;
    Region* overlay = nullptr;
    Snapshot* snapshot = nullptr;
};

class MediaSubsystem {
public:
    static MediaSubsystem& Get();

    bool Start(CoreSubsystem &core_subsystem, const DeviceRefs &device_refs);
    void Stop();
    MediaRefs refs() const;

private:
    MediaSubsystem() = default;
    ~MediaSubsystem() = default;

    MediaSubsystem(const MediaSubsystem&) = delete;
    MediaSubsystem& operator=(const MediaSubsystem&) = delete;

    std::unique_ptr<IDeviceMedia> media_;
    std::unique_ptr<Ai> ai_;
    std::unique_ptr<Region> overlay_;
    std::unique_ptr<Snapshot> snapshot_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_MEDIA_SUBSYSTEM_H_

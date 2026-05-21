#ifndef LIVE_STREAM_APP_MEDIA_SUBSYSTEM_H_
#define LIVE_STREAM_APP_MEDIA_SUBSYSTEM_H_

#include <memory>

#include "ai_service.h"
#include "media_service.h"
#include "osd_service.h"
#include "snapshot_service.h"

namespace live_stream {

struct MediaRefs {
    IMediaService* media = nullptr;
    AiService* ai = nullptr;
    OsdService* osd = nullptr;
    SnapshotService* snapshot = nullptr;
};

class MediaSubsystem {
public:
    static MediaSubsystem& Get();

    bool Start();
    void Stop();
    MediaRefs refs() const;

private:
    MediaSubsystem() = default;
    ~MediaSubsystem() = default;

    MediaSubsystem(const MediaSubsystem&) = delete;
    MediaSubsystem& operator=(const MediaSubsystem&) = delete;

    std::unique_ptr<IMediaService> media_;
    std::unique_ptr<AiService> ai_;
    std::unique_ptr<OsdService> osd_;
    std::unique_ptr<SnapshotService> snapshot_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_MEDIA_SUBSYSTEM_H_

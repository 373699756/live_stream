#ifndef LIVE_STREAM_APP_PROTOCOL_SUBSYSTEM_H_
#define LIVE_STREAM_APP_PROTOCOL_SUBSYSTEM_H_

#include <memory>

#include "media_pipeline.h"
#include "http.h"
#include "infra/executor.h"
#include "net.h"
#include "net_adaptive.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "runtime_config.h"
#include "webrtc.h"

namespace live_stream {

class CoreSubsystem;
struct DeviceRefs;
struct MediaRefs;

struct ProtocolRefs {
    IRtsp *rtsp = nullptr;
    IWebrtc *webrtc = nullptr;
    OnvifServer *onvif = nullptr;
    IHttp *http = nullptr;
};

class ProtocolSubsystem {
public:
    static ProtocolSubsystem &Get();

    bool Start(const AppRuntimeConfig &runtime_config,
               CoreSubsystem &core_subsystem,
               const DeviceRefs &device_refs,
               const MediaRefs &media_refs);
    void Stop();
    ProtocolRefs refs() const;
    NetEngine *net_engine() const { return net_engine_.get(); }

private:
    ProtocolSubsystem() = default;
    ~ProtocolSubsystem() = default;

    ProtocolSubsystem(const ProtocolSubsystem &) = delete;
    ProtocolSubsystem &operator=(const ProtocolSubsystem &) = delete;

    std::unique_ptr<infra::Executor> net_callback_executor_;
    std::unique_ptr<NetEngine> net_engine_;
    std::unique_ptr<IRtsp> rtsp_;
    std::unique_ptr<IWebrtc> webrtc_;
    std::unique_ptr<IMediaPipeline> media_pipeline_;
    std::unique_ptr<OnvifServer> onvif_;
    std::unique_ptr<IHttp> http_;
    std::unique_ptr<INetAdaptive> net_adaptive_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PROTOCOL_SUBSYSTEM_H_

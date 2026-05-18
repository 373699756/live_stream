#ifndef LIVE_STREAM_APP_PROTOCOL_SUBSYSTEM_H_
#define LIVE_STREAM_APP_PROTOCOL_SUBSYSTEM_H_

#include <memory>

#include "stream_hub_service.h"
#include "http_service.h"
#include "infra/executor.h"
#include "net_service.h"
#include "onvif_service.h"
#include "rtsp_service.h"
#include "runtime_config.h"
#include "webrtc_service.h"

namespace live_stream {

struct ProtocolRefs {
    IRtspService *rtsp = nullptr;
    IWebrtcService *webrtc = nullptr;
    IOnvifService *onvif = nullptr;
    IHttpService *http = nullptr;
};

class ProtocolSubsystem {
public:
    static ProtocolSubsystem &Get();

    bool Start(const AppRuntimeConfig &runtime_config);
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
    std::unique_ptr<IRtspService> rtsp_;
    std::unique_ptr<IWebrtcService> webrtc_;
    std::unique_ptr<IStreamHubService> stream_hub_;
    std::unique_ptr<IOnvifUriProvider> onvif_uri_provider_;
    std::unique_ptr<IOnvifService> onvif_;
    std::unique_ptr<IHttpService> http_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PROTOCOL_SUBSYSTEM_H_

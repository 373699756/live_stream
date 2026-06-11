#ifndef LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_SUBSYSTEM_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_SUBSYSTEM_H_

#include <memory>
#include <string>

#include "config/app_config.h"
#include "http.h"
#include "infra/executor.h"
#include "net.h"
#include "net_stat.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "subsystems/protocol_refs.h"
#include "webrtc.h"

namespace live_stream {

class CoreSubsystem;
class INetworkConfig;
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

    bool Start(const AppConfig &app_config,
               CoreSubsystem &core_subsystem,
               const DeviceRefs &device_refs,
               const MediaRefs &media_refs);
    void Stop();
    ProtocolRefs refs() const;
    INetEngine *net_engine() const { return net_engine_.get(); }

private:
    ProtocolSubsystem() = default;
    ~ProtocolSubsystem() = default;

    ProtocolSubsystem(const ProtocolSubsystem &) = delete;
    ProtocolSubsystem &operator=(const ProtocolSubsystem &) = delete;

    bool InstallConfigUpdateAttachments();
    void DetachConfigUpdateAttachments();
    ConfigResult ValidateProtocolConfigUpdate(const std::string &scope,
                                              const ConfigJson &value);
    ConfigResult ApplyProtocolConfigUpdate(const std::string &scope,
                                           const ConfigJson &value);
    bool BuildNextAppConfig(const std::string &scope,
                            const ConfigJson &value,
                            AppConfig *next_config) const;

    std::unique_ptr<infra::Executor> net_callback_executor_;
    std::unique_ptr<INetEngine> net_engine_;
    std::unique_ptr<IRtsp> rtsp_;
    std::unique_ptr<IWebrtc> webrtc_;
    std::unique_ptr<OnvifServer> onvif_;
    std::unique_ptr<IHttp> http_;
    std::unique_ptr<INetStat> net_stat_;
    IConfig *config_ = nullptr;
    INetworkConfig *network_config_ = nullptr;
    AppConfig app_config_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_SUBSYSTEM_H_

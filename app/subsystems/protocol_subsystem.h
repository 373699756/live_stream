#ifndef LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_SUBSYSTEM_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_SUBSYSTEM_H_

#include <memory>
#include <string>

#include "config/app_config.h"
#include "http.h"
#include "net.h"
#include "net_stat.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "subsystems/protocol_refs.h"
#include "webrtc.h"

namespace live_stream {

class CoreSubsystem;
class INetwork;
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

    bool InstallConfigUpdateScopes();
    void RemoveConfigUpdateScopes();
    ConfigStatus VerifyProtocolConfigUpdate(const std::string &scope,
                                            const ConfigJson &now,
                                            ConfigIssue *issue);
    ConfigStatus ApplyProtocolConfigUpdate(const std::string &scope,
                                           const ConfigJson &prev,
                                           const ConfigJson &now,
                                           ConfigIssue *issue);
    bool BuildNextAppConfig(const std::string &scope,
                            const ConfigJson &value,
                            AppConfig *next_config) const;

    std::unique_ptr<event::Loop> net_callback_loop_;
    std::unique_ptr<INetEngine> net_engine_;
    std::unique_ptr<IRtsp> rtsp_;
    std::unique_ptr<IWebrtc> webrtc_;
    std::unique_ptr<OnvifServer> onvif_;
    std::unique_ptr<IHttp> http_;
    std::unique_ptr<INetStat> net_stat_;
    IConfig *config_ = nullptr;
    INetwork *network_ = nullptr;
    AppConfig app_config_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_SUBSYSTEM_H_

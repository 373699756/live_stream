#ifndef LIVE_STREAM_RUNTIME_RUNTIME_H_
#define LIVE_STREAM_RUNTIME_RUNTIME_H_

namespace live_stream {

class IAuth;
class IConfig;
class ILogger;
class INetIo;

namespace event {
class EventCenter;
}  // namespace event

class Runtime {
public:
    static bool InstallLogger(ILogger *logger);
    static bool InstallConfig(IConfig *config);
    static bool InstallAuth(IAuth *auth);
    static bool InstallEventCenter(event::EventCenter *event_center);
    static bool InstallNetIo(INetIo *net_io);

    static ILogger *Logger();
    static IConfig *Config();
    static IAuth *Auth();
    static event::EventCenter *EventCenter();
    static INetIo *NetIo();

    static void ClearNetIo(INetIo *net_io);
    static void Clear();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RUNTIME_RUNTIME_H_

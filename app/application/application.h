#ifndef LIVE_STREAM_APP_APPLICATION_APPLICATION_H_
#define LIVE_STREAM_APP_APPLICATION_APPLICATION_H_

#include "config/app_config.h"
#include "application/startup_paths.h"

namespace live_stream {

class Application {
public:
    static Application& Get();

    bool Start(const StartupPaths& paths, const char* static_root_override);
    void Stop();
    void RunUntilSignal();

private:
    Application() = default;
    ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    AppConfig app_config_;
    bool started_ = false;
};

void RequestAppStop();
void InstallAppSignalHandlers();

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_APPLICATION_APPLICATION_H_

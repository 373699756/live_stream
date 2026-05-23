#ifndef LIVE_STREAM_APP_APP_RUNTIME_H_
#define LIVE_STREAM_APP_APP_RUNTIME_H_

#include "core_services.h"
#include "runtime_config.h"

namespace live_stream {

class AppRuntime {
public:
    static AppRuntime& Get();

    bool Start(const RuntimePaths& paths, const char* static_root_override);
    void Stop();
    void RunUntilSignal();

private:
    AppRuntime() = default;
    ~AppRuntime() = default;

    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    AppRuntimeConfig runtime_config_;
    bool started_ = false;
};

void RequestAppStop();
void InstallAppSignalHandlers();

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_APP_RUNTIME_H_

#include "runtime.h"

#include <mutex>

namespace live_stream {
namespace {

std::mutex g_runtime_mutex;
ILogger *g_logger = nullptr;
IConfig *g_config = nullptr;
IAuth *g_auth = nullptr;
event::EventCenter *g_event_center = nullptr;
INetIo *g_net_io = nullptr;

template <typename T>
bool InstallPointer(T *next, T **slot) {
    if (next == nullptr || slot == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    if (*slot != nullptr && *slot != next) {
        return false;
    }
    *slot = next;
    return true;
}

}  // namespace

bool Runtime::InstallLogger(ILogger *logger) {
    return InstallPointer(logger, &g_logger);
}

bool Runtime::InstallConfig(IConfig *config) {
    return InstallPointer(config, &g_config);
}

bool Runtime::InstallAuth(IAuth *auth) {
    return InstallPointer(auth, &g_auth);
}

bool Runtime::InstallEventCenter(event::EventCenter *event_center) {
    return InstallPointer(event_center, &g_event_center);
}

bool Runtime::InstallNetIo(INetIo *net_io) {
    return InstallPointer(net_io, &g_net_io);
}

ILogger *Runtime::Logger() {
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    return g_logger;
}

IConfig *Runtime::Config() {
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    return g_config;
}

IAuth *Runtime::Auth() {
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    return g_auth;
}

event::EventCenter *Runtime::EventCenter() {
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    return g_event_center;
}

INetIo *Runtime::NetIo() {
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    return g_net_io;
}

void Runtime::ClearNetIo(INetIo *net_io) {
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    if (g_net_io == net_io) {
        g_net_io = nullptr;
    }
}

void Runtime::Clear() {
    std::lock_guard<std::mutex> guard(g_runtime_mutex);
    g_net_io = nullptr;
    g_event_center = nullptr;
    g_auth = nullptr;
    g_config = nullptr;
    g_logger = nullptr;
}

}  // namespace live_stream

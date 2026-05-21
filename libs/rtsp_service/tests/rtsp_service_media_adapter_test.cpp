#include "rtsp_service.h"

#include "fake_stream_hub.h"
#include "net_service.h"

int main() {
    std::unique_ptr<live_stream::NetEngine> net_engine =
        live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!net_engine || !net_engine->Start()) {
        return 1;
    }

    live_stream::test::FakeStreamHub stream_hub;
    live_stream::RtspServiceOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    live_stream::RtspServiceDependencies deps;
    deps.net_engine = net_engine.get();
    deps.stream_hub = &stream_hub;

    auto rtsp = live_stream::CreateRtspService(options, deps);
    if (!rtsp || !rtsp->Start()) {
        return 2;
    }
    if (stream_hub.main_sink == nullptr || stream_hub.sub_sink == nullptr) {
        return 3;
    }
    rtsp->Stop();
    if (stream_hub.main_sink != nullptr || stream_hub.sub_sink != nullptr) {
        return 4;
    }
    net_engine->Stop();
    return 0;
}

#include "rtsp.h"

#include "fake_media_streams.h"
#include "net.h"

int main() {
    std::unique_ptr<live_stream::INetEngine> net_engine =
        live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!net_engine || !net_engine->Start()) {
        return 1;
    }

    live_stream::test::FakeMediaStreams media_streams;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    live_stream::RtspDependencies deps;
    deps.net_engine = net_engine.get();
    deps.net_loop = net_engine->DefaultLoop();
    deps.media_streams = &media_streams;

    auto rtsp = live_stream::CreateRtsp(options, deps);
    if (!rtsp || !rtsp->Start()) {
        return 2;
    }
    if (media_streams.ActiveSubscriptionCount() != 0) {
        return 3;
    }
    rtsp->Stop();
    if (media_streams.ActiveSubscriptionCount() != 0) {
        return 4;
    }
    net_engine->Stop();
    return 0;
}

#include "rtsp.h"

#include "fake_media_source.h"
#include "net.h"

int main() {
    std::unique_ptr<live_stream::NetEngine> net_engine =
        live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!net_engine || !net_engine->Start()) {
        return 1;
    }

    live_stream::test::FakeMediaFrameSource media_source;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    live_stream::RtspDependencies deps;
    deps.net_engine = net_engine.get();
    deps.media_source = &media_source;

    auto rtsp = live_stream::CreateRtsp(options, deps);
    if (!rtsp || !rtsp->Start()) {
        return 2;
    }
    if (media_source.ActiveReaderCount() != 0) {
        return 3;
    }
    rtsp->Stop();
    if (media_source.ActiveReaderCount() != 0) {
        return 4;
    }
    net_engine->Stop();
    return 0;
}

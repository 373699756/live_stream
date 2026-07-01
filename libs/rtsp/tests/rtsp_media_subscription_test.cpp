#include "rtsp.h"

#include "fake_media_streams.h"
#include "net.h"

int main() {
    std::unique_ptr<live_stream::INetIo> net_io =
        live_stream::CreateNetIo(live_stream::NetIoOptions{});
    if (!net_io || !net_io->Start()) {
        return 1;
    }

    live_stream::test::FakeMediaStreams media_streams;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    live_stream::RtspDependencies deps;
    deps.net_io = net_io.get();
    deps.net_loop = net_io->DefaultLoop();

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
    net_io->Stop();
    return 0;
}

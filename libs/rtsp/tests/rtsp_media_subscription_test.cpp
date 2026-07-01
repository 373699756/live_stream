#include "rtsp.h"

#include "fake_media_streams.h"
#include "net.h"
#include "runtime.h"

int main() {
    std::unique_ptr<live_stream::INetIo> net_io =
        live_stream::CreateNetIo(live_stream::NetIoOptions{});
    if (!net_io || !net_io->Start()) {
        return 1;
    }
    (void)live_stream::Runtime::InstallNetIo(net_io.get());

    live_stream::test::FakeMediaStreams media_streams;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    auto rtsp = live_stream::CreateRtsp(options, net_io->DefaultLoop());
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
    live_stream::Runtime::Clear();
    return 0;
}

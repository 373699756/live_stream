#include "rtsp.h"

#include "fake_media_streams.h"
#include "socket_io.h"
#include "runtime.h"

int main() {
    std::unique_ptr<live_stream::ISocketIo> socket_io =
        live_stream::CreateSocketIo(live_stream::SocketIoOptions{});
    if (!socket_io || !socket_io->Start()) {
        return 1;
    }
    (void)live_stream::Runtime::InstallSocketIo(socket_io.get());

    live_stream::test::FakeMediaStreams media_streams;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    auto rtsp = live_stream::CreateRtsp(options, socket_io->DefaultLoop());
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
    socket_io->Stop();
    live_stream::Runtime::Clear();
    return 0;
}

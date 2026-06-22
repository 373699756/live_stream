#include "http_server.h"

#include "net.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeRequestHandler : public live_stream::HttpRequestHandler {
public:
    bool ShouldUseStreamExecutor(const live_stream::HttpRequest&) const override {
        return false;
    }

    live_stream::HttpResponse HandleHttpRequest(
        const live_stream::HttpRequest&) override {
        live_stream::HttpResponse response;
        response.status_code = 200;
        response.body = "{}";
        return response;
    }

    live_stream::HttpStreamingRequestResult HandleStreamingHttpRequest(
        live_stream::ConnectionId,
        const live_stream::HttpRequest&) override {
        return live_stream::HttpStreamingRequestResult::kNotHandled;
    }
};

class FakeNetEngine : public live_stream::INetEngine {
public:
    struct Timer {
        live_stream::event::TimerId id = 0;
        live_stream::event::Task task;
        bool cancelled = false;
    };

    class FakeLoop : public live_stream::event::Loop {
    public:
        explicit FakeLoop(FakeNetEngine* engine) : engine_(engine) {}

        live_stream::event::EventStatus Post(
            live_stream::event::Task task) override {
            if (task) {
                task();
            }
            return live_stream::event::EventStatus::kOk;
        }

        live_stream::event::EventStatus RunAfter(
            uint32_t delay_ms,
            live_stream::event::Task task,
            live_stream::event::TimerId* timer_id) override {
            return engine_->RunTimerAfter(delay_ms, std::move(task),
                                          timer_id);
        }

        live_stream::event::EventStatus RunEvery(
            uint32_t,
            live_stream::event::Task,
            live_stream::event::TimerId* timer_id) override {
            if (timer_id == nullptr) {
                return live_stream::event::EventStatus::kInvalid;
            }
            *timer_id = 0;
            return live_stream::event::EventStatus::kInvalid;
        }

        bool CancelTimer(live_stream::event::TimerId id) override {
            return engine_->CancelTimerById(id);
        }

        bool IsCurrentThread() const override { return true; }

    private:
        FakeNetEngine* engine_ = nullptr;
    };

    FakeNetEngine() : loop_(this) {}

    bool Start() override { return true; }
    void Stop() override {}

    live_stream::event::Loop* DefaultLoop() override {
        return &loop_;
    }

    live_stream::event::Loop* PickLoop() override {
        return &loop_;
    }

    live_stream::TcpServerId ListenTcp(
        live_stream::event::Loop*,
        const live_stream::TcpListenOptions& options,
        const live_stream::TcpCallbacks& callbacks) override {
        listen_options = options;
        callbacks_ = callbacks;
        return 7;
    }

    bool CloseTcp(live_stream::TcpServerId id) override {
        last_closed_server = id;
        ++close_tcp_count;
        return true;
    }

    live_stream::UdpSocketId BindUdp(
        live_stream::event::Loop*,
        const live_stream::UdpBindOptions&,
        const live_stream::UdpCallbacks&) override {
        return 1;
    }

    bool CloseUdp(live_stream::UdpSocketId) override { return true; }

    bool Send(live_stream::ConnectionId, const uint8_t*, size_t) override {
        return true;
    }

    bool Close(live_stream::ConnectionId id) override {
        last_closed_connection = id;
        ++close_count;
        return true;
    }

    bool CloseAfterSend(live_stream::ConnectionId id) override {
        return Close(id);
    }

    bool SendTo(live_stream::UdpSocketId,
                live_stream::NetAddress,
                const uint8_t*,
                size_t) override {
        return true;
    }

    bool SetUdpPeer(live_stream::UdpSocketId,
                    live_stream::NetAddress) override {
        return true;
    }

    bool SendToPeer(live_stream::UdpSocketId,
                    const uint8_t*,
                    size_t) override {
        return true;
    }

    live_stream::event::EventStatus RunTimerAfter(
        uint32_t delay_ms,
        live_stream::event::Task task,
        live_stream::event::TimerId* timer_id) {
        if (timer_id == nullptr || !task) {
            return live_stream::event::EventStatus::kInvalid;
        }
        last_delay_ms = delay_ms;
        Timer timer;
        timer.id = next_timer_id++;
        timer.task = std::move(task);
        timers.push_back(std::move(timer));
        *timer_id = timers.back().id;
        return live_stream::event::EventStatus::kOk;
    }

    bool CancelTimerById(live_stream::event::TimerId id) {
        ++cancel_count;
        cancelled_timer_ids.push_back(id);
        for (Timer& timer : timers) {
            if (timer.id == id) {
                timer.cancelled = true;
                return true;
            }
        }
        return false;
    }

    live_stream::NetAddress TcpLocalAddress(
        live_stream::TcpServerId) const override {
        return live_stream::NetAddress{"127.0.0.1", 8080};
    }

    live_stream::NetAddress UdpLocalAddress(
        live_stream::UdpSocketId) const override {
        return live_stream::NetAddress{"127.0.0.1", 3702};
    }

    live_stream::NetAddress UdpPeerAddress(
        live_stream::UdpSocketId) const override {
        return live_stream::NetAddress{"127.0.0.1", 40000};
    }

    uint32_t PendingBytes(live_stream::ConnectionId) const override {
        return 0;
    }

    live_stream::NetStats GetStats() const override {
        return live_stream::NetStats();
    }

    void FireAccept(live_stream::ConnectionId connection_id) {
        if (callbacks_.on_accept == nullptr) {
            return;
        }
        live_stream::NetAddress peer;
        peer.ip = "192.0.2.10";
        peer.port = 12345;
        callbacks_.on_accept(callbacks_.user, connection_id, peer);
    }

    void FireRead(live_stream::ConnectionId connection_id,
                  const std::string& data) {
        if (callbacks_.on_read == nullptr) {
            return;
        }
        callbacks_.on_read(callbacks_.user, connection_id,
                           reinterpret_cast<const uint8_t*>(data.data()),
                           data.size());
    }

    bool RunTimer(live_stream::event::TimerId id) {
        for (Timer& timer : timers) {
            if (timer.id == id && timer.task) {
                timer.task();
                return true;
            }
        }
        return false;
    }

    live_stream::TcpListenOptions listen_options;
    live_stream::TcpServerId last_closed_server = 0;
    live_stream::ConnectionId last_closed_connection = 0;
    std::vector<Timer> timers;
    std::vector<live_stream::event::TimerId> cancelled_timer_ids;
    uint32_t last_delay_ms = 0;
    int close_tcp_count = 0;
    int close_count = 0;
    int cancel_count = 0;

private:
    FakeLoop loop_;
    live_stream::TcpCallbacks callbacks_;
    live_stream::event::TimerId next_timer_id = 1;
};

bool ContainsTimerId(const std::vector<live_stream::event::TimerId>& values,
                     live_stream::event::TimerId expected) {
    for (live_stream::event::TimerId value : values) {
        if (value == expected) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    FakeNetEngine net_engine;
    FakeRequestHandler handler;
    live_stream::HttpOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;
    options.enable_static_files = false;
    options.request_timeout_ms = 25;
    options.connection_idle_timeout_ms = 50;
    options.stream_executor_worker_count = 1;
    options.control_executor_worker_count = 1;

    live_stream::HttpDependencies dependencies;
    dependencies.net_engine = &net_engine;
    dependencies.net_loop = net_engine.DefaultLoop();
    live_stream::HttpServer server(options, dependencies, &handler);
    if (!server.Start()) {
        return 1;
    }

    net_engine.FireAccept(100);
    if (net_engine.timers.size() != 1 ||
        net_engine.timers[0].id != 1 ||
        net_engine.last_delay_ms != options.request_timeout_ms) {
        return 2;
    }

    net_engine.FireRead(100, "GET /api/");
    if (net_engine.timers.size() != 2 ||
        !ContainsTimerId(net_engine.cancelled_timer_ids, 1)) {
        return 3;
    }

    server.Stop();
    if (net_engine.close_tcp_count != 1 ||
        net_engine.last_closed_server != 7 ||
        net_engine.close_count != 1 ||
        net_engine.last_closed_connection != 100 ||
        !ContainsTimerId(net_engine.cancelled_timer_ids, 2)) {
        return 4;
    }

    const int close_count_after_stop = net_engine.close_count;
    if (!net_engine.RunTimer(2)) {
        return 5;
    }
    if (net_engine.close_count != close_count_after_stop) {
        return 6;
    }
    return 0;
}

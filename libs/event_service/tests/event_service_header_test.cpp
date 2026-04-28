#include "event_service.h"

#include "infra/status.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>

namespace {

int HeaderAndLifecycleTest() {
    std::unique_ptr<live_stream::IEventService> service =
        live_stream::CreateEventService();
    if (!service) {
        return 1;
    }
    if (std::strcmp(service->Name(), "event_service") != 0) {
        return 2;
    }
    if (service->Init() != infra::Status::kOk) {
        return 3;
    }
    if (service->Start() != infra::Status::kOk) {
        return 4;
    }
    service->Stop();
    service->Stop();
    service->Deinit();
    return 0;
}

int PublishSubscribeTest() {
    std::unique_ptr<live_stream::IEventService> service =
        live_stream::CreateEventService();
    if (service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 10;
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool received = false;
    std::string message;
    std::string target;
    int32_t value = 0;

    infra::Result<live_stream::EventSubscriptionId> subscription =
        service->Subscribe(live_stream::EventType::kConfigChanged,
                           [&](const live_stream::Event& event) {
                               std::lock_guard<std::mutex> lock(mutex);
                               received = true;
                               message = event.message;
                               target = event.target;
                               value = event.value;
                               condition.notify_one();
                           });
    if (!subscription.IsOk()) {
        return 11;
    }

    live_stream::Event ignored;
    ignored.type = live_stream::EventType::kNetworkChanged;
    ignored.message = "ignored";
    if (service->Publish(ignored) != infra::Status::kOk) {
        return 12;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (condition.wait_for(lock, std::chrono::milliseconds(50),
                               [&]() { return received; })) {
            return 13;
        }
    }

    live_stream::Event event;
    event.type = live_stream::EventType::kConfigChanged;
    event.source = "unit_test";
    event.target = "video.main";
    event.message = "changed";
    event.value = 7;
    if (service->Publish(event) != infra::Status::kOk) {
        return 14;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(1),
                                [&]() { return received; })) {
            return 15;
        }
        if (message != "changed") {
            return 16;
        }
        if (target != "video.main" || value != 7) {
            return 23;
        }
    }

    received = false;
    if (service->Unsubscribe(subscription.value) != infra::Status::kOk) {
        return 17;
    }
    if (service->Publish(event) != infra::Status::kOk) {
        return 18;
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (condition.wait_for(lock, std::chrono::milliseconds(50),
                               [&]() { return received; })) {
            return 19;
        }
    }

    service->Stop();
    service->Deinit();
    return 0;
}

int SubscriptionLimitTest() {
    std::unique_ptr<live_stream::IEventService> service =
        live_stream::CreateEventService();
    if (service->Init() != infra::Status::kOk) {
        return 29;
    }

    for (int i = 0; i < 128; ++i) {
        infra::Result<live_stream::EventSubscriptionId> subscription =
            service->Subscribe(live_stream::EventType::kConfigChanged,
                               [](const live_stream::Event& event) {
                                   (void)event;
                               });
        if (!subscription.IsOk()) {
            return 30;
        }
    }

    infra::Result<live_stream::EventSubscriptionId> overflow =
        service->Subscribe(live_stream::EventType::kConfigChanged,
                           [](const live_stream::Event& event) {
                               (void)event;
                           });
    if (overflow.status != infra::Status::kBusy) {
        return 31;
    }
    return 0;
}

int EventSizeLimitTest() {
    std::unique_ptr<live_stream::IEventService> service =
        live_stream::CreateEventService();
    if (service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 40;
    }

    live_stream::Event event;
    event.type = live_stream::EventType::kConfigChanged;
    event.source = std::string(65, 's');
    if (service->Publish(event) != infra::Status::kInvalidParam) {
        return 41;
    }

    event.source.clear();
    event.target = std::string(129, 't');
    if (service->Publish(event) != infra::Status::kInvalidParam) {
        return 42;
    }

    event.target.clear();
    event.message = std::string(257, 'm');
    if (service->Publish(event) != infra::Status::kInvalidParam) {
        return 43;
    }
    return 0;
}

int ErrorPathTest() {
    std::unique_ptr<live_stream::IEventService> service =
        live_stream::CreateEventService();

    live_stream::Event event;
    event.type = live_stream::EventType::kConfigChanged;
    if (service->Publish(event) != infra::Status::kBusy) {
        return 20;
    }
    if (service->Subscribe(live_stream::EventType::kConfigChanged,
                           live_stream::EventHandler()).status !=
        infra::Status::kInvalidParam) {
        return 21;
    }
    if (service->Subscribe(live_stream::EventType::kConfigChanged,
                           [](const live_stream::Event& received_event) {
                               (void)received_event;
                           }).status != infra::Status::kBusy) {
        return 24;
    }
    if (service->Unsubscribe(100) != infra::Status::kNotFound) {
        return 22;
    }
    return 0;
}

}  // namespace

int main() {
    int result = HeaderAndLifecycleTest();
    if (result != 0) {
        return result;
    }
    result = PublishSubscribeTest();
    if (result != 0) {
        return result;
    }
    result = SubscriptionLimitTest();
    if (result != 0) {
        return result;
    }
    result = EventSizeLimitTest();
    if (result != 0) {
        return result;
    }
    return ErrorPathTest();
}

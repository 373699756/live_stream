#include "event.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace {

using live_stream::event::Dispatcher;
using live_stream::event::Event;
using live_stream::event::EventCounts;
using live_stream::event::EventStatus;
using live_stream::event::EventType;
using live_stream::event::Loop;
using live_stream::event::Service;
using live_stream::event::StopMode;
using live_stream::event::Subscription;

int HeaderAndLifecycleTest() {
    Service service;
    if (!service.Start()) {
        return 1;
    }
    service.Stop();
    service.Stop();

    Dispatcher dispatcher;
    if (dispatcher.GetCounts().subscriptions != 0) {
        return 2;
    }
    return 0;
}

int PublishSubscribeTest() {
    Dispatcher dispatcher;

    std::mutex mutex;
    std::condition_variable condition;
    bool received = false;
    std::string message;
    std::string target;
    int32_t value = 0;

    Subscription subscription = dispatcher.SubscribeMany(
        std::vector<EventType>{EventType::kConfigChanged, EventType::kAlarmOn},
        [&](const Event &event) {
            std::lock_guard<std::mutex> lock(mutex);
            received = true;
            message = event.message;
            target = event.target;
            value = event.value;
            condition.notify_one();
        });
    if (!subscription.valid()) {
        return 10;
    }

    Event ignored;
    ignored.type = EventType::kNetworkChanged;
    ignored.message = "ignored";
    if (dispatcher.Publish(ignored) != EventStatus::kOk) {
        return 11;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (condition.wait_for(lock, std::chrono::milliseconds(50),
                               [&]() { return received; })) {
            return 12;
        }
    }

    Event event;
    event.type = EventType::kConfigChanged;
    event.source = "unit_test";
    event.target = "video.main";
    event.message = "changed";
    event.value = 7;
    if (dispatcher.Publish(event) != EventStatus::kOk) {
        return 13;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(1),
                                [&]() { return received; })) {
            return 14;
        }
        if (message != "changed") {
            return 15;
        }
        if (target != "video.main" || value != 7) {
            return 16;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        received = false;
    }

    Event alarm_event;
    alarm_event.type = EventType::kAlarmOn;
    alarm_event.source = "alarm";
    alarm_event.target = "motion";
    alarm_event.message = "motion";
    if (dispatcher.Publish(alarm_event) != EventStatus::kOk) {
        return 17;
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(1),
                                [&]() { return received; })) {
            return 18;
        }
    }

    subscription.Cancel();
    {
        std::lock_guard<std::mutex> lock(mutex);
        received = false;
    }
    if (dispatcher.Publish(event) != EventStatus::kOk) {
        return 19;
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (condition.wait_for(lock, std::chrono::milliseconds(50),
                               [&]() { return received; })) {
            return 20;
        }
    }

    const EventCounts counts = dispatcher.GetCounts();
    if (counts.published < 4 || counts.handled != 2 ||
        counts.rejected != 0 || counts.subscriptions != 0) {
        return 21;
    }
    return 0;
}

int SubscriptionMoveTest() {
    Dispatcher dispatcher;
    int handled = 0;

    Subscription first = dispatcher.Subscribe(
        EventType::kConfigChanged,
        [&](const Event &event) {
            (void)event;
            ++handled;
        });
    if (!first.valid()) {
        return 30;
    }
    Subscription second = std::move(first);
    if (first.valid() || !second.valid()) {
        return 31;
    }

    Event event;
    event.type = EventType::kConfigChanged;
    if (dispatcher.Publish(event) != EventStatus::kOk || handled != 1) {
        return 32;
    }

    second = Subscription();
    if (second.valid()) {
        return 33;
    }
    if (dispatcher.Publish(event) != EventStatus::kOk || handled != 1) {
        return 34;
    }
    return 0;
}

int EventSizeLimitTest() {
    Dispatcher dispatcher;

    Event event;
    event.type = EventType::kConfigChanged;
    event.source = std::string(65, 's');
    if (dispatcher.Publish(event) != EventStatus::kInvalid) {
        return 40;
    }

    event.source.clear();
    event.target = std::string(129, 't');
    if (dispatcher.Publish(event) != EventStatus::kInvalid) {
        return 41;
    }

    event.target.clear();
    event.message = std::string(257, 'm');
    if (dispatcher.Publish(event) != EventStatus::kInvalid) {
        return 42;
    }

    event.message.clear();
    event.type = static_cast<EventType>(999);
    if (dispatcher.Publish(event) != EventStatus::kInvalid) {
        return 43;
    }

    const EventCounts counts = dispatcher.GetCounts();
    if (counts.rejected != 4 || counts.published != 0) {
        return 44;
    }
    return 0;
}

int ErrorPathTest() {
    Dispatcher dispatcher;

    if (dispatcher.SubscribeMany(std::vector<EventType>(),
                                 [](const Event &event) {
                                     (void)event;
                                 }).valid()) {
        return 50;
    }
    if (dispatcher.SubscribeMany(
            std::vector<EventType>{static_cast<EventType>(999)},
            [](const Event &event) { (void)event; }).valid()) {
        return 51;
    }
    if (dispatcher.Subscribe(EventType::kConfigChanged,
                             live_stream::event::EventFn()).valid()) {
        return 52;
    }
    if (dispatcher.Cancel(100)) {
        return 53;
    }

    Loop loop;
    Event event;
    event.type = EventType::kConfigChanged;
    if (dispatcher.Post(nullptr, event) != EventStatus::kInvalid) {
        return 54;
    }
    if (dispatcher.Post(&loop, event) != EventStatus::kNotStarted) {
        return 55;
    }
    return 0;
}

int AsyncPublishTest() {
    Service service;
    if (!service.Start()) {
        return 60;
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool received = false;
    int64_t timestamp_ms = 0;
    bool on_loop_thread = false;

    Subscription subscription = service.dispatcher()->Subscribe(
        EventType::kSystemStatusChanged,
        [&](const Event &event) {
            std::lock_guard<std::mutex> lock(mutex);
            received = true;
            timestamp_ms = event.timestamp_ms;
            on_loop_thread = service.loop()->IsCurrentThread();
            condition.notify_one();
        });
    if (!subscription.valid()) {
        service.Stop();
        return 61;
    }

    Event event;
    event.type = EventType::kSystemStatusChanged;
    event.source = "unit_test";
    event.message = "async";
    if (service.PublishAsync(event) != EventStatus::kOk) {
        service.Stop();
        return 62;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(1),
                                [&]() { return received; })) {
            service.Stop();
            return 63;
        }
        if (timestamp_ms == 0 || !on_loop_thread) {
            service.Stop();
            return 64;
        }
    }

    service.Stop(StopMode::kDrain);
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
    result = SubscriptionMoveTest();
    if (result != 0) {
        return result;
    }
    result = EventSizeLimitTest();
    if (result != 0) {
        return result;
    }
    result = ErrorPathTest();
    if (result != 0) {
        return result;
    }
    return AsyncPublishTest();
}

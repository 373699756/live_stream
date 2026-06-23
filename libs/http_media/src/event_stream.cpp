#include "event_stream.h"

#include "config_json.h"
#include "infra/time.h"

namespace live_stream {
namespace {

const char *EventTypeName(event::EventType type) {
    switch (type) {
        case event::EventType::kMediaStatusChanged:
            return "media_status_changed";
        case event::EventType::kStreamStarted:
            return "stream_started";
        case event::EventType::kStreamStopped:
            return "stream_stopped";
        case event::EventType::kNetPressureChanged:
            return "net_pressure_changed";
        case event::EventType::kAlarmOn:
            return "alarm_on";
        case event::EventType::kAlarmOff:
            return "alarm_off";
        default:
            return "event";
    }
}

ConfigJson BuildEventJson(const event::Event &event,
                          const char *event_type_name) {
    ConfigJson data = ConfigJson::object();
    data["type"] = event_type_name;
    data["source"] = event.source;
    data["target"] = event.target;
    data["message"] = event.message;
    data["value"] = event.value;
    data["timestamp_ms"] = event.timestamp_ms != 0
                               ? event.timestamp_ms
                               : infra::Time::SystemTimeMillis();
    data["level"] = event.level;
    return data;
}

}  // namespace

std::string BuildEventStreamMessage(const event::Event &event) {
    const char *event_type_name = EventTypeName(event.type);
    const ConfigJson data = BuildEventJson(event, event_type_name);
    const std::string data_text = data.dump();

    // SSE 每条消息用 event/data 双行格式；payload 交给 ConfigJson 序列化，
    // 避免手写 JSON 转义漏掉引号、换行或反斜杠。
    std::string message;
    message.reserve(data_text.size() + 32);
    message += "event: ";
    message += event_type_name;
    message += "\ndata: ";
    message += data_text;
    message += "\n\n";
    return message;
}

std::string BuildEventStreamHello() {
    event::Event hello;
    hello.type = event::EventType::kMediaStatusChanged;
    hello.source = "http";
    hello.target = "events";
    hello.message = "connected";
    return BuildEventStreamMessage(hello);
}

}  // namespace live_stream

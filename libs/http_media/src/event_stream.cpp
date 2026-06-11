#include "event_stream.h"

#include "config_json.h"
#include "infra/time.h"

namespace live_stream {
namespace {

const char *EventTypeName(EventType type) {
    switch (type) {
        case EventType::kMediaStatusChanged:
            return "media_status_changed";
        case EventType::kStreamStarted:
            return "stream_started";
        case EventType::kStreamStopped:
            return "stream_stopped";
        case EventType::kAlarmOn:
            return "alarm_on";
        case EventType::kAlarmOff:
            return "alarm_off";
        default:
            return "event";
    }
}

ConfigJson BuildEventJson(const Event &event, const char *event_type_name) {
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

std::string BuildEventStreamMessage(const Event &event) {
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
    Event event;
    event.type = EventType::kMediaStatusChanged;
    event.source = "http";
    event.target = "events";
    event.message = "connected";
    return BuildEventStreamMessage(event);
}

}  // namespace live_stream

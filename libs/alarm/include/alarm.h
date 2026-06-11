/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: alarm.h
 * Brief: Defines the IPC alarm public API.
 */

#ifndef LIVE_STREAM_ALARM_ALARM_H_
#define LIVE_STREAM_ALARM_ALARM_H_

#include "request_context.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfig;
class IEvent;
class ILogger;

enum class AlarmSource {
    kMotion,
    kAiDetection,
    kIoInput,
    kTamper,
    kNetwork,
};

struct AlarmRule {
    AlarmSource source = AlarmSource::kMotion;
    bool enabled = false;
    uint32_t min_duration_ms = 500;
    uint32_t repeat_interval_ms = 0;
    bool manual_clear = false;
    uint8_t level = 1;
};

struct AlarmInput {
    AlarmSource source = AlarmSource::kMotion;
    bool active = false;
    int32_t value = 0;
    std::string message;
};

struct AlarmSourceState {
    AlarmSource source = AlarmSource::kMotion;
    bool enabled = false;
    bool waiting = false;
    bool active = false;
    int64_t waiting_since_ms = 0;
    int64_t active_since_ms = 0;
    int64_t last_alarm_time_ms = 0;
    uint8_t level = 0;
    std::string message;
};

struct AlarmStatus {
    bool active = false;
    AlarmSource source = AlarmSource::kMotion;
    int64_t active_since_ms = 0;
    int64_t last_trigger_time_ms = 0;
    uint8_t level = 0;
    std::string message;
    std::vector<AlarmSourceState> sources;
};

struct AlarmOptions {
    IConfig* config = nullptr;
    IEvent* event = nullptr;
    ILogger* logger = nullptr;
    std::vector<AlarmRule> default_rules;
};

class IAlarm {
public:
    virtual ~IAlarm() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual AlarmStatus GetAlarmStatus() = 0;
    virtual bool UpdateRules(const live_stream::RequestContext& context,
                             const std::vector<AlarmRule>& rules) = 0;
    virtual bool EnableRule(const live_stream::RequestContext& context,
                            AlarmSource source,
                            bool enabled) = 0;
    virtual bool InjectAlarmInput(const AlarmInput& input) = 0;
    virtual bool ClearAlarm(const live_stream::RequestContext& context) = 0;
};

std::unique_ptr<IAlarm> CreateAlarm(
    const AlarmOptions& options);

const char* AlarmSourceToString(AlarmSource source);

}  // namespace live_stream

#endif  // LIVE_STREAM_ALARM_ALARM_H_

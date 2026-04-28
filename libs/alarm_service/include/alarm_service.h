/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: alarm_service.h
 * Brief: Defines the IPC alarm service public API.
 */

#ifndef LIVE_STREAM_ALARM_SERVICE_H_
#define LIVE_STREAM_ALARM_SERVICE_H_

#include "infra/status.h"
#include "infra/request_context.h"
#include "infra/service.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfigService;
class IEventService;
class ILoggerService;

enum class AlarmSource {
    kMotion,
    kIoInput,
    kTamper,
    kNetwork,
};

struct AlarmRule {
    AlarmSource source = AlarmSource::kMotion;
    bool enabled = false;
    uint32_t min_duration_ms = 500;
};

struct AlarmInput {
    AlarmSource source = AlarmSource::kMotion;
    bool active = false;
    int32_t value = 0;
    std::string message;
};

struct AlarmStatus {
    bool active = false;
    AlarmSource source = AlarmSource::kMotion;
    int64_t active_since_ms = 0;
    int64_t last_trigger_time_ms = 0;
    std::string message;
};

struct AlarmServiceOptions {
    IConfigService* config_service = nullptr;
    IEventService* event_service = nullptr;
    ILoggerService* logger_service = nullptr;
    std::vector<AlarmRule> default_rules;
};

class IAlarmService : public infra::IService {
 public:
    virtual infra::Result<AlarmStatus> GetAlarmStatus() = 0;
    virtual infra::Status UpdateRules(const infra::RequestContext& context,
                                     const std::vector<AlarmRule>& rules) = 0;
    virtual infra::Status EnableRule(const infra::RequestContext& context,
                                    AlarmSource source,
                                    bool enabled) = 0;
    virtual infra::Status InjectAlarmInput(const AlarmInput& input) = 0;
    virtual infra::Status ClearAlarm(const infra::RequestContext& context) = 0;
};

std::unique_ptr<IAlarmService> CreateAlarmService(
    const AlarmServiceOptions& options);

const char* AlarmSourceToString(AlarmSource source);

}  // namespace live_stream

#endif  // LIVE_STREAM_ALARM_SERVICE_H_

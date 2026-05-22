/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: time_service.h
 * Brief: Defines the IPC system time service public API.
 */

#ifndef LIVE_STREAM_TIME_SERVICE_H_
#define LIVE_STREAM_TIME_SERVICE_H_

#include "request_context.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfigService;
class IEventService;
class ILoggerService;

enum class TimeSyncSource {
    kManual,
    kOnvif,
    kNtp,
};

struct NtpConfig {
    bool enabled = true;
    std::vector<std::string> servers;
    uint32_t sync_interval_sec = 3600;
};

struct TimeStatus {
    int64_t system_time_ms = 0;
    std::string timezone = "UTC";
    NtpConfig ntp;
    TimeSyncSource last_sync_source = TimeSyncSource::kManual;
    int64_t last_sync_time_ms = 0;
    bool last_sync_ok = true;
};

class ITimePlatform {
public:
    virtual ~ITimePlatform() = default;

    virtual int64_t GetSystemTimeMs() = 0;
    virtual bool SetSystemTimeMs(int64_t unix_time_ms) = 0;
    virtual bool SyncNtp(const std::vector<std::string>& servers,
                         int64_t* synced_time_ms) = 0;
};

struct TimeServiceOptions {
    IConfigService* config_service = nullptr;
    IEventService* event_service = nullptr;
    ILoggerService* logger_service = nullptr;
    ITimePlatform* platform = nullptr;
    std::string default_timezone = "UTC";
    NtpConfig default_ntp_config;
};

class ITimeService {
public:
    virtual ~ITimeService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual TimeStatus GetTimeStatus() = 0;
    virtual bool SetTimezone(const live_stream::RequestContext& context,
                             const std::string& timezone) = 0;
    virtual bool SetSystemTime(const live_stream::RequestContext& context,
                               int64_t unix_time_ms,
                               TimeSyncSource source) = 0;
    virtual bool SyncNow(const live_stream::RequestContext& context,
                         TimeSyncSource source) = 0;
    virtual bool UpdateNtpConfig(const live_stream::RequestContext& context,
                                 const NtpConfig& config) = 0;
};

std::unique_ptr<ITimeService> CreateTimeService(
    const TimeServiceOptions& options);

const char* TimeSyncSourceToString(TimeSyncSource source);

}  // namespace live_stream

#endif  // LIVE_STREAM_TIME_SERVICE_H_

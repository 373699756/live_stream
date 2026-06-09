/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: time_api.h
 * Brief: Defines the IPC system time public API.
 */

#ifndef LIVE_STREAM_TIME_TIME_API_H_
#define LIVE_STREAM_TIME_TIME_API_H_

#include "request_context.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfig;
class IEvent;
class ILogger;

enum class TimeSyncSource {
    kManual,
    kOnvif,
    kNtp,
    kBrowser,
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
    bool manual_sync_allowed = true;
    bool browser_sync_on_login = true;
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

struct TimeOptions {
    IConfig* config = nullptr;
    IEvent* event = nullptr;
    ILogger* logger = nullptr;
    ITimePlatform* platform = nullptr;
    std::string default_timezone = "UTC";
    NtpConfig default_ntp_config;
};

class ITime {
public:
    virtual ~ITime() = default;

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
    virtual bool UpdateBrowserSyncConfig(
        const live_stream::RequestContext& context,
        bool manual_sync_allowed,
        bool browser_sync_on_login) = 0;
};

std::unique_ptr<ITime> CreateTime(
    const TimeOptions& options);

const char* TimeSyncSourceToString(TimeSyncSource source);

}  // namespace live_stream

#endif  // LIVE_STREAM_TIME_TIME_API_H_

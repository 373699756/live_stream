/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: system_service.h
 * Brief: Defines the IPC system management service public API.
 */

#ifndef LIVE_STREAM_SYSTEM_SERVICE_H_
#define LIVE_STREAM_SYSTEM_SERVICE_H_

#include "live_stream/request_context.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfigService;
class IEventService;
class ILoggerService;

struct DeviceInfo {
    std::string model;
    std::string serial_number;
    std::string firmware_version;
};

struct SystemStatus {
    uint32_t cpu_usage_percent = 0;
    uint32_t memory_usage_percent = 0;
    int32_t temperature_celsius = 0;
    int64_t uptime_ms = 0;
    bool healthy = true;
    std::string health_reason;
};

struct SystemCapabilities {
    bool supports_reboot = true;
    bool supports_factory_reset = true;
    std::vector<std::string> features;
};

class ISystemPlatform {
public:
    virtual ~ISystemPlatform() = default;

    virtual DeviceInfo GetDeviceInfo() = 0;
    virtual SystemStatus GetSystemStatus() = 0;
    virtual SystemCapabilities GetCapabilities() = 0;
    virtual bool Reboot() = 0;
    virtual bool FactoryReset() = 0;
};

struct SystemServiceOptions {
    IConfigService* config_service = nullptr;
    IEventService* event_service = nullptr;
    ILoggerService* logger_service = nullptr;
    ISystemPlatform* platform = nullptr;
    uint32_t heartbeat_timeout_ms = 5000;
};

class ISystemService {
public:
    virtual ~ISystemService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual DeviceInfo GetDeviceInfo() = 0;
    virtual SystemStatus GetSystemStatus() = 0;
    virtual SystemCapabilities GetCapabilities() = 0;
    virtual bool Reboot(const live_stream::RequestContext& context) = 0;
    virtual bool FactoryReset(const live_stream::RequestContext& context) = 0;
    virtual bool ReportHeartbeat(const std::string& component) = 0;
};

std::unique_ptr<ISystemService> CreateSystemService(
    const SystemServiceOptions& options);

}  // namespace live_stream

#endif  // LIVE_STREAM_SYSTEM_SERVICE_H_

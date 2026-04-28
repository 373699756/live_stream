/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: onvif_service.h
 * Brief: Defines the ONVIF service public API.
 */

#ifndef LIVE_STREAM_ONVIF_SERVICE_H_
#define LIVE_STREAM_ONVIF_SERVICE_H_

#include "infra/status.h"
#include "infra/service.h"
#include "infra/stream_types.h"

#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {

class IAuthService;
class IConfigService;
class IEventService;
class NetEngine;
class ISystemService;
class ITimeService;

struct OnvifServiceOptions {
    std::string listen_ip = "0.0.0.0";
    std::string advertise_ip;
    uint16_t device_service_port = 8000;
    uint16_t discovery_port = 3702;
    bool discovery_enabled = true;
    bool enable_auth = false;
    std::string manufacturer = "CBinary";
    std::string model = "live_stream_ipc";
    std::string firmware_version = "0.1.0";
    std::string service_path = "/onvif/device_service";
    uint32_t max_request_bytes = 16 * 1024;
};

struct OnvifServiceStats {
    uint64_t discovery_requests = 0;
    uint64_t soap_requests = 0;
    uint64_t auth_failures = 0;
    uint64_t parse_failures = 0;
    uint64_t stream_uri_requests = 0;
    uint64_t snapshot_uri_requests = 0;
};

class IOnvifUriProvider {
 public:
    virtual ~IOnvifUriProvider() = default;

    virtual infra::Result<std::string> GetStreamUri(
        infra::StreamId stream_id) = 0;
    virtual infra::Result<std::string> GetSnapshotUri(
        infra::StreamId stream_id) = 0;
};

struct OnvifServiceDependencies {
    NetEngine* net_engine = nullptr;
    IAuthService* auth_service = nullptr;
    IEventService* event_service = nullptr;
    IConfigService* config_service = nullptr;
    ISystemService* system_service = nullptr;
    ITimeService* time_service = nullptr;
    IOnvifUriProvider* uri_provider = nullptr;
};

class IOnvifService : public infra::IService {
 public:
    ~IOnvifService() override = default;

    virtual OnvifServiceStats GetStats() const = 0;
};

std::unique_ptr<IOnvifService> CreateOnvifService(
    const OnvifServiceOptions& options,
    const OnvifServiceDependencies& dependencies);

class OnvifService {
 public:
    static const char* Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_H_

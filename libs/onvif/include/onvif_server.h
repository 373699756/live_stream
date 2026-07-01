/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: onvif_server.h
 * Brief: Defines the ONVIF server public API.
 */

#ifndef LIVE_STREAM_ONVIF_ONVIF_SERVER_H_
#define LIVE_STREAM_ONVIF_ONVIF_SERVER_H_

#include "event.h"

#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {

#ifndef LIVE_STREAM_RELEASE_VERSION
#define LIVE_STREAM_RELEASE_VERSION "0.1.0"
#endif

class DeviceMedia;
class ISystem;
class ITime;

struct OnvifServerOptions {
    std::string listen_ip = "0.0.0.0";
    std::string advertise_ip;
    std::string endpoint_uuid;
    uint16_t device_service_port = 8000;
    uint16_t discovery_port = 3702;
    bool discovery_enabled = true;
    bool enable_auth = false;
    std::string manufacturer = "CBinary";
    std::string model = "live_stream_ipc";
    std::string firmware_version = LIVE_STREAM_RELEASE_VERSION;
    std::string service_path = "/onvif/device_service";
    uint16_t http_port = 80;
    uint32_t max_request_bytes = 16 * 1024;
};

struct OnvifServerStats {
    uint64_t discovery_requests = 0;
    uint64_t soap_requests = 0;
    uint64_t auth_failures = 0;
    uint64_t parse_failures = 0;
    uint64_t stream_uri_requests = 0;
    uint64_t snapshot_uri_requests = 0;
};

class IOnvifReader {
public:
    virtual ~IOnvifReader() = default;

    virtual bool IsStarted() const = 0;
    virtual OnvifServerStats GetStats() const = 0;
};

class OnvifServer : public IOnvifReader {
public:
    OnvifServer(const OnvifServerOptions &options,
                event::Loop *socket_loop,
                ISystem *system,
                ITime *time,
                DeviceMedia *device);
    ~OnvifServer();

    OnvifServer(const OnvifServer &) = delete;
    OnvifServer &operator=(const OnvifServer &) = delete;

    bool Start();
    void Stop();
    bool ApplyOptions(const OnvifServerOptions &options);
    bool IsStarted() const override;
    OnvifServerStats GetStats() const override;

    static const char *Name();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<OnvifServer> CreateOnvifServer(
    const OnvifServerOptions &options,
    event::Loop *socket_loop,
    ISystem *system,
    ITime *time,
    DeviceMedia *device);

}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_ONVIF_SERVER_H_

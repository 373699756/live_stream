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

class IAuth;
class DeviceMedia;
class IRtsp;
class INetIo;
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
    std::string firmware_version = "0.1.0";
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

struct OnvifServerDependencies {
    INetIo *net_io = nullptr;
    event::Loop *net_loop = nullptr;
    IAuth *auth = nullptr;
    event::Dispatcher *event = nullptr;
    ISystem *system = nullptr;
    ITime *time = nullptr;
    DeviceMedia *device = nullptr;
    IRtsp *rtsp = nullptr;
};

class OnvifServer {
public:
    OnvifServer(const OnvifServerOptions &options,
                const OnvifServerDependencies &dependencies);
    ~OnvifServer();

    OnvifServer(const OnvifServer &) = delete;
    OnvifServer &operator=(const OnvifServer &) = delete;

    bool Start();
    void Stop();
    bool ApplyOptions(const OnvifServerOptions &options);
    bool IsStarted() const;
    OnvifServerStats GetStats() const;

    static const char *Name();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<OnvifServer> CreateOnvifServer(
    const OnvifServerOptions &options,
    const OnvifServerDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_ONVIF_SERVER_H_

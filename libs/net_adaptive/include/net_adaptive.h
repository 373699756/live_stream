#ifndef LIVE_STREAM_NET_ADAPTIVE_NET_ADAPTIVE_H_
#define LIVE_STREAM_NET_ADAPTIVE_NET_ADAPTIVE_H_

#include "media/stream_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IMediaSource;
class IRtsp;
class IWebrtc;
class NetEngine;

enum class NetAdaptivePressureLevel {
    kNormal = 0,
    kWatch,
    kConstrained,
};

enum class NetAdaptiveRecommendationType {
    kNone = 0,
    kRequestKeyFrame,
    kPreferSubStream,
    kCloseSlowClient,
};

struct NetAdaptiveOptions {
    bool enabled = true;
    uint32_t sample_interval_ms = 1000;
    uint32_t pending_bytes_watch = 256 * 1024;
    uint32_t pending_bytes_constrained = 768 * 1024;
};

struct NetAdaptiveDependencies {
    NetEngine *net_engine = nullptr;
    IRtsp *rtsp = nullptr;
    IWebrtc *webrtc = nullptr;
    IMediaSource *media_source = nullptr;
};

struct NetAdaptiveRecommendation {
    NetAdaptiveRecommendationType type = NetAdaptiveRecommendationType::kNone;
    NetAdaptivePressureLevel level = NetAdaptivePressureLevel::kNormal;
    std::string protocol;
    std::string target;
    StreamId stream_id = StreamId::kMain;
    std::string reason;
};

struct NetAdaptiveStats {
    bool enabled = false;
    NetAdaptivePressureLevel level = NetAdaptivePressureLevel::kNormal;
    uint32_t sampled_connections = 0;
    uint32_t constrained_connections = 0;
    uint32_t active_rtsp_sessions = 0;
    uint32_t active_webrtc_peers = 0;
    uint32_t slow_media_readers = 0;
    uint64_t samples = 0;
};

class INetAdaptive {
public:
    virtual ~INetAdaptive() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual NetAdaptiveStats GetStats() const = 0;
    virtual std::vector<NetAdaptiveRecommendation>
    GetRecommendations() const = 0;
};

std::unique_ptr<INetAdaptive> CreateNetAdaptive(
    const NetAdaptiveOptions &options,
    const NetAdaptiveDependencies &dependencies);

class NetAdaptive {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_NET_ADAPTIVE_NET_ADAPTIVE_H_

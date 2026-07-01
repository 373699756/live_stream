#include "handlers/media_preview_response.h"

#include "http_protocol.h"
#include "http_stream_id_json.h"

#include "config.h"
#include "json_reader.h"
#include "rtsp.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace {

std::string HostWithoutPort(const std::string &host_header) {
    if (host_header.empty()) {
        return std::string();
    }
    if (host_header[0] == '[') {
        const size_t close = host_header.find(']');
        return close == std::string::npos
                   ? host_header
                   : host_header.substr(1, close - 1);
    }
    const size_t colon = host_header.find(':');
    return colon == std::string::npos ? host_header
                                      : host_header.substr(0, colon);
}

std::string AdvertiseHostFromConfig(IConfig *config) {
    if (config == nullptr) {
        return std::string();
    }
    Json network = config->Get("network");
    std::string advertise_host;
    if (network.is_object() &&
        json_reader::ReadField(network, "advertise_ip", &advertise_host)) {
        return advertise_host;
    }
    return std::string();
}

uint16_t RtspPortFromConfig(IConfig *config, uint16_t fallback) {
    if (config == nullptr) {
        return fallback;
    }
    Json rtsp = config->Get("rtsp");
    int64_t port = 0;
    if (rtsp.is_object() &&
        json_reader::ReadField(rtsp, "port", &port, 1, 65535)) {
        return static_cast<uint16_t>(port);
    }
    Json network = config->Get("network");
    if (network.is_object() && network.contains("ports") &&
        network.at("ports").is_object() &&
        json_reader::ReadField(network.at("ports"), "rtsp", &port, 1,
                               65535)) {
        return static_cast<uint16_t>(port);
    }
    return fallback;
}

std::string BuildRtspUrl(IConfig *config,
                         IRtspSessionReader *rtsp_reader,
                         const HttpRequest &request,
                         StreamId stream_id) {
    if (rtsp_reader == nullptr) {
        return std::string();
    }
    RtspListenAddress address = rtsp_reader->LocalAddress();
    address.port = RtspPortFromConfig(config, address.port);
    if (address.port == 0) {
        return std::string();
    }
    std::string host = HostWithoutPort(GetHeader(request, "Host"));
    if (host.empty()) {
        host = AdvertiseHostFromConfig(config);
    }
    if (host.empty() || host == "0.0.0.0") {
        host = address.ip;
    }
    return BuildRtspStreamUrl(address, stream_id, host);
}

}  // namespace

Json BuildMediaPreviewResponse(IConfig *config,
                               IRtspSessionReader *rtsp_reader,
                               const HttpRequest &request,
                               StreamId stream_id) {
    const std::string stream = StreamIdToJsonString(stream_id);
    Json root = Json::object();
    root["stream"] = stream;
    root["hls"] = "/live/" + stream + "/hls/index.m3u8";
    root["http_flv"] = "/live/" + stream + ".live.flv";
    root["mjpeg"] = "/live/" + stream + ".mjpg";
    root["snapshot"] = "/snapshot/" + stream + ".jpg";
    root["rtsp"] = BuildRtspUrl(config, rtsp_reader, request, stream_id);
    root["webrtc_whep"] = "/live/" + stream + "/whep";
    return root;
}

}  // namespace live_stream

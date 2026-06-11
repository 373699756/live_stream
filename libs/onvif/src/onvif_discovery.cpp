#include "onvif_discovery.h"

#include "onvif_soap.h"
#include "onvif_types.h"
#include "system.h"

#include <cstdio>

namespace live_stream {
namespace onvif {
namespace {

uint64_t Fnv1a64(const std::string &value, uint64_t seed) {
    uint64_t hash = seed;
    for (char c : value) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string Hex64(uint64_t value) {
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string(buffer);
}

std::string FormatUuid(uint64_t high, uint64_t low) {
    const std::string left = Hex64(high);
    const std::string right = Hex64(low);
    return left.substr(0, 8) + "-" + left.substr(8, 4) + "-" +
           left.substr(12, 4) + "-" + right.substr(0, 4) + "-" +
           right.substr(4, 12);
}

std::string NormalizeEndpointUuid(const std::string &endpoint_uuid) {
    if (endpoint_uuid.empty()) {
        return "";
    }
    if (endpoint_uuid.find("urn:uuid:") == 0) {
        return endpoint_uuid;
    }
    if (endpoint_uuid.find("uuid:") == 0) {
        return "urn:" + endpoint_uuid;
    }
    return "urn:uuid:" + endpoint_uuid;
}

std::string BuildDefaultEndpointUuid(const OnvifServerOptions &options,
                                     ISystem *system) {
    // 未配置 endpoint_uuid 时用设备静态信息生成稳定 UUID。它不是随机值，
    // 否则 NVR 每次发现都会把同一台设备当成新设备。
    std::string seed = options.manufacturer + ":" + options.model + ":" +
                       options.firmware_version;
    if (system != nullptr) {
        const DeviceInfo info = system->GetDeviceInfo();
        if (!info.serial_number.empty()) {
            seed += ":" + info.serial_number;
        }
        if (!info.model.empty()) {
            seed += ":" + info.model;
        }
    }
    const uint64_t high = Fnv1a64(seed, 1469598103934665603ULL);
    const uint64_t low = Fnv1a64(seed, high ^ 0x9e3779b97f4a7c15ULL);
    return "urn:uuid:" + FormatUuid(high, low);
}

std::string EndpointUuid(const OnvifServerOptions &options,
                         ISystem *system) {
    const std::string configured = NormalizeEndpointUuid(options.endpoint_uuid);
    if (!configured.empty()) {
        return configured;
    }
    return BuildDefaultEndpointUuid(options, system);
}

std::string ScopeValue(std::string value) {
    if (value.empty()) {
        return "unknown";
    }
    for (char &c : value) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/') {
            c = '_';
        }
    }
    return value;
}

std::string BuildScopes(const OnvifServerOptions &options,
                        ISystem *system) {
    DeviceInfo info;
    info.model = options.model;
    info.firmware_version = options.firmware_version;
    if (system != nullptr) {
        const DeviceInfo service_info = system->GetDeviceInfo();
        if (!service_info.model.empty()) {
            info = service_info;
        }
    }

    // Scopes 是 NVR 做设备筛选的主要字段，只暴露设备类型、硬件名和 Profile，
    // 不把内部模块名或 Web API 路径带到 ONVIF 协议里。
    return "onvif://www.onvif.org/type/video_encoder "
           "onvif://www.onvif.org/hardware/" +
           XmlEscape(ScopeValue(info.model)) +
           " onvif://www.onvif.org/name/" +
           XmlEscape(ScopeValue(options.manufacturer + "_" + info.model)) +
           " onvif://www.onvif.org/Profile/Streaming";
}

std::string DeviceServiceUrl(const OnvifServerOptions &options,
                             const std::string &advertise_ip) {
    return "http://" + advertise_ip + ":" +
           std::to_string(options.device_service_port) + options.service_path;
}

std::string DiscoveryHeader(const std::string &request) {
    std::string relates_to;
    static_cast<void>(ExtractXmlTagText(request, "MessageID", &relates_to));
    // RelatesTo 回填客户端 Probe 的 MessageID；部分 NVR 依赖它把异步 UDP 响应
    // 和自己的发现请求对应起来。
    std::string header =
        "<s:Header>"
        "<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/"
        "ProbeMatches</a:Action>"
        "<a:MessageID>urn:uuid:live-stream-probe-response</a:MessageID>";
    if (!relates_to.empty()) {
        header += "<a:RelatesTo>" + XmlEscape(relates_to) + "</a:RelatesTo>";
    }
    header +=
        "<a:To>http://www.w3.org/2005/08/addressing/anonymous</a:To>"
        "</s:Header>";
    return header;
}

std::string DiscoveryEnvelope(const std::string &header,
                              const std::string &body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\""
           " xmlns:a=\"http://www.w3.org/2005/08/addressing\""
           " xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\""
           " xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\""
           " xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\""
           " xmlns:tt=\"http://www.onvif.org/ver10/schema\">" +
           header + "<s:Body>" + body + "</s:Body></s:Envelope>";
}

}  // namespace

bool IsOnvifProbeRequest(const std::string &request) {
    return Contains(request, "Probe");
}

std::string BuildDiscoveryProbeMatches(
    const OnvifServerOptions &options,
    ISystem *system,
    const std::string &advertise_ip,
    const std::string &request) {
    // WS-Discovery 只返回 device service 的 XAddr。后续 media/device SOAP
    // 都通过这个 HTTP 入口继续协商。
    const std::string endpoint_uuid = EndpointUuid(options, system);
    const std::string service_url = DeviceServiceUrl(options, advertise_ip);
    const std::string body =
        "<d:ProbeMatches>"
        "<d:ProbeMatch>"
        "<a:EndpointReference><a:Address>" +
        XmlEscape(endpoint_uuid) +
        "</a:Address></a:EndpointReference>"
        "<d:Types>dn:NetworkVideoTransmitter tds:Device</d:Types>"
        "<d:Scopes>" +
        BuildScopes(options, system) +
        "</d:Scopes>"
        "<d:XAddrs>" +
        XmlEscape(service_url) +
        "</d:XAddrs>"
        "<d:MetadataVersion>1</d:MetadataVersion>"
        "</d:ProbeMatch>"
        "</d:ProbeMatches>";
    return DiscoveryEnvelope(DiscoveryHeader(request), body);
}

}  // namespace onvif
}  // namespace live_stream

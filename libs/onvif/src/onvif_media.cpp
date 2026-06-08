#include "onvif_media.h"

#include "device_media.h"
#include "onvif_soap.h"
#include "rtsp.h"

namespace live_stream {
namespace onvif {
namespace {

std::string SnapshotPath(StreamId stream_id) {
    return std::string("/snapshot/") + StreamToken(stream_id) + ".jpg";
}

bool StreamAvailable(IDeviceMedia *device_media, StreamId stream_id) {
    return device_media == nullptr ||
           device_media->IsStreamStarted(stream_id);
}

std::string BuildStreamUri(IRtsp *rtsp,
                           StreamId stream_id,
                           const std::string &advertise_ip) {
    if (rtsp == nullptr) {
        return std::string();
    }
    // RTSP URL 规则归 rtsp 模块所有；ONVIF 只提供 advertise_ip，
    // 不复制 /live/main 或端口拼接规则。
    const RtspListenAddress address = rtsp->LocalAddress();
    return BuildRtspStreamUrl(address, stream_id, advertise_ip);
}

std::string BuildSnapshotUri(const OnvifServerOptions &options,
                             StreamId stream_id,
                             const std::string &advertise_ip) {
    return std::string("http://") + advertise_ip + ":" +
           std::to_string(options.http_port) + SnapshotPath(stream_id);
}

std::string UnavailableUriFault(uint32_t *status, std::string *reason) {
    if (status != nullptr) {
        *status = 500;
    }
    if (reason != nullptr) {
        *reason = "Internal Server Status";
    }
    return BuildSoapFaultBody("uri unavailable");
}

void AppendProfile(std::string *body,
                   StreamId stream_id,
                   const char *name) {
    *body += "<trt:Profiles token=\"";
    *body += StreamToken(stream_id);
    *body += "\"><tt:Name>";
    *body += name;
    *body += "</tt:Name></trt:Profiles>";
}

const std::string &StreamUriForId(const OnvifMediaUris &media_uris,
                                  StreamId stream_id) {
    return stream_id == StreamId::kSub ? media_uris.stream_sub
                                       : media_uris.stream_main;
}

const std::string &SnapshotUriForId(const OnvifMediaUris &media_uris,
                                    StreamId stream_id) {
    return stream_id == StreamId::kSub ? media_uris.snapshot_sub
                                       : media_uris.snapshot_main;
}

}  // namespace

OnvifMediaUris BuildOnvifMediaUris(const OnvifServerOptions &options,
                                   IDeviceMedia *device_media,
                                   IRtsp *rtsp,
                                   const std::string &advertise_ip) {
    OnvifMediaUris media_uris;
    // 只有设备侧认为 stream 已启动时才发布 RTSP URI；snapshot URI 使用 HTTP
    // 固定契约生成，不代表 ONVIF 拥有 HTTP handler 状态。
    if (StreamAvailable(device_media, StreamId::kMain)) {
        media_uris.stream_main =
            BuildStreamUri(rtsp, StreamId::kMain, advertise_ip);
    }
    if (StreamAvailable(device_media, StreamId::kSub)) {
        media_uris.stream_sub =
            BuildStreamUri(rtsp, StreamId::kSub, advertise_ip);
    }
    media_uris.snapshot_main =
        BuildSnapshotUri(options, StreamId::kMain, advertise_ip);
    media_uris.snapshot_sub =
        BuildSnapshotUri(options, StreamId::kSub, advertise_ip);
    return media_uris;
}

bool ParseProfileToken(const std::string &body, StreamId *stream_id) {
    if (stream_id == nullptr) {
        return false;
    }
    std::string token;
    if (!ExtractXmlTagText(body, "ProfileToken", &token)) {
        // 部分 NVR 的 GetStreamUri 不带 ProfileToken，按 ONVIF 互通习惯默认主码流。
        *stream_id = StreamId::kMain;
        return true;
    }
    token = ToLower(token);
    if (token == "sub" || token == "profile_sub") {
        *stream_id = StreamId::kSub;
        return true;
    }
    if (token == "main" || token == "profile_main") {
        *stream_id = StreamId::kMain;
        return true;
    }
    return false;
}

std::string BuildProfilesBody(const OnvifMediaUris &media_uris) {
    std::string body = "<trt:GetProfilesResponse>";
    if (!media_uris.stream_main.empty()) {
        AppendProfile(&body, StreamId::kMain, "MainStream");
    }
    if (!media_uris.stream_sub.empty()) {
        AppendProfile(&body, StreamId::kSub, "SubStream");
    }
    body += "</trt:GetProfilesResponse>";
    return body;
}

std::string BuildProfileFaultBody(uint32_t *status, std::string *reason) {
    if (status != nullptr) {
        *status = 400;
    }
    if (reason != nullptr) {
        *reason = "Bad Request";
    }
    return BuildSoapFaultBody("unknown profile token");
}

OnvifBody BuildStreamUriBody(const OnvifMediaUris &media_uris,
                             StreamId stream_id,
                             uint32_t *status,
                             std::string *reason) {
    const std::string &uri = StreamUriForId(media_uris, stream_id);
    if (uri.empty()) {
        return OnvifBody{UnavailableUriFault(status, reason), false};
    }
    return OnvifBody{
        "<trt:GetStreamUriResponse><trt:MediaUri><tt:Uri>" +
            XmlEscape(uri) +
            "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
            StreamToken(stream_id) +
            "</trt:ProfileToken></trt:GetStreamUriResponse>",
        true};
}

OnvifBody BuildSnapshotUriBody(const OnvifMediaUris &media_uris,
                               StreamId stream_id,
                               uint32_t *status,
                               std::string *reason) {
    const std::string &uri = SnapshotUriForId(media_uris, stream_id);
    if (uri.empty()) {
        return OnvifBody{UnavailableUriFault(status, reason), false};
    }
    return OnvifBody{
        "<trt:GetSnapshotUriResponse><trt:MediaUri><tt:Uri>" +
            XmlEscape(uri) +
            "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
            StreamToken(stream_id) +
            "</trt:ProfileToken></trt:GetSnapshotUriResponse>",
        true};
}

}  // namespace onvif
}  // namespace live_stream

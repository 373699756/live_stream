#include "onvif_media.h"

#include "device_media.h"
#include "onvif_soap.h"
#include "rtsp.h"

namespace live_stream {
namespace onvif {
namespace {

const std::string &SnapshotPath(const OnvifServerOptions &options,
                                StreamId stream_id) {
    return stream_id == StreamId::kSub ? options.snapshot_sub_path
                                       : options.snapshot_main_path;
}

bool StreamAvailable(const OnvifServerDependencies &dependencies,
                     StreamId stream_id) {
    return dependencies.device_media == nullptr ||
           dependencies.device_media->IsStreamStarted(stream_id);
}

std::string BuildStreamUri(const OnvifServerDependencies &dependencies,
                           StreamId stream_id,
                           const std::string &advertise_ip) {
    if (dependencies.rtsp == nullptr) {
        return std::string();
    }
    const RtspListenAddress address = dependencies.rtsp->LocalAddress();
    return BuildRtspStreamUrl(address, stream_id, advertise_ip);
}

std::string BuildSnapshotUri(const OnvifServerOptions &options,
                             StreamId stream_id,
                             const std::string &advertise_ip) {
    return std::string("http://") + advertise_ip + ":" +
           std::to_string(options.http_port) + SnapshotPath(options, stream_id);
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
                                   const OnvifServerDependencies &dependencies,
                                   const std::string &advertise_ip) {
    OnvifMediaUris media_uris;
    if (StreamAvailable(dependencies, StreamId::kMain)) {
        media_uris.stream_main =
            BuildStreamUri(dependencies, StreamId::kMain, advertise_ip);
    }
    if (StreamAvailable(dependencies, StreamId::kSub)) {
        media_uris.stream_sub =
            BuildStreamUri(dependencies, StreamId::kSub, advertise_ip);
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

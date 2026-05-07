#include "onvif_media.h"

#include "onvif_soap.h"
#include "onvif_types.h"

namespace live_stream {
namespace onvif_internal {
namespace {

std::string UriFault(uint32_t* status, std::string* reason) {
    if (status != nullptr) {
        *status = 500;
    }
    if (reason != nullptr) {
        *reason = "Internal Server Status";
    }
    return SoapFault("uri unavailable");
}

}  // namespace

bool ParseStreamId(const std::string& body, StreamId* stream_id) {
    if (stream_id == nullptr) {
        return false;
    }
    const std::string begin_tag = "<ProfileToken>";
    const std::string end_tag = "</ProfileToken>";
    const std::size_t begin = body.find(begin_tag);
    if (begin != std::string::npos) {
        const std::size_t token_begin = begin + begin_tag.size();
        const std::size_t end = body.find(end_tag, token_begin);
        if (end != std::string::npos) {
            const std::string token =
                ToLower(body.substr(token_begin, end - token_begin));
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
        return false;
    }
    *stream_id = StreamId::kMain;
    return true;
}

void AppendProfile(std::string* body,
                   StreamId stream_id,
                   const char* name) {
    *body += "<trt:Profiles token=\"";
    *body += StreamToken(stream_id);
    *body += "\"><tt:Name>";
    *body += name;
    *body += "</tt:Name></trt:Profiles>";
}

bool ProfileAvailable(IOnvifUriProvider* uri_provider,
                      StreamId stream_id) {
    if (uri_provider == nullptr) {
        return true;
    }
    return !uri_provider->GetStreamUri(stream_id).empty();
}

std::string ProfilesBody(IOnvifUriProvider* uri_provider) {
    std::string body = "<trt:GetProfilesResponse>";
    if (ProfileAvailable(uri_provider, StreamId::kMain)) {
        AppendProfile(&body, StreamId::kMain, "MainStream");
    }
    if (ProfileAvailable(uri_provider, StreamId::kSub)) {
        AppendProfile(&body, StreamId::kSub, "SubStream");
    }
    body += "</trt:GetProfilesResponse>";
    return body;
}

std::string ProfileFault(uint32_t* status, std::string* reason) {
    if (status != nullptr) {
        *status = 400;
    }
    if (reason != nullptr) {
        *reason = "Bad Request";
    }
    return SoapFault("unknown profile token");
}

OnvifBodyResult StreamUriBody(IOnvifUriProvider* uri_provider,
                              StreamId stream_id,
                              uint32_t* status,
                              std::string* reason) {
    if (uri_provider == nullptr) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    const std::string uri = uri_provider->GetStreamUri(stream_id);
    if (uri.empty()) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    return OnvifBodyResult{
        "<trt:GetStreamUriResponse><trt:MediaUri><tt:Uri>" +
            XmlEscape(uri) +
            "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
            StreamToken(stream_id) +
            "</trt:ProfileToken></trt:GetStreamUriResponse>",
        true};
}

OnvifBodyResult SnapshotUriBody(IOnvifUriProvider* uri_provider,
                                StreamId stream_id,
                                uint32_t* status,
                                std::string* reason) {
    if (uri_provider == nullptr) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    const std::string uri = uri_provider->GetSnapshotUri(stream_id);
    if (uri.empty()) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    return OnvifBodyResult{
        "<trt:GetSnapshotUriResponse><trt:MediaUri><tt:Uri>" +
            XmlEscape(uri) +
            "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
            StreamToken(stream_id) +
            "</trt:ProfileToken></trt:GetSnapshotUriResponse>",
        true};
}

}  // namespace onvif_internal
}  // namespace live_stream

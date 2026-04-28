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

infra::Result<infra::StreamId> ParseStreamId(const std::string& body) {
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
                return infra::Result<infra::StreamId>::Ok(
                    infra::StreamId::kSub);
            }
            if (token == "main" || token == "profile_main") {
                return infra::Result<infra::StreamId>::Ok(
                    infra::StreamId::kMain);
            }
            return infra::Result<infra::StreamId>::Fail(
                infra::Status::kInvalidParam);
        }
        return infra::Result<infra::StreamId>::Fail(
            infra::Status::kInvalidParam);
    }
    return infra::Result<infra::StreamId>::Ok(infra::StreamId::kMain);
}

std::string ProfilesBody() {
    return "<trt:GetProfilesResponse>"
           "<trt:Profiles token=\"profile_main\"><tt:Name>MainStream"
           "</tt:Name></trt:Profiles>"
           "<trt:Profiles token=\"profile_sub\"><tt:Name>SubStream"
           "</tt:Name></trt:Profiles></trt:GetProfilesResponse>";
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
                              infra::StreamId stream_id,
                              uint32_t* status,
                              std::string* reason) {
    if (uri_provider == nullptr) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    infra::Result<std::string> uri = uri_provider->GetStreamUri(stream_id);
    if (!uri.IsOk()) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    return OnvifBodyResult{
        "<trt:GetStreamUriResponse><trt:MediaUri><tt:Uri>" +
            XmlEscape(uri.value) +
            "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
            StreamToken(stream_id) +
            "</trt:ProfileToken></trt:GetStreamUriResponse>",
        true};
}

OnvifBodyResult SnapshotUriBody(IOnvifUriProvider* uri_provider,
                                infra::StreamId stream_id,
                                uint32_t* status,
                                std::string* reason) {
    if (uri_provider == nullptr) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    infra::Result<std::string> uri = uri_provider->GetSnapshotUri(stream_id);
    if (!uri.IsOk()) {
        return OnvifBodyResult{UriFault(status, reason), false};
    }
    return OnvifBodyResult{
        "<trt:GetSnapshotUriResponse><trt:MediaUri><tt:Uri>" +
            XmlEscape(uri.value) +
            "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
            StreamToken(stream_id) +
            "</trt:ProfileToken></trt:GetSnapshotUriResponse>",
        true};
}

}  // namespace onvif_internal
}  // namespace live_stream

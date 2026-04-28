#include "onvif_soap.h"

#include <cstdlib>

namespace live_stream {
namespace onvif_internal {
namespace {

int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 +
           static_cast<int64_t>(doe) - 719468;
}

}  // namespace

std::string SoapEnvelope(const std::string& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
           "<s:Body>" +
           body + "</s:Body></s:Envelope>";
}

std::string SoapFault(const std::string& reason) {
    return SoapEnvelope("<s:Fault><s:Reason><s:Text>" + XmlEscape(reason) +
                        "</s:Text></s:Reason></s:Fault>");
}

OnvifAction ParseAction(const std::string& body) {
    if (Contains(body, "GetDeviceInformation")) {
        return OnvifAction::kGetDeviceInformation;
    }
    if (Contains(body, "GetSystemDateAndTime")) {
        return OnvifAction::kGetSystemDateAndTime;
    }
    if (Contains(body, "SetSystemDateAndTime")) {
        return OnvifAction::kSetSystemDateAndTime;
    }
    if (Contains(body, "GetProfiles")) {
        return OnvifAction::kGetProfiles;
    }
    if (Contains(body, "GetStreamUri")) {
        return OnvifAction::kGetStreamUri;
    }
    if (Contains(body, "GetSnapshotUri")) {
        return OnvifAction::kGetSnapshotUri;
    }
    return OnvifAction::kUnknown;
}

infra::Result<int64_t> ExtractInt64Tag(const std::string& text,
                                       const std::string& tag) {
    const std::string begin_tag = "<" + tag + ">";
    const std::string end_tag = "</" + tag + ">";
    const std::size_t begin = text.find(begin_tag);
    if (begin == std::string::npos) {
        return infra::Result<int64_t>::Fail(infra::Status::kNotFound);
    }
    const std::size_t value_begin = begin + begin_tag.size();
    const std::size_t end = text.find(end_tag, value_begin);
    if (end == std::string::npos) {
        return infra::Result<int64_t>::Fail(infra::Status::kInvalidParam);
    }
    const std::string raw = text.substr(value_begin, end - value_begin);
    char* parse_end = nullptr;
    const long long parsed = std::strtoll(raw.c_str(), &parse_end, 10);
    if (parse_end == raw.c_str() || *parse_end != '\0') {
        return infra::Result<int64_t>::Fail(infra::Status::kInvalidParam);
    }
    return infra::Result<int64_t>::Ok(static_cast<int64_t>(parsed));
}

infra::Result<int64_t> ParseOnvifUnixTimeMs(const std::string& request) {
    infra::Result<int64_t> unix_ms = ExtractInt64Tag(request, "tt:UnixTimeMs");
    if (unix_ms.IsOk()) {
        return unix_ms;
    }
    unix_ms = ExtractInt64Tag(request, "UnixTimeMs");
    if (unix_ms.IsOk()) {
        return unix_ms;
    }

    infra::Result<int64_t> year = ExtractInt64Tag(request, "tt:Year");
    infra::Result<int64_t> month = ExtractInt64Tag(request, "tt:Month");
    infra::Result<int64_t> day = ExtractInt64Tag(request, "tt:Day");
    infra::Result<int64_t> hour = ExtractInt64Tag(request, "tt:Hour");
    infra::Result<int64_t> minute = ExtractInt64Tag(request, "tt:Minute");
    infra::Result<int64_t> second = ExtractInt64Tag(request, "tt:Second");
    if (!year.IsOk()) {
        year = ExtractInt64Tag(request, "Year");
    }
    if (!month.IsOk()) {
        month = ExtractInt64Tag(request, "Month");
    }
    if (!day.IsOk()) {
        day = ExtractInt64Tag(request, "Day");
    }
    if (!hour.IsOk()) {
        hour = ExtractInt64Tag(request, "Hour");
    }
    if (!minute.IsOk()) {
        minute = ExtractInt64Tag(request, "Minute");
    }
    if (!second.IsOk()) {
        second = ExtractInt64Tag(request, "Second");
    }
    if (!year.IsOk() || !month.IsOk() || !day.IsOk() || !hour.IsOk() ||
        !minute.IsOk() || !second.IsOk() || year.value < 1970 ||
        month.value < 1 || month.value > 12 || day.value < 1 ||
        day.value > 31 || hour.value < 0 || hour.value > 23 ||
        minute.value < 0 || minute.value > 59 || second.value < 0 ||
        second.value > 60) {
        return infra::Result<int64_t>::Fail(infra::Status::kInvalidParam);
    }

    const int64_t days = DaysFromCivil(
        static_cast<int>(year.value), static_cast<unsigned>(month.value),
        static_cast<unsigned>(day.value));
    const int64_t seconds = days * 86400 + hour.value * 3600 +
                            minute.value * 60 + second.value;
    return infra::Result<int64_t>::Ok(seconds * 1000);
}

}  // namespace onvif_internal
}  // namespace live_stream

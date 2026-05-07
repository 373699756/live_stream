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

bool ExtractInt64Tag(const std::string& text,
                     const std::string& tag,
                     int64_t* value) {
    if (value == nullptr) {
        return false;
    }
    const std::string begin_tag = "<" + tag + ">";
    const std::string end_tag = "</" + tag + ">";
    const std::size_t begin = text.find(begin_tag);
    if (begin == std::string::npos) {
        return false;
    }
    const std::size_t value_begin = begin + begin_tag.size();
    const std::size_t end = text.find(end_tag, value_begin);
    if (end == std::string::npos) {
        return false;
    }
    const std::string raw = text.substr(value_begin, end - value_begin);
    char* parse_end = nullptr;
    const long long parsed = std::strtoll(raw.c_str(), &parse_end, 10);
    if (parse_end == raw.c_str() || *parse_end != '\0') {
        return false;
    }
    *value = static_cast<int64_t>(parsed);
    return true;
}

bool ParseOnvifUnixTimeMs(const std::string& request, int64_t* unix_time_ms) {
    if (unix_time_ms == nullptr) {
        return false;
    }
    int64_t unix_ms = 0;
    if (ExtractInt64Tag(request, "tt:UnixTimeMs", &unix_ms) ||
        ExtractInt64Tag(request, "UnixTimeMs", &unix_ms)) {
        *unix_time_ms = unix_ms;
        return true;
    }

    int64_t year = 0;
    int64_t month = 0;
    int64_t day = 0;
    int64_t hour = 0;
    int64_t minute = 0;
    int64_t second = 0;
    const bool has_year = ExtractInt64Tag(request, "tt:Year", &year) ||
                          ExtractInt64Tag(request, "Year", &year);
    const bool has_month = ExtractInt64Tag(request, "tt:Month", &month) ||
                           ExtractInt64Tag(request, "Month", &month);
    const bool has_day = ExtractInt64Tag(request, "tt:Day", &day) ||
                         ExtractInt64Tag(request, "Day", &day);
    const bool has_hour = ExtractInt64Tag(request, "tt:Hour", &hour) ||
                          ExtractInt64Tag(request, "Hour", &hour);
    const bool has_minute = ExtractInt64Tag(request, "tt:Minute", &minute) ||
                            ExtractInt64Tag(request, "Minute", &minute);
    const bool has_second = ExtractInt64Tag(request, "tt:Second", &second) ||
                            ExtractInt64Tag(request, "Second", &second);
    if (!has_year || !has_month || !has_day || !has_hour || !has_minute ||
        !has_second || year < 1970 || month < 1 || month > 12 || day < 1 ||
        day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60) {
        return false;
    }

    const int64_t days = DaysFromCivil(
        static_cast<int>(year), static_cast<unsigned>(month),
        static_cast<unsigned>(day));
    const int64_t seconds = days * 86400 + hour * 3600 +
                            minute * 60 + second;
    *unix_time_ms = seconds * 1000;
    return true;
}

}  // namespace onvif_internal
}  // namespace live_stream

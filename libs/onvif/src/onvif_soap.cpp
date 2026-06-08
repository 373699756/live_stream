#include "onvif_soap.h"

#include <cstdlib>

namespace live_stream {
namespace onvif {
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

std::string TagLocalName(const std::string &tag_name) {
    const std::size_t separator = tag_name.find(':');
    if (separator == std::string::npos) {
        return tag_name;
    }
    return tag_name.substr(separator + 1);
}

std::string StartTagName(const std::string &tag_body) {
    std::size_t name_end = 0;
    while (name_end < tag_body.size() &&
           tag_body[name_end] != ' ' &&
           tag_body[name_end] != '\t' &&
           tag_body[name_end] != '\r' &&
           tag_body[name_end] != '\n' &&
           tag_body[name_end] != '/') {
        ++name_end;
    }
    return tag_body.substr(0, name_end);
}

bool IsClosingTagForLocalName(const std::string &text,
                              std::size_t tag_begin,
                              const std::string &local_name) {
    if (tag_begin + 2 >= text.size() || text[tag_begin] != '<' ||
        text[tag_begin + 1] != '/') {
        return false;
    }
    const std::size_t tag_end = text.find('>', tag_begin + 2);
    if (tag_end == std::string::npos) {
        return false;
    }
    const std::string tag_name =
        StartTagName(text.substr(tag_begin + 2, tag_end - tag_begin - 2));
    return TagLocalName(tag_name) == local_name;
}

std::size_t FindClosingTag(const std::string &text,
                           std::size_t value_begin,
                           const std::string &local_name) {
    std::size_t search_pos = value_begin;
    while (true) {
        const std::size_t tag_begin = text.find("</", search_pos);
        if (tag_begin == std::string::npos) {
            return std::string::npos;
        }
        if (IsClosingTagForLocalName(text, tag_begin, local_name)) {
            return tag_begin;
        }
        search_pos = tag_begin + 2;
    }
}

std::string LocalNameFromInput(const std::string &name) {
    return TagLocalName(name);
}

}  // namespace

std::string BuildSoapEnvelope(const std::string &body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\""
           " xmlns:a=\"http://www.w3.org/2005/08/addressing\""
           " xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\""
           " xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\""
           " xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\""
           " xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\""
           " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
           "<s:Body>" +
           body + "</s:Body></s:Envelope>";
}

std::string BuildSoapFaultBody(const std::string &reason) {
    return "<s:Fault><s:Reason><s:Text>" + XmlEscape(reason) +
           "</s:Text></s:Reason></s:Fault>";
}

std::string BuildSoapFaultEnvelope(const std::string &reason) {
    return BuildSoapEnvelope(BuildSoapFaultBody(reason));
}

OnvifAction ParseSoapAction(const std::string &body) {
    // 当前只匹配已支持 action 的标签名；ONVIF namespace 前缀不固定，
    // 所以不按完整限定名判断。
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

bool ExtractXmlTagText(const std::string &text,
                       const std::string &local_name,
                       std::string *value) {
    if (value == nullptr || local_name.empty()) {
        return false;
    }
    // 这里是轻量 SOAP 字段提取，不是通用 XML 解析器。只按 local name 匹配
    // ONVIF 当前支持的简单请求字段，命名空间前缀不同也能识别。
    const std::string wanted = LocalNameFromInput(local_name);
    std::size_t search_pos = 0;
    while (true) {
        const std::size_t tag_begin = text.find('<', search_pos);
        if (tag_begin == std::string::npos) {
            return false;
        }
        if (tag_begin + 1 >= text.size() || text[tag_begin + 1] == '/' ||
            text[tag_begin + 1] == '?' || text[tag_begin + 1] == '!') {
            search_pos = tag_begin + 1;
            continue;
        }
        const std::size_t tag_end = text.find('>', tag_begin + 1);
        if (tag_end == std::string::npos) {
            return false;
        }
        const std::string tag_body =
            text.substr(tag_begin + 1, tag_end - tag_begin - 1);
        const std::string tag_name = StartTagName(tag_body);
        if (TagLocalName(tag_name) != wanted) {
            search_pos = tag_end + 1;
            continue;
        }
        if (!tag_body.empty() && tag_body[tag_body.size() - 1] == '/') {
            return false;
        }
        const std::size_t value_begin = tag_end + 1;
        const std::size_t value_end =
            FindClosingTag(text, value_begin, wanted);
        if (value_end == std::string::npos) {
            return false;
        }
        *value = text.substr(value_begin, value_end - value_begin);
        return true;
    }
}

bool ExtractInt64Tag(const std::string &text,
                     const std::string &local_name,
                     int64_t *value) {
    if (value == nullptr) {
        return false;
    }
    std::string raw;
    if (!ExtractXmlTagText(text, local_name, &raw)) {
        return false;
    }
    // 只接受纯整数字段；带单位、空白尾巴或嵌套 XML 都视为无效输入。
    char *parse_end = nullptr;
    const long long parsed = std::strtoll(raw.c_str(), &parse_end, 10);
    if (parse_end == raw.c_str() || *parse_end != '\0') {
        return false;
    }
    *value = static_cast<int64_t>(parsed);
    return true;
}

bool ParseOnvifUnixTimeMs(const std::string &request, int64_t *unix_time_ms) {
    if (unix_time_ms == nullptr) {
        return false;
    }
    // Web 控制面扩展字段 UnixTimeMs 优先；标准 ONVIF 客户端则走
    // Year/Month/Day/Hour/Minute/Second 组合。
    int64_t unix_ms = 0;
    if (ExtractInt64Tag(request, "UnixTimeMs", &unix_ms)) {
        *unix_time_ms = unix_ms;
        return true;
    }

    int64_t year = 0;
    int64_t month = 0;
    int64_t day = 0;
    int64_t hour = 0;
    int64_t minute = 0;
    int64_t second = 0;
    const bool has_year = ExtractInt64Tag(request, "Year", &year);
    const bool has_month = ExtractInt64Tag(request, "Month", &month);
    const bool has_day = ExtractInt64Tag(request, "Day", &day);
    const bool has_hour = ExtractInt64Tag(request, "Hour", &hour);
    const bool has_minute = ExtractInt64Tag(request, "Minute", &minute);
    const bool has_second = ExtractInt64Tag(request, "Second", &second);
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

}  // namespace onvif
}  // namespace live_stream

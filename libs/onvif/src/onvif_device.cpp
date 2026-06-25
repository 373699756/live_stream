#include "onvif_device.h"

#include "onvif_soap.h"
#include "onvif_types.h"
#include "system.h"
#include "system/time.h"

namespace live_stream {
namespace onvif {

std::string BuildDeviceInformationBody(const OnvifServerOptions &options,
                                       ISystem *system) {
    DeviceInfo info;
    std::string manufacturer = options.manufacturer;
    info.model = options.model;
    info.firmware_version = options.firmware_version;
    // 优先使用 system 模块的真实设备信息；缺失时回退到 ONVIF 配置，保证
    // discovery 和 device service 返回的型号/固件保持一致。
    if (system != nullptr) {
        const DeviceInfo service_info = system->GetDeviceInfo();
        if (!service_info.model.empty()) {
            info = service_info;
        }
    }
    return "<tds:GetDeviceInformationResponse>"
           "<tds:Manufacturer>" +
           XmlEscape(manufacturer) + "</tds:Manufacturer><tds:Model>" +
           XmlEscape(info.model) + "</tds:Model><tds:FirmwareVersion>" +
           XmlEscape(info.firmware_version) +
           "</tds:FirmwareVersion><tds:SerialNumber>" +
           XmlEscape(info.serial_number) +
           "</tds:SerialNumber></tds:GetDeviceInformationResponse>";
}

std::string BuildSystemDateAndTimeBody(ITime *time) {
    TimeInfo time_info;
    if (time != nullptr) {
        time_info = time->GetTimeInfo();
    }
    return "<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime>"
           "<tds:TimeZone><tt:TZ>" +
           XmlEscape(time_info.timezone) +
           "</tt:TZ></tds:TimeZone><tds:UTCDateTime><tt:UnixTimeMs>" +
           std::to_string(time_info.system_time_ms) +
           "</tt:UnixTimeMs></tds:UTCDateTime></tds:SystemDateAndTime>"
           "</tds:GetSystemDateAndTimeResponse>";
}

std::string BuildSetSystemDateAndTimeBody(ITime *time,
                                          const std::string &request,
                                          uint32_t *status_code,
                                          std::string *reason) {
    if (time == nullptr) {
        if (status_code != nullptr) {
            *status_code = 500;
        }
        if (reason != nullptr) {
            *reason = "Internal Server Status";
        }
        return BuildSoapFaultBody("time service unavailable");
    }
    int64_t unix_time_ms = 0;
    if (!ParseOnvifUnixTimeMs(request, &unix_time_ms)) {
        if (status_code != nullptr) {
            *status_code = 400;
        }
        if (reason != nullptr) {
            *reason = "Bad Request";
        }
        return BuildSoapFaultBody("invalid date time");
    }
    // ONVIF 校时走 time 模块统一入口，审计上下文标记为 onvif，
    // 不在协议层直接改系统时间。
    live_stream::RequestContext context;
    context.user_name = "onvif";
    if (!time->SetSystemTime(context, unix_time_ms,
                             TimeSyncSource::kOnvif)) {
        if (status_code != nullptr) {
            *status_code = 500;
        }
        if (reason != nullptr) {
            *reason = "Internal Server Status";
        }
        return BuildSoapFaultBody("time sync failed");
    }
    return "<tds:SetSystemDateAndTimeResponse/>";
}

}  // namespace onvif
}  // namespace live_stream

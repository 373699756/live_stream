#include "onvif_device.h"

#include "onvif_soap.h"
#include "onvif_types.h"
#include "system.h"
#include "time_api.h"

namespace live_stream {
namespace onvif {

std::string BuildDeviceInformationBody(const OnvifServerOptions &options,
                                       ISystem *system) {
    DeviceInfo info;
    std::string manufacturer = options.manufacturer;
    info.model = options.model;
    info.firmware_version = options.firmware_version;
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
    TimeStatus status;
    if (time != nullptr) {
        status = time->GetTimeStatus();
    }
    return "<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime>"
           "<tds:TimeZone><tt:TZ>" +
           XmlEscape(status.timezone) +
           "</tt:TZ></tds:TimeZone><tds:UTCDateTime><tt:UnixTimeMs>" +
           std::to_string(status.system_time_ms) +
           "</tt:UnixTimeMs></tds:UTCDateTime></tds:SystemDateAndTime>"
           "</tds:GetSystemDateAndTimeResponse>";
}

std::string BuildSetSystemDateAndTimeBody(ITime *time,
                                          const std::string &request,
                                          uint32_t *status,
                                          std::string *reason) {
    if (time == nullptr) {
        if (status != nullptr) {
            *status = 500;
        }
        if (reason != nullptr) {
            *reason = "Internal Server Status";
        }
        return BuildSoapFaultBody("time service unavailable");
    }
    int64_t unix_time_ms = 0;
    if (!ParseOnvifUnixTimeMs(request, &unix_time_ms)) {
        if (status != nullptr) {
            *status = 400;
        }
        if (reason != nullptr) {
            *reason = "Bad Request";
        }
        return BuildSoapFaultBody("invalid date time");
    }
    live_stream::RequestContext context;
    context.user_name = "onvif";
    if (!time->SetSystemTime(context, unix_time_ms,
                                     TimeSyncSource::kOnvif)) {
        if (status != nullptr) {
            *status = 500;
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

#include "onvif_device.h"

#include "onvif_soap.h"
#include "onvif_types.h"
#include "system_service.h"
#include "time_service.h"

namespace live_stream {
namespace onvif_internal {

std::string DeviceInformationBody(const OnvifServiceOptions& options,
                                  ISystemService* system_service) {
    DeviceInfo info;
    std::string manufacturer = options.manufacturer;
    info.model = options.model;
    info.firmware_version = options.firmware_version;
    if (system_service != nullptr) {
        infra::Result<DeviceInfo> result = system_service->GetDeviceInfo();
        if (result.IsOk()) {
            info = result.value;
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

std::string SystemDateAndTimeBody(ITimeService* time_service) {
    TimeStatus status;
    if (time_service != nullptr) {
        infra::Result<TimeStatus> result = time_service->GetTimeStatus();
        if (result.IsOk()) {
            status = result.value;
        }
    }
    return "<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime>"
           "<tds:TimeZone><tt:TZ>" +
           XmlEscape(status.timezone) +
           "</tt:TZ></tds:TimeZone><tds:UTCDateTime><tt:UnixTimeMs>" +
           std::to_string(status.system_time_ms) +
           "</tt:UnixTimeMs></tds:UTCDateTime></tds:SystemDateAndTime>"
           "</tds:GetSystemDateAndTimeResponse>";
}

std::string SetSystemDateAndTimeBody(ITimeService* time_service,
                                     const std::string& request,
                                     uint32_t* status,
                                     std::string* reason) {
    if (time_service == nullptr) {
        if (status != nullptr) {
            *status = 500;
        }
        if (reason != nullptr) {
            *reason = "Internal Server Status";
        }
        return SoapFault("time service unavailable");
    }
    infra::Result<int64_t> unix_time_ms = ParseOnvifUnixTimeMs(request);
    if (!unix_time_ms.IsOk()) {
        if (status != nullptr) {
            *status = 400;
        }
        if (reason != nullptr) {
            *reason = "Bad Request";
        }
        return SoapFault("invalid date time");
    }
    infra::RequestContext context;
    context.user_name = "onvif";
    const infra::Status error = time_service->SetSystemTime(
        context, unix_time_ms.value, TimeSyncSource::kOnvif);
    if (error != infra::Status::kOk) {
        if (status != nullptr) {
            *status = 500;
        }
        if (reason != nullptr) {
            *reason = "Internal Server Status";
        }
        return SoapFault("time sync failed");
    }
    return "<tds:SetSystemDateAndTimeResponse/>";
}

}  // namespace onvif_internal
}  // namespace live_stream

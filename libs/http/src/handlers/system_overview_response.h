#ifndef LIVE_STREAM_HTTP_SRC_HANDLERS_SYSTEM_OVERVIEW_RESPONSE_H_
#define LIVE_STREAM_HTTP_SRC_HANDLERS_SYSTEM_OVERVIEW_RESPONSE_H_

#include "json.h"

namespace live_stream {

class IAlarm;
class IAiReader;
class INetwork;
class ISystem;
class ITime;
class IUpgrade;
class DeviceMedia;

Json BuildSystemOverviewJson(ISystem *system,
                             ITime *time,
                             INetwork *network,
                             IAlarm *alarm,
                             IUpgrade *upgrade,
                             IAiReader *ai,
                             DeviceMedia *device);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_SYSTEM_OVERVIEW_RESPONSE_H_

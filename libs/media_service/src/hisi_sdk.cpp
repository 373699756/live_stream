#include "hisi_sdk_default.h"

namespace live_stream {
namespace hisisdk {

IHisiSdk& DefaultSdk() {
    static DefaultHisiSdk sdk;
    return sdk;
}

}  // namespace hisisdk
}  // namespace live_stream

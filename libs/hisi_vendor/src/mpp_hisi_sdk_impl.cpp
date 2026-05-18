#include "mpp_hisi_sdk_impl.h"

namespace live_stream {
namespace hisisdk {

MppHisiSdk::MppHisiSdk() : impl_(new Impl()) {}

MppHisiSdk::~MppHisiSdk() {
  DeinitSystem();
  delete impl_;
}

IHisiSdk& MppSdk() {
  static MppHisiSdk sdk;
  return sdk;
}

}  // namespace hisisdk
}  // namespace live_stream

#include "mpp_hisi_sdk_impl.h"

namespace live_stream {
namespace hisisdk {

MppHisiSdk::MppHisiSdk() : impl_(new MppHisiSdkImpl()) {}

MppHisiSdk::~MppHisiSdk() {
    if (impl_ != nullptr) {
        if (impl_->has_active_config_) {
            StopVencStream(impl_->active_config_);
        }
        DeinitSystem();
        delete impl_;
        impl_ = nullptr;
    }
}

IHisiSdk& MppSdk() {
    static MppHisiSdk sdk;
    return sdk;
}

}  // namespace hisisdk
}  // namespace live_stream

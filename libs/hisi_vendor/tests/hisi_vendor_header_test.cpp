#include "hisi_vendor/mpp_hisi_sdk.h"

#include <type_traits>

int main() {
    static_assert(std::is_base_of<live_stream::hisisdk::IHisiSdk,
                                  live_stream::hisisdk::MppHisiSdk>::value,
                  "MppHisiSdk must implement IHisiSdk");
    return 0;
}

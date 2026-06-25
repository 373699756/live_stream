#include "hisi_vendor/mpp_sdk.h"

#include <type_traits>

int main() {
    static_assert(std::is_base_of<live_stream::hisisdk::IHisiSystem,
                                  live_stream::hisisdk::MppHisiSdk>::value,
                  "MppHisiSdk must implement IHisiSystem");
    static_assert(std::is_base_of<live_stream::hisisdk::IHisiMediaPipeline,
                                  live_stream::hisisdk::MppHisiSdk>::value,
                  "MppHisiSdk must implement IHisiMediaPipeline");
    static_assert(std::is_base_of<live_stream::hisisdk::IHisiVencStream,
                                  live_stream::hisisdk::MppHisiSdk>::value,
                  "MppHisiSdk must implement IHisiVencStream");
    static_assert(std::is_base_of<live_stream::hisisdk::IHisiRegion,
                                  live_stream::hisisdk::MppHisiSdk>::value,
                  "MppHisiSdk must implement IHisiRegion");
    static_assert(std::is_base_of<live_stream::hisisdk::IHisiSnapshot,
                                  live_stream::hisisdk::MppHisiSdk>::value,
                  "MppHisiSdk must implement IHisiSnapshot");
    static_assert(std::is_base_of<live_stream::hisisdk::IHisiImage,
                                  live_stream::hisisdk::MppHisiSdk>::value,
                  "MppHisiSdk must implement IHisiImage");
    return 0;
}

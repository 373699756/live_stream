#ifndef LIVE_STREAM_DEVICE_SRC_IMAGE_TUNER_H_
#define LIVE_STREAM_DEVICE_SRC_IMAGE_TUNER_H_

#include "device.h"
#include "image_strategy.h"
#include "hisi_vendor/sdk.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace live_stream {
namespace device_internal {

class ImageTuner {
public:
    using QueryExposure = std::function<hisisdk::ExposureInfo()>;
    using ApplyImage = std::function<bool(const Json& image_config,
                                          uint64_t config_generation)>;

    ImageTuner(QueryExposure query_exposure, ApplyImage apply_image);
    ~ImageTuner();

    void SetConfig(const Json& image_config);
    Json GetConfig() const;
    ImageInfo GetInfo() const;
    bool IsConfigGenerationCurrent(uint64_t config_generation) const;

    void Start();
    void Stop();

private:
    void Run();
    int ResolveStrategyTier(int requested_tier);
    bool IsAutoExposure(const Json &image_config);
    void UpdateImageInfoExposureOnly(const hisisdk::ExposureInfo &exposure,
                                    const Json &image_config,
                                    const ImageInfo &current_info);
    void ResetStrategyState();

    QueryExposure query_exposure_;
    ApplyImage apply_image_;
    mutable std::mutex mutex_;
    std::thread thread_;
    Json image_config_ = Json::object();
    ImageInfo image_info_;
    ImageStrategySettings strategy_settings_;
    uint64_t config_generation_ = 0;
    int current_tier_ = 0;
    int pending_tier_ = -1;
    int pending_tier_hits_ = 0;
    bool tier_state_initialized_ = false;
    bool auto_exposure_mode_ = true;
    bool running_ = false;
    bool stop_requested_ = false;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_IMAGE_TUNER_H_

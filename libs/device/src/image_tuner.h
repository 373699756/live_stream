#ifndef LIVE_STREAM_DEVICE_SRC_IMAGE_TUNER_H_
#define LIVE_STREAM_DEVICE_SRC_IMAGE_TUNER_H_

#include "device.h"
#include "hisi_vendor/sdk.h"

#include <functional>
#include <mutex>
#include <thread>

namespace live_stream {
namespace device_internal {

class ImageTuner {
public:
    using QueryExposure = std::function<hisisdk::ExposureInfo()>;
    using ApplyImage = std::function<bool(const Json& image_config)>;

    ImageTuner(QueryExposure query_exposure, ApplyImage apply_image);
    ~ImageTuner();

    void SetConfig(const Json& image_config);
    Json GetConfig() const;
    ImageInfo GetInfo() const;

    void Start();
    void Stop();

private:
    void Run();

    QueryExposure query_exposure_;
    ApplyImage apply_image_;
    mutable std::mutex mutex_;
    std::thread thread_;
    Json image_config_ = Json::object();
    ImageInfo image_info_;
    bool running_ = false;
    bool stop_requested_ = false;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_IMAGE_TUNER_H_

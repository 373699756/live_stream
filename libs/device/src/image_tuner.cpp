#include "image_tuner.h"

#include "image_strategy.h"

#include <chrono>
#include <utility>

namespace live_stream {
namespace device_internal {
namespace {

constexpr int kImageTunerIntervalMs = 1000;

}  // namespace

ImageTuner::ImageTuner(QueryExposure query_exposure, ApplyImage apply_image)
    : query_exposure_(std::move(query_exposure)),
      apply_image_(std::move(apply_image)) {}

ImageTuner::~ImageTuner() {
    Stop();
}

void ImageTuner::SetConfig(const Json& image_config) {
    std::lock_guard<std::mutex> lock(mutex_);
    image_config_ = image_config;
}

Json ImageTuner::GetConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_config_;
}

ImageInfo ImageTuner::GetInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_info_;
}

void ImageTuner::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    stop_requested_ = false;
    running_ = true;
    thread_ = std::thread(&ImageTuner::Run, this);
}

void ImageTuner::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    stop_requested_ = false;
    image_info_.active = false;
}

void ImageTuner::Run() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return;
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kImageTunerIntervalMs));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return;
            }
            const bool enabled = IsImageStrategyEnabled(image_config_);
            image_info_.enabled = enabled;
            if (!enabled) {
                image_info_.active = false;
                continue;
            }
        }

        const hisisdk::ExposureInfo exposure = query_exposure_();
        if (!exposure.valid) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return;
            }
            image_info_.exposure_valid = false;
            continue;
        }

        ImageInfo next_info;
        Json adjusted;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_ || !IsImageStrategyEnabled(image_config_)) {
                continue;
            }
            adjusted = BuildImageStrategyConfig(image_config_, image_info_,
                                                exposure, next_info);
        }

        bool applied = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stop_requested_ && IsImageStrategyEnabled(image_config_)) {
                applied = true;
            }
        }
        if (applied) {
            applied = apply_image_(adjusted);
        }
        if (applied) {
            std::lock_guard<std::mutex> lock(mutex_);
            image_info_ = next_info;
        }
    }
}

}  // namespace device_internal
}  // namespace live_stream

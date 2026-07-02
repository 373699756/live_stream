#include "image_tuner.h"

#include "image_strategy.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace live_stream {
namespace device_internal {
namespace {

constexpr int kImageTunerIntervalMs = 1000;
constexpr int kUnknownImageTier = -1;

const char *ImageTierName(int tier) {
    switch (tier) {
        case 0:
            return "day";
        case 1:
            return "indoor";
        case 2:
            return "low_light";
        case 3:
            return "very_low_light";
    }
    return "";
}

void ApplyImageStrategyState(ImageInfo &info, int requested_tier,
                            int pending_tier, int pending_tier_hits,
                            int32_t tier_stability_samples) {
    if (requested_tier != kUnknownImageTier) {
        info.requested_tier = ImageTierName(requested_tier);
    } else {
        info.requested_tier.clear();
    }
    if (pending_tier != kUnknownImageTier) {
        info.pending_tier = ImageTierName(pending_tier);
    } else {
        info.pending_tier.clear();
    }
    info.pending_tier_hits = std::max(0, pending_tier_hits);
    info.tier_stability_samples = std::max(1, tier_stability_samples);
}

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
    strategy_settings_ = LoadImageStrategySettings(image_config);
    ++config_generation_;
    ResetStrategyState();
}

Json ImageTuner::GetConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_config_;
}

ImageInfo ImageTuner::GetInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_info_;
}

bool ImageTuner::IsConfigGenerationCurrent(
    uint64_t config_generation) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_generation == config_generation_;
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
    ResetStrategyState();
}

bool ImageTuner::IsAutoExposure(const Json &image_config) {
    const auto exposure = image_config.find("exposure");
    if (exposure == image_config.end() || !exposure->is_object()) {
        return true;
    }
    return exposure->value("mode", std::string("auto")) != "manual";
}

int ImageTuner::ResolveStrategyTier(int requested_tier) {
    if (!tier_state_initialized_) {
        current_tier_ = requested_tier;
        tier_state_initialized_ = true;
        return current_tier_;
    }
    if (current_tier_ == requested_tier) {
        pending_tier_ = -1;
        pending_tier_hits_ = 0;
        return current_tier_;
    }

    if (pending_tier_ == requested_tier) {
        ++pending_tier_hits_;
    } else {
        pending_tier_ = requested_tier;
        pending_tier_hits_ = 1;
    }

    if (pending_tier_hits_ >= strategy_settings_.tier_stability_samples) {
        current_tier_ = pending_tier_;
        pending_tier_ = -1;
        pending_tier_hits_ = 0;
    }
    return current_tier_;
}

void ImageTuner::ResetStrategyState() {
    current_tier_ = 0;
    pending_tier_ = -1;
    pending_tier_hits_ = 0;
    tier_state_initialized_ = false;
    image_info_.requested_tier.clear();
    image_info_.pending_tier.clear();
    image_info_.pending_tier_hits = 0;
    image_info_.tier_stability_samples = 0;
}

void ImageTuner::UpdateImageInfoExposureOnly(const hisisdk::ExposureInfo &exposure,
                                            const Json &image_config,
                                            const ImageInfo &current_info) {
    image_info_.enabled = IsImageStrategyEnabled(image_config);
    image_info_.active = false;
    image_info_.exposure_valid = true;
    image_info_.iso = exposure.iso;
    image_info_.exposure_time_us = exposure.exposure_time_us;
    image_info_.analog_gain = exposure.analog_gain;
    image_info_.digital_gain = exposure.digital_gain;
    image_info_.isp_digital_gain = exposure.isp_digital_gain;
    image_info_.mode = image_config.value("strategy", Json::object())
                          .value("mode", std::string("low_noise"));
    image_info_.tier = current_info.tier.empty() ? "day" : current_info.tier;
    image_info_.requested_tier.clear();
    image_info_.pending_tier.clear();
    image_info_.pending_tier_hits = 0;
    image_info_.tier_stability_samples = strategy_settings_.tier_stability_samples;
    image_info_.saturation = current_info.saturation;
    image_info_.sharpness = current_info.sharpness;
    image_info_.denoise_2d = current_info.denoise_2d;
    image_info_.denoise_3d = current_info.denoise_3d;
    image_info_.gamma = current_info.gamma;
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

        Json image_config;
        ImageInfo current_info;
        ImageStrategySettings strategy_settings;
        uint64_t config_generation = 0;
        bool auto_exposure = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return;
            }
            image_config = image_config_;
            current_info = image_info_;
            strategy_settings = strategy_settings_;
            config_generation = config_generation_;
            auto_exposure = IsAutoExposure(image_config);
            if (auto_exposure_mode_ && !auto_exposure) {
                ResetStrategyState();
            }
            auto_exposure_mode_ = auto_exposure;
            const bool enabled = IsImageStrategyEnabled(image_config);
            image_info_.enabled = enabled;
            if (!enabled) {
                image_info_.active = false;
                ApplyImageStrategyState(image_info_, kUnknownImageTier,
                                       kUnknownImageTier, 0,
                                       strategy_settings.tier_stability_samples);
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
            image_info_.active = false;
            ApplyImageStrategyState(image_info_, kUnknownImageTier, kUnknownImageTier,
                                   0, strategy_settings.tier_stability_samples);
            continue;
        }

        if (!auto_exposure) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_ || !IsImageStrategyEnabled(image_config)) {
                continue;
            }
            UpdateImageInfoExposureOnly(exposure, image_config, current_info);
            ApplyImageStrategyState(image_info_, kUnknownImageTier, kUnknownImageTier,
                                   0, strategy_settings.tier_stability_samples);
            continue;
        }

        ImageInfo next_info;
        Json adjusted;
        int requested_tier = kUnknownImageTier;
        int strategy_tier = kUnknownImageTier;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_ || !IsImageStrategyEnabled(image_config)) {
                continue;
            }
            if (config_generation != config_generation_) {
                continue;
            }
            requested_tier =
                DetermineImageStrategyTier(exposure, strategy_settings);
            strategy_tier = ResolveStrategyTier(requested_tier);
            ApplyImageStrategyState(image_info_, requested_tier, pending_tier_,
                                   pending_tier_hits_,
                                   strategy_settings.tier_stability_samples);
            adjusted = BuildImageStrategyConfig(image_config, current_info,
                                               strategy_tier, exposure, next_info,
                                               strategy_settings);
            if (adjusted == image_config) {
                image_info_.exposure_valid = true;
                image_info_.active = next_info.active;
                image_info_.iso = exposure.iso;
                image_info_.exposure_time_us = exposure.exposure_time_us;
                image_info_.analog_gain = exposure.analog_gain;
                image_info_.digital_gain = exposure.digital_gain;
                image_info_.isp_digital_gain = exposure.isp_digital_gain;
                image_info_.mode = next_info.mode;
                image_info_.tier = next_info.tier;
                image_info_.saturation = next_info.saturation;
                image_info_.sharpness = next_info.sharpness;
                image_info_.denoise_2d = next_info.denoise_2d;
                image_info_.denoise_3d = next_info.denoise_3d;
                image_info_.gamma = next_info.gamma;
                ApplyImageStrategyState(image_info_, requested_tier,
                                       pending_tier_, pending_tier_hits_,
                                       strategy_settings.tier_stability_samples);
                continue;
            }
        }

        if (apply_image_(adjusted, config_generation)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return;
            }
            if (config_generation != config_generation_ ||
                !IsImageStrategyEnabled(image_config_)) {
                continue;
            }
            image_info_ = next_info;
            ApplyImageStrategyState(
                image_info_, requested_tier, pending_tier_,
                pending_tier_hits_,
                strategy_settings.tier_stability_samples);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return;
            }
            if (config_generation != config_generation_ ||
                !IsImageStrategyEnabled(image_config_)) {
                continue;
            }
            image_info_.enabled = true;
            image_info_.active = false;
            image_info_.exposure_valid = true;
            image_info_.iso = exposure.iso;
            image_info_.exposure_time_us = exposure.exposure_time_us;
            image_info_.analog_gain = exposure.analog_gain;
            image_info_.digital_gain = exposure.digital_gain;
            image_info_.isp_digital_gain = exposure.isp_digital_gain;
            image_info_.mode = current_info.mode;
            image_info_.tier = current_info.tier;
            image_info_.saturation = current_info.saturation;
            image_info_.sharpness = current_info.sharpness;
            image_info_.denoise_2d = current_info.denoise_2d;
            image_info_.denoise_3d = current_info.denoise_3d;
            image_info_.gamma = current_info.gamma;
            ApplyImageStrategyState(
                image_info_, requested_tier, pending_tier_,
                pending_tier_hits_,
                strategy_settings.tier_stability_samples);
        }
    }
}

}  // namespace device_internal
}  // namespace live_stream

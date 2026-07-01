#include "handlers/http_handlers.h"

#include "http_ai_health.h"
#include "http_auth_gate.h"
#include "http_path.h"
#include "http_response.h"
#include "http_stream_id_json.h"

#include "ai.h"
#include "config.h"
#include "device.h"
#include "json_reader.h"

#include <cctype>
#include <string>
#include <vector>

namespace live_stream {
namespace {

const char *AiBackendToJsonString(AiBackend backend) {
    switch (backend) {
        case AiBackend::kHi3516Dv300Nnie:
            return "hisi3516dv300_nnie";
    }
    return "unknown";
}

const char *AiTaskToJsonString(AiTask task) {
    switch (task) {
        case AiTask::kObjectDetection:
            return "object_detection";
        case AiTask::kPerimeterDetection:
            return "perimeter_detection";
        case AiTask::kMotionClassification:
            return "motion_classification";
        case AiTask::kOcclusionDetection:
            return "occlusion_detection";
    }
    return "unknown";
}

Json AiTaskConfigToJson(const AiModelConfig &config) {
    Json root = Json::object();
    root["enabled"] = config.enabled;
    root["backend"] = AiBackendToJsonString(config.backend);
    root["task"] = AiTaskToJsonString(config.task);
    root["stream"] = StreamIdToJsonString(config.stream_id);
    root["model_path"] = config.model_path;
    root["input_width"] = config.input_width;
    root["input_height"] = config.input_height;
    root["inference_interval_ms"] = config.inference_interval_ms;
    root["confidence_threshold"] = config.confidence_threshold;
    root["max_results"] = config.max_results;
    Json regions = Json::array();
    for (const AiPerimeterRegion &region : config.perimeter.regions) {
        Json item = Json::object();
        item["name"] = region.name;
        item["x"] = region.x;
        item["y"] = region.y;
        item["width"] = region.width;
        item["height"] = region.height;
        regions.push_back(item);
    }
    root["perimeter_regions"] = regions;
    return root;
}

Json AiConfigToJson(const AiConfig &config) {
    Json root = Json::object();
    root["enabled"] = config.enabled;
    Json tasks = Json::array();
    for (const AiModelConfig &task_config : config.tasks) {
        tasks.push_back(AiTaskConfigToJson(task_config));
    }
    root["tasks"] = tasks;
    return root;
}

Json AiBackendsToJson(const std::vector<AiBackend> &backends) {
    Json items = Json::array();
    for (AiBackend backend : backends) {
        items.push_back(AiBackendToJsonString(backend));
    }
    return items;
}

Json AiStreamsToJson(const std::vector<StreamId> &streams) {
    Json items = Json::array();
    for (StreamId stream : streams) {
        items.push_back(StreamIdToJsonString(stream));
    }
    return items;
}

Json AiTaskCapabilityToJson(const AiTaskCapability &capability) {
    Json root = Json::object();
    root["task"] = AiTaskToJsonString(capability.task);
    root["available"] = capability.available;
    root["requires_model"] = capability.requires_model;
    root["unavailable_reason"] = capability.unavailable_reason;
    root["default_model_path"] = capability.default_model_path;
    root["default_input_width"] = capability.default_input_width;
    root["default_input_height"] = capability.default_input_height;
    root["min_inference_interval_ms"] =
        capability.min_inference_interval_ms;
    root["max_inference_interval_ms"] =
        capability.max_inference_interval_ms;
    root["default_inference_interval_ms"] =
        capability.default_inference_interval_ms;
    root["min_results"] = capability.min_results;
    root["max_results"] = capability.max_results;
    root["default_max_results"] = capability.default_max_results;
    root["min_confidence_threshold"] =
        capability.min_confidence_threshold;
    root["max_confidence_threshold"] =
        capability.max_confidence_threshold;
    root["default_confidence_threshold"] =
        capability.default_confidence_threshold;
    root["max_perimeter_regions"] = capability.max_perimeter_regions;
    root["supported_backends"] =
        AiBackendsToJson(capability.supported_backends);
    root["supported_streams"] =
        AiStreamsToJson(capability.supported_streams);
    return root;
}

Json AiCapabilitiesToJson(const AiCapabilities &capabilities) {
    Json root = Json::object();
    root["available"] = capabilities.available;
    root["model_runtime_available"] =
        capabilities.model_runtime_available;
    root["model_runtime_reason"] = capabilities.model_runtime_reason;
    Json tasks = Json::array();
    for (const AiTaskCapability &capability : capabilities.tasks) {
        tasks.push_back(AiTaskCapabilityToJson(capability));
    }
    root["tasks"] = tasks;
    return root;
}

AiTaskCapability UnavailableAiTaskCapability(AiTask task) {
    AiTaskCapability capability;
    capability.task = task;
    capability.available = false;
    capability.requires_model = task == AiTask::kObjectDetection ||
                                task == AiTask::kPerimeterDetection;
    capability.unavailable_reason = "ai_not_running";
    capability.default_model_path =
        capability.requires_model ? "models/inst_ssd_cycle.wk" : "";
    capability.supported_backends.push_back(AiBackend::kHi3516Dv300Nnie);
    capability.supported_streams.push_back(StreamId::kSub);
    capability.supported_streams.push_back(StreamId::kMain);
    capability.max_perimeter_regions =
        task == AiTask::kPerimeterDetection ? 8 : 0;
    return capability;
}

AiCapabilities UnavailableAiCapabilities() {
    AiCapabilities capabilities;
    capabilities.available = false;
    capabilities.model_runtime_available = false;
    capabilities.model_runtime_reason = "ai_not_running";
    capabilities.tasks.push_back(
        UnavailableAiTaskCapability(AiTask::kObjectDetection));
    capabilities.tasks.push_back(
        UnavailableAiTaskCapability(AiTask::kPerimeterDetection));
    capabilities.tasks.push_back(
        UnavailableAiTaskCapability(AiTask::kMotionClassification));
    capabilities.tasks.push_back(
        UnavailableAiTaskCapability(AiTask::kOcclusionDetection));
    return capabilities;
}

Json AiStatsToJson(const AiStats &stats) {
    Json root = Json::object();
    root["enabled"] = stats.enabled;
    root["backend_available"] = stats.backend_available;
    root["alarm_linked"] = stats.alarm_linked;
    root["last_success_time_ms"] = stats.last_success_time_ms;
    root["last_failure_time_ms"] = stats.last_failure_time_ms;
    root["received_frames"] = stats.received_frames;
    root["skipped_frames"] = stats.skipped_frames;
    root["inferences"] = stats.inferences;
    root["failed_inferences"] = stats.failed_inferences;
    root["dropped_tasks"] = stats.dropped_tasks;
    root["last_inference_time_ms"] = stats.last_inference_time_ms;
    root["max_inference_time_ms"] = stats.max_inference_time_ms;
    root["average_inference_time_ms"] = stats.average_inference_time_ms;
    root["active_results"] = stats.active_results;
    return root;
}

Json AiResultToJson(const AiInferenceResult &result);

Json AiTaskInfoToJson(const AiTaskInfo &task_info) {
    Json root = Json::object();
    root["config"] = AiTaskConfigToJson(task_info.config);
    root["stats"] = AiStatsToJson(task_info.stats);
    root["last_result"] = AiResultToJson(task_info.last_result);
    return root;
}

Json AiTaskInfoListToJson(const std::vector<AiTaskInfo> &task_infos) {
    Json items = Json::array();
    for (const AiTaskInfo &task_info : task_infos) {
        items.push_back(AiTaskInfoToJson(task_info));
    }
    return items;
}

Json DisabledAiStatusToJson(IConfig *config) {
    Json root = Json::object();
    Json ai_config =
        config != nullptr ? config->Get("ai") : Json::object();
    bool enabled = false;
    if (ai_config.is_object()) {
        static_cast<void>(
            json_reader::ReadField(ai_config, "enabled", &enabled));
    } else {
        ai_config = Json::object();
        ai_config["enabled"] = false;
        ai_config["tasks"] = Json::array();
    }
    root["enabled"] = enabled;
    root["config"] = ai_config;
    root["summary"] = AiStatsToJson(AiStats{});
    root["tasks"] = Json::array();
    root["last_result"] = AiResultToJson(AiInferenceResult{});
    root["capabilities"] =
        AiCapabilitiesToJson(UnavailableAiCapabilities());
    return root;
}

Json AiDetectionToJson(const AiDetection &detection) {
    Json root = Json::object();
    root["label"] = detection.label;
    root["confidence"] = detection.confidence;
    root["x"] = detection.x;
    root["y"] = detection.y;
    root["width"] = detection.width;
    root["height"] = detection.height;
    return root;
}

Json AiResultToJson(const AiInferenceResult &result) {
    Json root = Json::object();
    root["success"] = result.success;
    root["stream"] = StreamIdToJsonString(result.stream_id);
    root["sequence"] = result.sequence;
    root["pts_us"] = result.pts_us;
    Json detections = Json::array();
    for (const AiDetection &detection : result.detections) {
        detections.push_back(AiDetectionToJson(detection));
    }
    root["detections"] = detections;
    return root;
}

bool IsValidAlertId(const std::string &id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (const char c : id) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') {
            return false;
        }
    }
    return true;
}

Json AiAlertToJson(const AiAlertRecord &alert) {
    Json root = Json::object();
    root["id"] = alert.id;
    root["timestamp_ms"] = alert.timestamp_ms;
    root["stream"] = StreamIdToJsonString(alert.stream_id);
    root["task"] = AiTaskToJsonString(alert.task);
    root["image_url"] = "/api/ai/alerts/" + alert.id + "/image";
    root["detected_targets"] = alert.detected_targets;
    root["confidence_max"] = alert.max_confidence;
    Json detections = Json::array();
    for (const AiDetection &detection : alert.detections) {
        detections.push_back(AiDetectionToJson(detection));
    }
    root["detections"] = detections;
    return root;
}

Json AiAlertListToJson(const std::vector<AiAlertRecord> &alerts) {
    Json root = Json::object();
    Json items = Json::array();
    for (const AiAlertRecord &alert : alerts) {
        items.push_back(AiAlertToJson(alert));
    }
    root["items"] = items;
    return root;
}

Json ImageInfoToJson(const ImageInfo &info) {
    Json root = Json::object();
    root["enabled"] = info.enabled;
    root["active"] = info.active;
    root["exposure_valid"] = info.exposure_valid;
    root["iso"] = info.iso;
    root["exposure_time_us"] = info.exposure_time_us;
    root["analog_gain"] = info.analog_gain;
    root["digital_gain"] = info.digital_gain;
    root["isp_digital_gain"] = info.isp_digital_gain;
    root["mode"] = info.mode;
    root["tier"] = info.tier;
    root["saturation"] = info.saturation;
    root["sharpness"] = info.sharpness;
    root["denoise_2d"] = info.denoise_2d;
    root["denoise_3d"] = info.denoise_3d;
    root["gamma"] = info.gamma;
    return root;
}

}  // namespace

class AiHttpHandler : public IHttpHandler {
public:
    explicit AiHttpHandler(const AiHandlerRefs &refs)
        : access_(refs.access),
          config_(refs.config),
          ai_(refs.ai),
          device_(refs.device) {}

    void RegisterRoutes(IHttpRouter &router) override {
        router.AddExactRoute(HttpMethod::kGet,
                             "/api/status/image-strategy", this,
                             &AiHttpHandler::HandleImageStrategy);
        router.AddExactRoute(HttpMethod::kGet, "/api/ai/status",
                             this, &AiHttpHandler::HandleInfo);
        router.AddExactRoute(HttpMethod::kGet,
                             "/api/ai/capabilities", this,
                             &AiHttpHandler::HandleCapabilities);
        router.AddExactRoute(HttpMethod::kGet, "/api/ai/alerts",
                             this, &AiHttpHandler::HandleAlerts);
        router.AddPrefixRoute(HttpMethod::kGet, "/api/ai/alerts/",
                              this, &AiHttpHandler::HandleAlertImage);
    }

private:
    HttpResponse HandleImageStrategy(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (device_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        return JsonResponse(
            200, ImageInfoToJson(
                     device_->GetImageInfo()));
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return JsonResponse(
            200, AiCapabilitiesToJson(
                     ai_ != nullptr ? ai_->GetCapabilities()
                                    : UnavailableAiCapabilities()));
    }

    HttpResponse HandleInfo(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (ai_ == nullptr) {
            if (!IsAiConfigEnabled(config_)) {
                return JsonResponse(200, DisabledAiStatusToJson(config_));
            }
            return StatusResponse(503, "AI not running");
        }
        const AiConfig config = ai_->GetConfig();
        Json root = Json::object();
        root["enabled"] = config.enabled;
        root["config"] = AiConfigToJson(config);
        root["summary"] = AiStatsToJson(ai_->GetStats());
        root["tasks"] = AiTaskInfoListToJson(ai_->GetTaskInfoList());
        root["last_result"] = AiResultToJson(ai_->GetLastResult());
        root["capabilities"] =
            AiCapabilitiesToJson(ai_->GetCapabilities());
        return JsonResponse(200, root);
    }

    HttpResponse HandleAlerts(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (ai_ == nullptr) {
            if (!IsAiConfigEnabled(config_)) {
                return JsonResponse(200, AiAlertListToJson({}));
            }
            return StatusResponse(503, "AI not running");
        }
        return JsonResponse(200, AiAlertListToJson(ai_->ListAlerts()));
    }

    HttpResponse HandleAlertImage(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (ai_ == nullptr) {
            return StatusResponse(503, "AI not running");
        }
        const std::string prefix = "/api/ai/alerts/";
        const std::string suffix = PathSuffix(request.path, prefix);
        const std::string marker = "/image";
        if (suffix.size() <= marker.size() ||
            suffix.substr(suffix.size() - marker.size()) != marker) {
            return StatusResponse(404, "Not Found");
        }
        const std::string id = suffix.substr(0, suffix.size() - marker.size());
        if (!IsValidAlertId(id)) {
            return StatusResponse(400, "Invalid alert id");
        }
        const std::string image = ai_->ReadAlertImage(id);
        if (image.empty()) {
            return StatusResponse(404, "AI alert image not found");
        }
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "image/jpeg";
        response.headers["Cache-Control"] = "no-cache";
        response.body = image;
        return response;
    }

    HttpAccess *access_ = nullptr;
    IConfig *config_ = nullptr;
    IAiReader *ai_ = nullptr;
    DeviceMedia *device_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeAiHandler(
    const AiHandlerRefs &refs) {
    return std::unique_ptr<IHttpHandler>(
        new AiHttpHandler(refs));
}

}  // namespace live_stream

#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "ai.h"
#include "config.h"
#include "device.h"
#include "json_utils.h"

#include <cctype>
#include <string>
#include <vector>

namespace live_stream {
namespace {

const char *AiBackendToJsonString(AiBackend backend) {
    switch (backend) {
        case AiBackend::kHi3516Dv300Nnie:
            return "hisi3516dv300_nnie";
        case AiBackend::kHostStub:
            return "host_stub";
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

ConfigJson AiTaskConfigToJson(const AiModelConfig &config) {
    ConfigJson root = ConfigJson::object();
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
    ConfigJson regions = ConfigJson::array();
    for (const AiPerimeterRegion &region : config.perimeter.regions) {
        ConfigJson item = ConfigJson::object();
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

ConfigJson AiConfigToJson(const AiConfig &config) {
    ConfigJson root = ConfigJson::object();
    root["enabled"] = config.enabled;
    ConfigJson tasks = ConfigJson::array();
    for (const AiModelConfig &task_config : config.tasks) {
        tasks.push_back(AiTaskConfigToJson(task_config));
    }
    root["tasks"] = tasks;
    return root;
}

ConfigJson AiBackendsToJson(const std::vector<AiBackend> &backends) {
    ConfigJson items = ConfigJson::array();
    for (AiBackend backend : backends) {
        items.push_back(AiBackendToJsonString(backend));
    }
    return items;
}

ConfigJson AiStreamsToJson(const std::vector<StreamId> &streams) {
    ConfigJson items = ConfigJson::array();
    for (StreamId stream : streams) {
        items.push_back(StreamIdToJsonString(stream));
    }
    return items;
}

ConfigJson AiTaskCapabilityToJson(const AiTaskCapability &capability) {
    ConfigJson root = ConfigJson::object();
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

ConfigJson AiCapabilitiesToJson(const AiCapabilities &capabilities) {
    ConfigJson root = ConfigJson::object();
    root["available"] = capabilities.available;
    root["model_runtime_available"] =
        capabilities.model_runtime_available;
    root["model_runtime_reason"] = capabilities.model_runtime_reason;
    ConfigJson tasks = ConfigJson::array();
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

ConfigJson AiStatsToJson(const AiStats &stats) {
    ConfigJson root = ConfigJson::object();
    root["enabled"] = stats.enabled;
    root["backend_available"] = stats.backend_available;
    root["alarm_linked"] = stats.alarm_linked;
    root["last_success_time_ms"] = stats.last_success_time_ms;
    root["last_failure_time_ms"] = stats.last_failure_time_ms;
    root["received_frames"] = stats.received_frames;
    root["skipped_frames"] = stats.skipped_frames;
    root["inference_count"] = stats.inference_count;
    root["inference_failed_count"] = stats.inference_failed_count;
    root["dropped_tasks"] = stats.dropped_tasks;
    root["last_inference_time_ms"] = stats.last_inference_time_ms;
    root["max_inference_time_ms"] = stats.max_inference_time_ms;
    root["average_inference_time_ms"] = stats.average_inference_time_ms;
    root["active_results"] = stats.active_results;
    return root;
}

ConfigJson AiResultToJson(const AiInferenceResult &result);

ConfigJson AiTaskInfoToJson(const AiTaskInfo &status) {
    ConfigJson root = ConfigJson::object();
    root["config"] = AiTaskConfigToJson(status.config);
    root["stats"] = AiStatsToJson(status.stats);
    root["last_result"] = AiResultToJson(status.last_result);
    return root;
}

ConfigJson AiTaskInfoListToJson(const std::vector<AiTaskInfo> &statuses) {
    ConfigJson items = ConfigJson::array();
    for (const AiTaskInfo &status : statuses) {
        items.push_back(AiTaskInfoToJson(status));
    }
    return items;
}

ConfigJson DisabledAiStatusToJson(IConfig *config) {
    ConfigJson root = ConfigJson::object();
    ConfigJson ai_config =
        config != nullptr ? config->Get("ai") : ConfigJson::object();
    bool enabled = false;
    if (ai_config.is_object()) {
        static_cast<void>(
            json_utils::ReadField(ai_config, "enabled", &enabled));
    } else {
        ai_config = ConfigJson::object();
        ai_config["enabled"] = false;
        ai_config["tasks"] = ConfigJson::array();
    }
    root["enabled"] = enabled;
    root["config"] = ai_config;
    root["summary"] = AiStatsToJson(AiStats{});
    root["tasks"] = ConfigJson::array();
    root["last_result"] = AiResultToJson(AiInferenceResult{});
    root["capabilities"] =
        AiCapabilitiesToJson(UnavailableAiCapabilities());
    return root;
}

ConfigJson AiDetectionToJson(const AiDetection &detection) {
    ConfigJson root = ConfigJson::object();
    root["label"] = detection.label;
    root["confidence"] = detection.confidence;
    root["x"] = detection.x;
    root["y"] = detection.y;
    root["width"] = detection.width;
    root["height"] = detection.height;
    return root;
}

ConfigJson AiResultToJson(const AiInferenceResult &result) {
    ConfigJson root = ConfigJson::object();
    root["success"] = result.success;
    root["stream"] = StreamIdToJsonString(result.stream_id);
    root["sequence"] = result.sequence;
    root["pts_us"] = result.pts_us;
    ConfigJson detections = ConfigJson::array();
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

ConfigJson AiAlertToJson(const AiAlertRecord &alert) {
    ConfigJson root = ConfigJson::object();
    root["id"] = alert.id;
    root["timestamp_ms"] = alert.timestamp_ms;
    root["stream"] = StreamIdToJsonString(alert.stream_id);
    root["task"] = AiTaskToJsonString(alert.task);
    root["image_url"] = "/api/ai/alerts/" + alert.id + "/image";
    root["detection_count"] = alert.detection_count;
    root["confidence_max"] = alert.max_confidence;
    ConfigJson detections = ConfigJson::array();
    for (const AiDetection &detection : alert.detections) {
        detections.push_back(AiDetectionToJson(detection));
    }
    root["detections"] = detections;
    return root;
}

ConfigJson AiAlertListToJson(const std::vector<AiAlertRecord> &alerts) {
    ConfigJson root = ConfigJson::object();
    ConfigJson items = ConfigJson::array();
    for (const AiAlertRecord &alert : alerts) {
        items.push_back(AiAlertToJson(alert));
    }
    root["items"] = items;
    return root;
}

ConfigJson ImageInfoToJson(const ImageInfo &info) {
    ConfigJson root = ConfigJson::object();
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
    AiHttpHandler(HttpAccess *access, IConfig *config,
                  IAiView *ai, DeviceMedia *device)
        : access_(access), config_(config), ai_(ai), device_(device) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet,
                              "/api/status/image-strategy",
                              &AiHttpHandler::HandleImageStrategyRoute,
                              this);
        router->AddExactRoute(HttpMethod::kGet, "/api/ai/status",
                              &AiHttpHandler::HandleStatusRoute, this);
        router->AddExactRoute(HttpMethod::kGet,
                              "/api/ai/capabilities",
                              &AiHttpHandler::HandleCapabilitiesRoute,
                              this);
        router->AddExactRoute(HttpMethod::kGet, "/api/ai/alerts",
                              &AiHttpHandler::HandleAlertsRoute, this);
        router->AddPrefixRoute(HttpMethod::kGet, "/api/ai/alerts/",
                               &AiHttpHandler::HandleAlertImageRoute, this);
    }

private:
    static HttpResponse HandleStatusRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<AiHttpHandler *>(user)->HandleStatus(request);
    }

    static HttpResponse HandleAlertsRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<AiHttpHandler *>(user)->HandleAlerts(request);
    }

    static HttpResponse HandleCapabilitiesRoute(void *user,
                                                const HttpRequest &request) {
        return static_cast<AiHttpHandler *>(user)->HandleCapabilities(
            request);
    }

    static HttpResponse HandleImageStrategyRoute(void *user,
                                                 const HttpRequest &request) {
        return static_cast<AiHttpHandler *>(user)->HandleImageStrategy(
            request);
    }

    static HttpResponse HandleAlertImageRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<AiHttpHandler *>(user)->HandleAlertImage(request);
    }

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

    HttpResponse HandleStatus(const HttpRequest &request) {
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
        ConfigJson root = ConfigJson::object();
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
    IAiView *ai_ = nullptr;
    DeviceMedia *device_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeAiHandler(HttpAccess *access,
                                            IConfig *config, IAiView *ai,
                                            DeviceMedia *device) {
    return std::unique_ptr<IHttpHandler>(
        new AiHttpHandler(access, config, ai, device));
}

}  // namespace live_stream

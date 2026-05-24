#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "ai_service.h"
#include "config_service.h"

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
        case AiTask::kFaceDetection:
            return "face_detection";
        case AiTask::kMotionClassification:
            return "motion_classification";
    }
    return "unknown";
}

ConfigJson AiConfigToJson(const AiModelConfig &config) {
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
    return root;
}

ConfigJson AiStatsToJson(const AiServiceStats &stats) {
    ConfigJson root = ConfigJson::object();
    root["enabled"] = stats.enabled;
    root["backend_available"] = stats.backend_available;
    root["received_frames"] = stats.received_frames;
    root["skipped_frames"] = stats.skipped_frames;
    root["inference_count"] = stats.inference_count;
    root["inference_failed_count"] = stats.inference_failed_count;
    root["dropped_tasks"] = stats.dropped_tasks;
    root["active_results"] = stats.active_results;
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

}  // namespace

class AiHttpHandler : public IHttpHandler {
public:
    AiHttpHandler(HttpAccess *access, IConfigService *config_service,
                  IAiView *ai_service)
        : access_(access), config_service_(config_service),
          ai_service_(ai_service) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/ai/status",
                              &AiHttpHandler::HandleStatusRoute, this);
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

    static HttpResponse HandleAlertImageRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<AiHttpHandler *>(user)->HandleAlertImage(request);
    }

    HttpResponse HandleStatus(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (ai_service_ == nullptr) {
            if (!IsAiConfigEnabled(config_service_)) {
                ConfigJson root = ConfigJson::object();
                root["config"] =
                    config_service_->GetValue("ai");
                root["stats"] = AiStatsToJson(AiServiceStats{});
                root["last_result"] = AiResultToJson(AiInferenceResult{});
                return JsonResponse(200, root);
            }
            return StatusResponse(503, "AI service not running");
        }
        ConfigJson root = ConfigJson::object();
        root["config"] = AiConfigToJson(ai_service_->GetConfig());
        root["stats"] = AiStatsToJson(ai_service_->GetStats());
        root["last_result"] =
            AiResultToJson(ai_service_->GetLastResult());
        return JsonResponse(200, root);
    }

    HttpResponse HandleAlerts(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (ai_service_ == nullptr) {
            if (!IsAiConfigEnabled(config_service_)) {
                return JsonResponse(200, AiAlertListToJson({}));
            }
            return StatusResponse(503, "AI service not running");
        }
        return JsonResponse(200, AiAlertListToJson(ai_service_->ListAlerts()));
    }

    HttpResponse HandleAlertImage(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (ai_service_ == nullptr) {
            return StatusResponse(503, "AI service not running");
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
        const std::string image = ai_service_->ReadAlertImage(id);
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
    IConfigService *config_service_ = nullptr;
    IAiView *ai_service_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateAiHttpHandler(HttpAccess *access,
                    IConfigService *config_service, IAiView *ai_service) {
    return std::unique_ptr<IHttpHandler>(
        new AiHttpHandler(access, config_service, ai_service));
}

}  // namespace live_stream

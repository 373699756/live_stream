#ifndef LIVE_STREAM_AI_SRC_AI_ALERT_IMAGES_H_
#define LIVE_STREAM_AI_SRC_AI_ALERT_IMAGES_H_

#include "ai.h"
#include "executor.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace live_stream {

class DeviceMedia;

namespace ai_internal {

struct AiAlertCapture {
    AiInferenceResult result;
    AiModelConfig config;
    int64_t timestamp_ms = 0;
    std::function<void()> on_drop;
};

class AiAlertImages final {
public:
    AiAlertImages(const std::string &image_dir, uint32_t max_records);
    ~AiAlertImages();

    bool Start();
    void Stop();
    bool PostCapture(DeviceMedia *device, const AiAlertCapture &capture);
    std::vector<AiAlertRecord> List() const;
    std::string ReadImage(const std::string &id) const;

private:
    void SaveCapture(DeviceMedia *device, const AiAlertCapture &capture);
    void AddAlert(const AiAlertRecord &alert);
    uint64_t NextAlertId();

    std::string image_dir_;
    uint32_t max_records_ = 100;
    std::shared_ptr<event::Executor> executor_;
    std::vector<AiAlertRecord> alerts_;
    uint64_t next_alert_id_ = 1;
    mutable std::mutex mutex_;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_ALERT_IMAGES_H_

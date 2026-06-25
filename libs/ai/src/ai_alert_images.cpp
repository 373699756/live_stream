#include "ai_alert_images.h"

#include "device.h"
#include "infra/fs.h"

#include <algorithm>
#include <cstdint>

namespace live_stream {
namespace ai_internal {
namespace {

constexpr uint32_t kAlertExecutorQueueCapacity = 8;

bool LooksLikeJpeg(const SnapshotFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    return data != nullptr && frame.Size() >= 2 && data[0] == 0xff &&
           data[1] == 0xd8;
}

float MaxConfidence(const std::vector<AiDetection> &detections) {
    float max_confidence = 0.0f;
    for (const AiDetection &detection : detections) {
        if (detection.confidence > max_confidence) {
            max_confidence = detection.confidence;
        }
    }
    return max_confidence;
}

std::string AlertImagePath(const std::string &dir, const std::string &id) {
    return infra::Path::Join(dir, id + ".jpg");
}

void NotifyDrop(const AiAlertCapture &capture) {
    if (capture.on_drop) {
        capture.on_drop();
    }
}

}  // namespace

AiAlertImages::AiAlertImages(const std::string &image_dir,
                             uint32_t max_records)
    : image_dir_(image_dir), max_records_(max_records == 0 ? 100 : max_records) {}

AiAlertImages::~AiAlertImages() { Stop(); }

bool AiAlertImages::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (executor_) {
        return true;
    }
    std::shared_ptr<event::Executor> next_executor(new event::Executor());
    event::ExecutorOptions options;
    options.workers = 1;
    options.queue_capacity = kAlertExecutorQueueCapacity;
    if (!next_executor->Start(options)) {
        return false;
    }
    executor_ = next_executor;
    return true;
}

void AiAlertImages::Stop() {
    std::shared_ptr<event::Executor> stopped_executor;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_executor = std::move(executor_);
    }
    if (stopped_executor) {
        stopped_executor->Stop(event::StopMode::kDiscard);
    }
}

bool AiAlertImages::PostCapture(DeviceMedia *device,
                                const AiAlertCapture &capture) {
    std::shared_ptr<event::Executor> executor_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        executor_snapshot = executor_;
    }
    if (device == nullptr || !executor_snapshot) {
        NotifyDrop(capture);
        return false;
    }
    if (executor_snapshot->Post([this, device, capture]() {
            SaveCapture(device, capture);
        }) != event::EventStatus::kOk) {
        NotifyDrop(capture);
        return false;
    }
    return true;
}

std::vector<AiAlertRecord> AiAlertImages::List() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AiAlertRecord> alerts = alerts_;
    std::reverse(alerts.begin(), alerts.end());
    return alerts;
}

std::string AiAlertImages::ReadImage(const std::string &id) const {
    if (id.empty()) {
        return std::string();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iter = std::find_if(
            alerts_.begin(), alerts_.end(),
            [&id](const AiAlertRecord &alert) { return alert.id == id; });
        if (iter == alerts_.end()) {
            return std::string();
        }
    }
    return infra::File::ReadAll(AlertImagePath(image_dir_, id));
}

void AiAlertImages::SaveCapture(DeviceMedia *device,
                                const AiAlertCapture &capture) {
    SnapshotRequest request;
    request.stream_id = capture.config.stream_id;
    request.include_thumbnail = false;
    SnapshotFrame frame = device->CaptureSnapshot(request);
    if (!LooksLikeJpeg(frame)) {
        NotifyDrop(capture);
        return;
    }

    if (!infra::Path::MakeDirs(image_dir_)) {
        NotifyDrop(capture);
        return;
    }

    const std::string id = std::to_string(capture.timestamp_ms) + "-" +
                           std::to_string(NextAlertId());
    const uint8_t *data = frame.PayloadData();
    std::string image;
    image.assign(reinterpret_cast<const char *>(data), frame.Size());
    if (!infra::File::WriteAll(AlertImagePath(image_dir_, id), image)) {
        NotifyDrop(capture);
        return;
    }

    AiAlertRecord alert;
    alert.id = id;
    alert.timestamp_ms = capture.timestamp_ms;
    alert.stream_id = capture.result.stream_id;
    alert.task = capture.config.task;
    alert.detected_targets =
        static_cast<uint32_t>(capture.result.detections.size());
    alert.max_confidence = MaxConfidence(capture.result.detections);
    alert.detections = capture.result.detections;
    AddAlert(alert);
}

void AiAlertImages::AddAlert(const AiAlertRecord &alert) {
    std::vector<std::string> expired_image_paths;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        alerts_.push_back(alert);
        while (alerts_.size() > max_records_) {
            expired_image_paths.push_back(
                AlertImagePath(image_dir_, alerts_.front().id));
            alerts_.erase(alerts_.begin());
        }
    }
    for (const std::string &path : expired_image_paths) {
        static_cast<void>(infra::File::Remove(path));
    }
}

uint64_t AiAlertImages::NextAlertId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_alert_id_++;
}

}  // namespace ai_internal
}  // namespace live_stream

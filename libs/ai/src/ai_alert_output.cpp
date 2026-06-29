#include "ai_alert_output.h"

namespace live_stream {
namespace ai_internal {

AiAlertOutput::AiAlertOutput(IAlarm *alarm,
                             DeviceMedia *device,
                             const std::string &image_dir,
                             uint32_t max_records)
    : alarm_(alarm),
      device_(device),
      images_(image_dir, max_records == 0 ? 100 : max_records) {}

bool AiAlertOutput::Linked() const { return alarm_ != nullptr; }

bool AiAlertOutput::CanCaptureImages() const { return device_ != nullptr; }

bool AiAlertOutput::StartImages() { return images_.Start(); }

void AiAlertOutput::StopImages() { images_.Stop(); }

bool AiAlertOutput::PostCapture(const AiAlertCapture &capture) {
    return images_.PostCapture(device_, capture);
}

bool AiAlertOutput::PublishAlarmInput(const AlarmInput &input) {
    if (alarm_ == nullptr) {
        return true;
    }
    return alarm_->InjectAlarmInput(input);
}

void AiAlertOutput::ClearAlarmInput() {
    if (alarm_ == nullptr) {
        return;
    }
    AlarmInput input;
    input.source = AlarmSource::kAiDetection;
    input.active = false;
    static_cast<void>(alarm_->InjectAlarmInput(input));
}

std::vector<AiAlertRecord> AiAlertOutput::ListImages() const {
    return images_.List();
}

std::string AiAlertOutput::ReadImage(const std::string &id) const {
    return images_.ReadImage(id);
}

}  // namespace ai_internal
}  // namespace live_stream

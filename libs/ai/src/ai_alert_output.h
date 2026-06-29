#ifndef LIVE_STREAM_AI_SRC_AI_ALERT_OUTPUT_H_
#define LIVE_STREAM_AI_SRC_AI_ALERT_OUTPUT_H_

#include "ai.h"
#include "ai_alert_images.h"
#include "alarm.h"
#include "device.h"

#include <string>
#include <vector>

namespace live_stream {
namespace ai_internal {

class AiAlertOutput final {
public:
    AiAlertOutput(IAlarm *alarm,
                  DeviceMedia *device,
                  const std::string &image_dir,
                  uint32_t max_records);

    bool Linked() const;
    bool CanCaptureImages() const;
    bool StartImages();
    void StopImages();
    bool PostCapture(const AiAlertCapture &capture);
    bool PublishAlarmInput(const AlarmInput &input);
    void ClearAlarmInput();
    std::vector<AiAlertRecord> ListImages() const;
    std::string ReadImage(const std::string &id) const;

private:
    IAlarm *alarm_ = nullptr;
    DeviceMedia *device_ = nullptr;
    AiAlertImages images_;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_ALERT_OUTPUT_H_

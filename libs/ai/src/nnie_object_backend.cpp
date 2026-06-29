#include "nnie_object_backend.h"

#include "nnie_forward_workspace.h"
#include "nnie_input_writer.h"
#include "nnie_model_session.h"
#include "nnie_ssd_output_decoder.h"

#include "ai_config.h"
#include "hisi_ai_platform.h"
#include "infra/log.h"

namespace live_stream {
namespace ai_internal {

struct NnieObjectBackend::Impl {
    bool Start(const AiModelConfig &config) {
        if (config.model_path.empty()) {
            return false;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        Stop();
        if (!LoadModel(config)) {
            return false;
        }
        started_ = true;
        return true;
#else
        (void)config;
        return false;
#endif
    }

    void Stop() {
#if LIVE_STREAM_HAS_HISI_NNIE
        UnloadModel();
#endif
        started_ = false;
    }

    bool Started() const { return started_; }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) {
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!started_ || !frame.Valid()) {
            return result;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        if (!model_session_.Loaded()) {
            return result;
        }
        if (!input_writer_.Write(frame, config, workspace_.InputBlob())) {
            return result;
        }
        if (!workspace_.Forward(model_session_.MutableModel())) {
            return result;
        }
        result.success = true;
        if ((config.task == AiTask::kObjectDetection ||
             config.task == AiTask::kPerimeterDetection) &&
            ssd_decoder_.Ready()) {
            result.detections = ssd_decoder_.Decode(workspace_, config);
        }
#else
        (void)config;
#endif
        return result;
    }

#if LIVE_STREAM_HAS_HISI_NNIE
    bool LoadModel(const AiModelConfig &config) {
        if (!model_session_.Load(config)) {
            return false;
        }
        if (!workspace_.Prepare(model_session_.MutableModel())) {
            UnloadModel();
            return false;
        }
        if (!workspace_.ValidateInputConfig(config)) {
            UnloadModel();
            return false;
        }
        if (!ssd_decoder_.Prepare(model_session_.Model(), workspace_)) {
            UnloadModel();
            return false;
        }
        if (!ValidateTaskModel(config)) {
            UnloadModel();
            return false;
        }
        return true;
    }

    void UnloadModel() {
        workspace_.Release();
        input_writer_.Release();
        ssd_decoder_.Clear();
        model_session_.Unload();
    }

    bool ValidateTaskModel(const AiModelConfig &config) const {
        if (config.task == AiTask::kObjectDetection ||
            config.task == AiTask::kPerimeterDetection) {
            if (ssd_decoder_.Ready()) {
                return true;
            }
            Error("ai", "Unsupported object detection NNIE model outputs");
            return false;
        }
        return true;
    }

    NnieModelSession model_session_;
    NnieForwardWorkspace workspace_;
    NnieInputWriter input_writer_;
    NnieSsdOutputDecoder ssd_decoder_;
#endif
    bool started_ = false;
};

NnieObjectBackend::NnieObjectBackend() : impl_(new Impl()) {}

NnieObjectBackend::~NnieObjectBackend() { delete impl_; }

bool NnieObjectBackend::Start(const AiModelConfig &config) {
    return impl_->Start(config);
}

void NnieObjectBackend::Stop() { impl_->Stop(); }

bool NnieObjectBackend::Started() const { return impl_->Started(); }

AiInferenceResult NnieObjectBackend::Run(const hisisdk::YuvFrame &frame,
                                         StreamId stream_id,
                                         const AiModelConfig &config) {
    return impl_->Run(frame, stream_id, config);
}

}  // namespace ai_internal
}  // namespace live_stream

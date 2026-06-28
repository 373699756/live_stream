#include "nnie_object_backend.h"

#include "nnie_forward_workspace.h"
#include "nnie_input_writer.h"
#include "nnie_model_session.h"

#include "ai_config.h"
#include "hisi_ai_platform.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "ssd_postprocess.h"

#include <cstring>
#include <vector>

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
            ssd_model_ready_) {
            result.detections = DecodeSsdDetections(config);
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
        if (!PrepareSsdPostprocess()) {
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
        ClearSsdPostprocessCache();
        model_session_.Unload();
    }

    bool ValidateTaskModel(const AiModelConfig &config) const {
        if (config.task == AiTask::kObjectDetection ||
            config.task == AiTask::kPerimeterDetection) {
            if (ssd_model_ready_) {
                return true;
            }
            Error("ai", "Unsupported object detection NNIE model outputs");
            return false;
        }
        return true;
    }

    bool CopyS32BlobValues(const SVP_DST_BLOB_S &blob,
                           std::vector<int32_t> *values) const {
        if (values == nullptr || blob.enType != SVP_BLOB_TYPE_S32 ||
            blob.u64VirAddr == 0 ||
            blob.u32Stride < blob.unShape.stWhc.u32Width * sizeof(int32_t)) {
            return false;
        }
        const int32_t *data =
            static_cast<const int32_t *>(VirAddrToPointer(blob.u64VirAddr));
        if (data == nullptr) {
            return false;
        }
        const uint32_t stride_words = blob.u32Stride / sizeof(int32_t);
        const uint64_t total_values =
            static_cast<uint64_t>(blob.u32Num) *
            blob.unShape.stWhc.u32Chn *
            blob.unShape.stWhc.u32Height *
            blob.unShape.stWhc.u32Width;
        if (total_values > static_cast<uint64_t>(kMaxHiU32)) {
            return false;
        }
        values->clear();
        values->resize(static_cast<std::size_t>(total_values));
        int32_t *output = values->data();
        for (uint32_t n = 0; n < blob.u32Num; ++n) {
            const uint32_t batch_offset =
                n * blob.unShape.stWhc.u32Chn *
                blob.unShape.stWhc.u32Height * stride_words;
            for (uint32_t chn = 0; chn < blob.unShape.stWhc.u32Chn; ++chn) {
                for (uint32_t row = 0; row < blob.unShape.stWhc.u32Height;
                     ++row) {
                    const uint32_t row_offset =
                        batch_offset +
                        (chn * blob.unShape.stWhc.u32Height + row) *
                            stride_words;
                    const std::size_t row_bytes =
                        static_cast<std::size_t>(blob.unShape.stWhc.u32Width) *
                        sizeof(int32_t);
                    std::memcpy(output, data + row_offset, row_bytes);
                    output += blob.unShape.stWhc.u32Width;
                }
            }
        }
        return true;
    }

    bool CollectSsdOutputs() {
        ssd_postprocess_.BeginFrame();

        for (uint32_t layer = 0; layer < kSsdLayers; ++layer) {
            if (!CopyS32BlobValues(workspace_.OutputBlob(layer * 2U),
                                   &ssd_layer_values_) ||
                !ssd_postprocess_.AppendLocationLayer(layer,
                                                      ssd_layer_values_)) {
                return false;
            }

            if (!CopyS32BlobValues(workspace_.OutputBlob(layer * 2U + 1U),
                                   &ssd_layer_values_) ||
                !ssd_postprocess_.AppendConfidenceLayer(layer,
                                                        ssd_layer_values_)) {
                return false;
            }
        }

        return ssd_postprocess_.IsFrameComplete();
    }

    bool PrepareSsdPostprocess() {
        ssd_model_ready_ = workspace_.IsSsdModel(model_session_.Model());
        if (!ssd_model_ready_) {
            ssd_layer_values_.clear();
            ssd_postprocess_.Clear();
            return true;
        }
        if (!ssd_postprocess_.Prepare()) {
            Error("ai", "Prepare SSD priors failed");
            ClearSsdPostprocessCache();
            return false;
        }
        ssd_layer_values_.reserve(kSsdSoftmaxInputChannel[0]);
        return true;
    }

    void ClearSsdPostprocessCache() {
        ssd_model_ready_ = false;
        ssd_layer_values_.clear();
        ssd_postprocess_.Clear();
    }

    std::vector<AiDetection> DecodeSsdDetections(const AiModelConfig &config) {
        if (!CollectSsdOutputs()) {
            return std::vector<AiDetection>();
        }
        return ssd_postprocess_.DecodeDetections(config);
    }

    NnieModelSession model_session_;
    NnieForwardWorkspace workspace_;
    NnieInputWriter input_writer_;
    std::vector<int32_t> ssd_layer_values_;
    SsdPostprocess ssd_postprocess_;
    bool ssd_model_ready_ = false;
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

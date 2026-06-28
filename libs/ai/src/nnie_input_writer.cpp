#include "nnie_input_writer.h"

#include "hisi_ai_platform.h"

#include <array>
#include <cstring>
#include <vector>

namespace live_stream {
namespace ai_internal {
namespace {

#if LIVE_STREAM_HAS_HISI_NNIE
constexpr uint32_t kVgsFrameAlign = 32;
constexpr uint32_t kIveImageAlign = 16;
constexpr uint32_t kIveCscMinWidth = 64;
constexpr uint32_t kIveCscMinHeight = 64;

std::array<int, 256> BuildYuvYTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        const int c = static_cast<int>(i) > 16 ? static_cast<int>(i) - 16 : 0;
        table[i] = 298 * c;
    }
    return table;
}

std::array<int, 256> BuildYuvUToBTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = 516 * (static_cast<int>(i) - 128);
    }
    return table;
}

std::array<int, 256> BuildYuvUToGTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = -100 * (static_cast<int>(i) - 128);
    }
    return table;
}

std::array<int, 256> BuildYuvVToRTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = 409 * (static_cast<int>(i) - 128);
    }
    return table;
}

std::array<int, 256> BuildYuvVToGTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = -208 * (static_cast<int>(i) - 128);
    }
    return table;
}

const std::array<int, 256> kYuvYTable = BuildYuvYTable();
const std::array<int, 256> kYuvUToBTable = BuildYuvUToBTable();
const std::array<int, 256> kYuvUToGTable = BuildYuvUToGTable();
const std::array<int, 256> kYuvVToRTable = BuildYuvVToRTable();
const std::array<int, 256> kYuvVToGTable = BuildYuvVToGTable();

struct U8C3SamplePoint {
    uint32_t y_offset = 0;
    uint32_t vu_offset = 0;
};

struct ScaledYvuFrame {
    VIDEO_FRAME_INFO_S frame_info{};
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
};

struct IveRgbFrame {
    IVE_IMAGE_S image{};
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
};

bool CheckedFrameRange(uint32_t stride, uint32_t width, uint32_t height,
                       uint32_t available_size) {
    if (stride < width || width == 0 || height == 0) {
        return false;
    }
    const uint64_t end =
        static_cast<uint64_t>(stride) * (height - 1U) + width;
    return end <= available_size;
}

bool IsValidYvu420FrameRange(uint32_t stride_y, uint32_t stride_uv,
                             uint32_t width, uint32_t height,
                             uint32_t available_size) {
    const uint64_t y_size = static_cast<uint64_t>(stride_y) * height;
    if (y_size > available_size || y_size > kMaxHiU32) {
        return false;
    }
    const uint32_t uv_available_size =
        available_size - static_cast<uint32_t>(y_size);
    return CheckedFrameRange(stride_y, width, height,
                             static_cast<uint32_t>(y_size)) &&
           CheckedFrameRange(stride_uv, width, height / 2U,
                             uv_available_size);
}

uint8_t ClampToByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}
#endif

}  // namespace

struct NnieInputWriter::Impl {
#if LIVE_STREAM_HAS_HISI_NNIE
    void FreeScaledYvuFrame() {
        if (scaled_yvu_frame_.phy_addr != 0 && scaled_yvu_frame_.vir_addr) {
            HI_MPI_SYS_MmzFree(scaled_yvu_frame_.phy_addr,
                               scaled_yvu_frame_.vir_addr);
        }
        scaled_yvu_frame_ = ScaledYvuFrame{};
    }

    void FreeIveRgbFrame() {
        if (ive_rgb_frame_.phy_addr != 0 && ive_rgb_frame_.vir_addr) {
            HI_MPI_SYS_MmzFree(ive_rgb_frame_.phy_addr,
                               ive_rgb_frame_.vir_addr);
        }
        ive_rgb_frame_ = IveRgbFrame{};
    }

    bool ValidateYuvFrameRange(const hisisdk::YuvFrame &frame,
                               uint32_t *available_size) const {
        if (available_size == nullptr || !frame.buffer.Valid() ||
            frame.buffer.Data() == nullptr ||
            frame.width == 0 || frame.height == 0 ||
            (frame.width % 2U) != 0 || (frame.height % 2U) != 0 ||
            frame.stride_y < frame.width || frame.stride_uv < frame.width) {
            return false;
        }

        *available_size = frame.Size();
        const uint64_t y_size =
            static_cast<uint64_t>(frame.stride_y) * frame.height;
        const uint64_t uv_size =
            static_cast<uint64_t>(frame.stride_uv) * (frame.height / 2U);
        if (y_size + uv_size > *available_size || y_size > kMaxHiU32) {
            return false;
        }
        return true;
    }

    bool Write(const hisisdk::YuvFrame &frame,
               const AiModelConfig &config,
               SVP_SRC_BLOB_S *input_tensor) {
        input_tensor_ = input_tensor;
        if (input_tensor_ == nullptr) {
            return false;
        }
        SVP_SRC_BLOB_S &src = *input_tensor_;
        if (src.enType == SVP_BLOB_TYPE_YVU420SP) {
            return WriteYvu420spTensor(frame, config);
        }
        if (src.enType == SVP_BLOB_TYPE_U8) {
            return WriteU8C3Tensor(frame);
        }
        return false;
    }

    bool WriteYvu420spTensor(const hisisdk::YuvFrame &frame,
                             const AiModelConfig &config) {
        SVP_SRC_BLOB_S &src = *input_tensor_;
        uint32_t available_size = 0;
        if (src.enType != SVP_BLOB_TYPE_YVU420SP ||
            src.unShape.stWhc.u32Width != config.input_width ||
            src.unShape.stWhc.u32Height != config.input_height ||
            frame.width != config.input_width ||
            frame.height != config.input_height ||
            !ValidateYuvFrameRange(frame, &available_size)) {
            return false;
        }

        const uint8_t *frame_data = frame.buffer.Data();
        const uint32_t y_size = frame.stride_y * frame.height;
        if (!IsValidYvu420FrameRange(frame.stride_y, frame.stride_uv,
                                     config.input_width, config.input_height,
                                     available_size)) {
            return false;
        }

        uint8_t *dst = static_cast<uint8_t *>(VirAddrToPointer(src.u64VirAddr));
        if (dst == nullptr || src.u32Stride < config.input_width) {
            return false;
        }
        const uint8_t *src_y = frame_data;
        const uint8_t *src_uv = frame_data + y_size;
        const uint32_t total_rows = config.input_height * 3U / 2U;
        for (uint32_t row = 0; row < config.input_height; ++row) {
            std::memcpy(dst + row * src.u32Stride,
                        src_y + row * frame.stride_y,
                        config.input_width);
        }
        for (uint32_t row = 0; row < config.input_height / 2U; ++row) {
            std::memcpy(dst + (config.input_height + row) * src.u32Stride,
                        src_uv + row * frame.stride_uv,
                        config.input_width);
        }

        const HI_U32 flush_size = total_rows * src.u32Stride;
        const HI_S32 ret = HI_MPI_SYS_MmzFlushCache(src.u64PhyAddr, dst,
                                                    flush_size);
        return ret == HI_SUCCESS;
    }

    bool WriteU8C3Tensor(const hisisdk::YuvFrame &frame) {
        SVP_SRC_BLOB_S &dst_blob = *input_tensor_;
        uint32_t available_size = 0;
        if (dst_blob.enType != SVP_BLOB_TYPE_U8 ||
            dst_blob.unShape.stWhc.u32Chn != 3 ||
            !ValidateYuvFrameRange(frame, &available_size)) {
            return false;
        }
        const uint32_t dst_width = dst_blob.unShape.stWhc.u32Width;
        const uint32_t dst_height = dst_blob.unShape.stWhc.u32Height;
        if (dst_width == 0 || dst_height == 0 ||
            dst_blob.u32Stride < dst_width) {
            return false;
        }
        if (TryWriteU8C3TensorWithVgs(frame, dst_width, dst_height)) {
            return true;
        }

        const uint8_t *frame_data = frame.buffer.Data();
        const uint32_t y_size = frame.stride_y * frame.height;
        if (!IsValidYvu420FrameRange(frame.stride_y, frame.stride_uv,
                                     frame.width, frame.height,
                                     available_size)) {
            return false;
        }
        if (!EnsureU8C3SampleMap(frame, dst_width, dst_height)) {
            return false;
        }

        uint8_t *dst =
            static_cast<uint8_t *>(VirAddrToPointer(dst_blob.u64VirAddr));
        if (dst == nullptr) {
            return false;
        }
        const uint8_t *src_y = frame_data;
        const uint8_t *src_vu = frame_data + y_size;
        const uint32_t channel_size = dst_blob.u32Stride * dst_height;
        uint8_t *dst_b = dst;
        uint8_t *dst_g = dst + channel_size;
        uint8_t *dst_r = dst + channel_size * 2U;

        for (uint32_t y = 0; y < dst_height; ++y) {
            const uint32_t sample_row = y * dst_width;
            const uint32_t dst_offset = y * dst_blob.u32Stride;
            for (uint32_t x = 0; x < dst_width; ++x) {
                const U8C3SamplePoint &sample =
                    u8c3_sample_points_[sample_row + x];
                const uint8_t y_value = src_y[sample.y_offset];
                const uint8_t v_value = src_vu[sample.vu_offset];
                const uint8_t u_value = src_vu[sample.vu_offset + 1U];
                const int y_scaled = kYuvYTable[y_value];
                const uint8_t r =
                    ClampToByte((y_scaled + kYuvVToRTable[v_value] + 128) >>
                                8);
                const uint8_t g =
                    ClampToByte((y_scaled + kYuvUToGTable[u_value] +
                                 kYuvVToGTable[v_value] + 128) >>
                                8);
                const uint8_t b =
                    ClampToByte((y_scaled + kYuvUToBTable[u_value] + 128) >>
                                8);
                dst_b[dst_offset + x] = b;
                dst_g[dst_offset + x] = g;
                dst_r[dst_offset + x] = r;
            }
        }

        const HI_U32 flush_size =
            dst_blob.u32Stride * dst_height * dst_blob.unShape.stWhc.u32Chn;
        return HI_MPI_SYS_MmzFlushCache(dst_blob.u64PhyAddr, dst,
                                        flush_size) == HI_SUCCESS;
    }

    bool TryWriteU8C3TensorWithVgs(const hisisdk::YuvFrame &frame,
                                   uint32_t dst_width,
                                   uint32_t dst_height) {
        if (!CanUseVgsScale(frame, dst_width, dst_height) ||
            !EnsureScaledYvuFrame(dst_width, dst_height) ||
            !ScaleFrameWithVgs(frame, &scaled_yvu_frame_)) {
            return false;
        }
        const VIDEO_FRAME_S &scaled = scaled_yvu_frame_.frame_info.stVFrame;
        const uint32_t scaled_size = scaled_yvu_frame_.size;
        const uint32_t y_size = scaled.u32Stride[0] * scaled.u32Height;
        if (!IsValidYvu420FrameRange(scaled.u32Stride[0], scaled.u32Stride[1],
                                     scaled.u32Width, scaled.u32Height,
                                     scaled_size)) {
            return false;
        }
        if (TryWriteU8C3TensorWithIveCsc(frame, scaled_yvu_frame_)) {
            return true;
        }

        SVP_SRC_BLOB_S &dst_blob = *input_tensor_;
        uint8_t *dst =
            static_cast<uint8_t *>(VirAddrToPointer(dst_blob.u64VirAddr));
        if (dst == nullptr) {
            return false;
        }
        const uint8_t *src_y =
            static_cast<const uint8_t *>(scaled_yvu_frame_.vir_addr);
        const uint8_t *src_vu = src_y + y_size;
        const uint32_t channel_size = dst_blob.u32Stride * dst_height;
        uint8_t *dst_b = dst;
        uint8_t *dst_g = dst + channel_size;
        uint8_t *dst_r = dst + channel_size * 2U;

        for (uint32_t y = 0; y < dst_height; ++y) {
            const uint32_t dst_offset = y * dst_blob.u32Stride;
            const uint32_t y_offset = y * scaled.u32Stride[0];
            const uint32_t vu_offset = (y / 2U) * scaled.u32Stride[1];
            for (uint32_t x = 0; x < dst_width; ++x) {
                const uint32_t chroma_x = (x / 2U) * 2U;
                const uint8_t y_value = src_y[y_offset + x];
                const uint8_t v_value = src_vu[vu_offset + chroma_x];
                const uint8_t u_value = src_vu[vu_offset + chroma_x + 1U];
                const int y_scaled = kYuvYTable[y_value];
                const uint8_t r =
                    ClampToByte((y_scaled + kYuvVToRTable[v_value] + 128) >>
                                8);
                const uint8_t g =
                    ClampToByte((y_scaled + kYuvUToGTable[u_value] +
                                 kYuvVToGTable[v_value] + 128) >>
                                8);
                const uint8_t b =
                    ClampToByte((y_scaled + kYuvUToBTable[u_value] + 128) >>
                                8);
                dst_b[dst_offset + x] = b;
                dst_g[dst_offset + x] = g;
                dst_r[dst_offset + x] = r;
            }
        }

        const HI_U32 flush_size =
            dst_blob.u32Stride * dst_height * dst_blob.unShape.stWhc.u32Chn;
        return HI_MPI_SYS_MmzFlushCache(dst_blob.u64PhyAddr, dst,
                                        flush_size) == HI_SUCCESS;
    }

    bool TryWriteU8C3TensorWithIveCsc(
        const hisisdk::YuvFrame &frame, const ScaledYvuFrame &scaled_frame) {
        const VIDEO_FRAME_S &scaled = scaled_frame.frame_info.stVFrame;
        if (scaled.u32Width < kIveCscMinWidth ||
            scaled.u32Height < kIveCscMinHeight ||
            !EnsureIveRgbFrame(scaled.u32Width, scaled.u32Height)) {
            return false;
        }
        IVE_IMAGE_S src = MakeIveYvu420spImage(scaled_frame);
        IVE_CSC_CTRL_S ctrl{};
        ctrl.enMode = CscModeForFrame(frame);
        IVE_HANDLE handle = 0;
        HI_S32 ret =
            HI_MPI_IVE_CSC(&handle, &src, &ive_rgb_frame_.image, &ctrl,
                           HI_TRUE);
        if (ret != HI_SUCCESS) {
            return false;
        }
        if (!QueryIveTask(handle)) {
            return false;
        }
        return CopyIveRgbToBgrTensor(ive_rgb_frame_.image);
    }

    bool EnsureIveRgbFrame(uint32_t width, uint32_t height) {
        if (ive_rgb_frame_.phy_addr != 0 &&
            ive_rgb_frame_.vir_addr != nullptr &&
            ive_rgb_frame_.width == width &&
            ive_rgb_frame_.height == height) {
            return true;
        }
        FreeIveRgbFrame();

        const uint32_t stride = AlignUpU32(width, kIveImageAlign);
        const uint64_t channel_size = static_cast<uint64_t>(stride) * height;
        const uint64_t frame_size = channel_size * 3U;
        if (stride < width || channel_size == 0 || frame_size > kMaxHiU32) {
            return false;
        }

        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(&phy_addr, &vir_addr,
                                         "LIVE_AI_IVE_CSC", nullptr,
                                         static_cast<HI_U32>(frame_size));
        if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
            return false;
        }
        std::memset(vir_addr, 0, static_cast<size_t>(frame_size));

        ive_rgb_frame_.phy_addr = phy_addr;
        ive_rgb_frame_.vir_addr = vir_addr;
        ive_rgb_frame_.size = static_cast<uint32_t>(frame_size);
        ive_rgb_frame_.width = width;
        ive_rgb_frame_.height = height;
        ive_rgb_frame_.stride = stride;
        IVE_IMAGE_S &image = ive_rgb_frame_.image;
        std::memset(&image, 0, sizeof(image));
        image.enType = IVE_IMAGE_TYPE_U8C3_PLANAR;
        image.u32Width = width;
        image.u32Height = height;
        for (uint32_t i = 0; i < 3; ++i) {
            image.au32Stride[i] = stride;
            image.au64PhyAddr[i] = phy_addr + channel_size * i;
            image.au64VirAddr[i] =
                static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr)) +
                channel_size * i;
        }
        return true;
    }

    IVE_IMAGE_S MakeIveYvu420spImage(
        const ScaledYvuFrame &scaled_frame) const {
        const VIDEO_FRAME_S &frame = scaled_frame.frame_info.stVFrame;
        IVE_IMAGE_S image{};
        image.enType = IVE_IMAGE_TYPE_YUV420SP;
        image.u32Width = frame.u32Width;
        image.u32Height = frame.u32Height;
        image.au32Stride[0] = frame.u32Stride[0];
        image.au32Stride[1] = frame.u32Stride[1];
        image.au64PhyAddr[0] = frame.u64PhyAddr[0];
        image.au64PhyAddr[1] = frame.u64PhyAddr[1];
        image.au64VirAddr[0] = frame.u64VirAddr[0];
        image.au64VirAddr[1] = frame.u64VirAddr[1];
        return image;
    }

    IVE_CSC_MODE_E CscModeForFrame(const hisisdk::YuvFrame &frame) const {
        if (frame.mpp_info.color_gamut ==
            static_cast<int32_t>(COLOR_GAMUT_BT601)) {
            return IVE_CSC_MODE_PIC_BT601_YUV2RGB;
        }
        return IVE_CSC_MODE_PIC_BT709_YUV2RGB;
    }

    bool CopyIveRgbToBgrTensor(const IVE_IMAGE_S &rgb) {
        SVP_SRC_BLOB_S &dst_blob = *input_tensor_;
        const uint32_t width = dst_blob.unShape.stWhc.u32Width;
        const uint32_t height = dst_blob.unShape.stWhc.u32Height;
        if (rgb.enType != IVE_IMAGE_TYPE_U8C3_PLANAR ||
            rgb.u32Width != width || rgb.u32Height != height ||
            rgb.au32Stride[0] < width || rgb.au32Stride[1] < width ||
            rgb.au32Stride[2] < width || dst_blob.u32Stride < width ||
            rgb.au64VirAddr[0] == 0 || rgb.au64VirAddr[1] == 0 ||
            rgb.au64VirAddr[2] == 0) {
            return false;
        }

        uint8_t *dst =
            static_cast<uint8_t *>(VirAddrToPointer(dst_blob.u64VirAddr));
        const uint8_t *src_r =
            static_cast<const uint8_t *>(VirAddrToPointer(rgb.au64VirAddr[0]));
        const uint8_t *src_g =
            static_cast<const uint8_t *>(VirAddrToPointer(rgb.au64VirAddr[1]));
        const uint8_t *src_b =
            static_cast<const uint8_t *>(VirAddrToPointer(rgb.au64VirAddr[2]));
        if (dst == nullptr || src_r == nullptr || src_g == nullptr ||
            src_b == nullptr) {
            return false;
        }

        const uint32_t channel_size = dst_blob.u32Stride * height;
        uint8_t *dst_b = dst;
        uint8_t *dst_g = dst + channel_size;
        uint8_t *dst_r = dst + channel_size * 2U;
        for (uint32_t row = 0; row < height; ++row) {
            const uint32_t dst_offset = row * dst_blob.u32Stride;
            std::memcpy(dst_b + dst_offset, src_b + row * rgb.au32Stride[2],
                        width);
            std::memcpy(dst_g + dst_offset, src_g + row * rgb.au32Stride[1],
                        width);
            std::memcpy(dst_r + dst_offset, src_r + row * rgb.au32Stride[0],
                        width);
        }

        const HI_U32 flush_size =
            dst_blob.u32Stride * height * dst_blob.unShape.stWhc.u32Chn;
        return HI_MPI_SYS_MmzFlushCache(dst_blob.u64PhyAddr, dst,
                                        flush_size) == HI_SUCCESS;
    }

    bool CanUseVgsScale(const hisisdk::YuvFrame &frame, uint32_t dst_width,
                        uint32_t dst_height) const {
        const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
        return info.valid &&
               info.pixel_format ==
                   static_cast<int32_t>(PIXEL_FORMAT_YVU_SEMIPLANAR_420) &&
               info.compress_mode ==
                   static_cast<int32_t>(COMPRESS_MODE_NONE) &&
               info.phy_addr[0] != 0 && info.width == frame.width &&
               info.height == frame.height && dst_width % 2U == 0 &&
               dst_height % 2U == 0;
    }

    bool EnsureScaledYvuFrame(uint32_t width, uint32_t height) {
        if (scaled_yvu_frame_.phy_addr != 0 &&
            scaled_yvu_frame_.vir_addr != nullptr &&
            scaled_yvu_frame_.width == width &&
            scaled_yvu_frame_.height == height) {
            return true;
        }
        FreeScaledYvuFrame();

        const uint32_t aligned_height = AlignUpU32(height, 2U);
        const uint32_t stride = AlignUpU32(width, kVgsFrameAlign);
        const uint64_t y_size = static_cast<uint64_t>(stride) * aligned_height;
        const uint64_t frame_size = y_size * 3U / 2U;
        if (stride < width || y_size == 0 || frame_size > kMaxHiU32) {
            return false;
        }

        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(&phy_addr, &vir_addr,
                                         "LIVE_AI_VGS_SCALE", nullptr,
                                         static_cast<HI_U32>(frame_size));
        if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
            return false;
        }
        std::memset(vir_addr, 0, static_cast<size_t>(frame_size));

        scaled_yvu_frame_.phy_addr = phy_addr;
        scaled_yvu_frame_.vir_addr = vir_addr;
        scaled_yvu_frame_.size = static_cast<uint32_t>(frame_size);
        scaled_yvu_frame_.width = width;
        scaled_yvu_frame_.height = height;
        scaled_yvu_frame_.stride = stride;
        VIDEO_FRAME_INFO_S &frame_info = scaled_yvu_frame_.frame_info;
        std::memset(&frame_info, 0, sizeof(frame_info));
        frame_info.enModId = HI_ID_VGS;
        frame_info.u32PoolId = VB_INVALID_POOLID;
        VIDEO_FRAME_S &frame = frame_info.stVFrame;
        frame.u32Width = width;
        frame.u32Height = height;
        frame.enField = VIDEO_FIELD_FRAME;
        frame.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        frame.enVideoFormat = VIDEO_FORMAT_LINEAR;
        frame.enCompressMode = COMPRESS_MODE_NONE;
        frame.enDynamicRange = DYNAMIC_RANGE_SDR8;
        frame.enColorGamut = COLOR_GAMUT_BT709;
        frame.u32Stride[0] = stride;
        frame.u32Stride[1] = stride;
        frame.u32Stride[2] = stride;
        frame.u64PhyAddr[0] = phy_addr;
        frame.u64PhyAddr[1] = phy_addr + y_size;
        frame.u64PhyAddr[2] = frame.u64PhyAddr[1];
        frame.u64VirAddr[0] =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
        frame.u64VirAddr[1] = frame.u64VirAddr[0] + y_size;
        frame.u64VirAddr[2] = frame.u64VirAddr[1];
        return true;
    }

    VIDEO_FRAME_INFO_S MakeInputVideoFrameInfo(
        const hisisdk::YuvFrame &frame) const {
        const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
        VIDEO_FRAME_INFO_S frame_info{};
        frame_info.u32PoolId = info.pool_id;
        frame_info.enModId = static_cast<MOD_ID_E>(info.module_id);
        VIDEO_FRAME_S &video_frame = frame_info.stVFrame;
        video_frame.u32Width = info.width;
        video_frame.u32Height = info.height;
        video_frame.enField = static_cast<VIDEO_FIELD_E>(info.field);
        video_frame.enPixelFormat =
            static_cast<PIXEL_FORMAT_E>(info.pixel_format);
        video_frame.enVideoFormat =
            static_cast<VIDEO_FORMAT_E>(info.video_format);
        video_frame.enCompressMode =
            static_cast<COMPRESS_MODE_E>(info.compress_mode);
        video_frame.enDynamicRange =
            static_cast<DYNAMIC_RANGE_E>(info.dynamic_range);
        video_frame.enColorGamut = static_cast<COLOR_GAMUT_E>(info.color_gamut);
        for (uint32_t i = 0; i < 3; ++i) {
            video_frame.u32Stride[i] = info.stride[i];
            video_frame.u32HeaderStride[i] = info.header_stride[i];
            video_frame.u32ExtStride[i] = info.ext_stride[i];
            video_frame.u64PhyAddr[i] = info.phy_addr[i];
            video_frame.u64VirAddr[i] = info.vir_addr[i];
            video_frame.u64HeaderPhyAddr[i] = info.header_phy_addr[i];
            video_frame.u64HeaderVirAddr[i] = info.header_vir_addr[i];
            video_frame.u64ExtPhyAddr[i] = info.ext_phy_addr[i];
            video_frame.u64ExtVirAddr[i] = info.ext_vir_addr[i];
        }
        video_frame.s16OffsetTop = info.offset_top;
        video_frame.s16OffsetBottom = info.offset_bottom;
        video_frame.s16OffsetLeft = info.offset_left;
        video_frame.s16OffsetRight = info.offset_right;
        video_frame.u32MaxLuminance = info.max_luminance;
        video_frame.u32MinLuminance = info.min_luminance;
        video_frame.u32TimeRef = info.time_ref;
        video_frame.u64PTS = static_cast<HI_U64>(frame.pts_us);
        video_frame.u32FrameFlag = info.frame_flag;
        if (video_frame.u32Stride[1] == 0) {
            video_frame.u32Stride[1] = video_frame.u32Stride[0];
        }
        if (video_frame.u64PhyAddr[1] == 0 &&
            video_frame.u64PhyAddr[0] != 0 &&
            video_frame.u32Stride[0] != 0) {
            video_frame.u64PhyAddr[1] =
                video_frame.u64PhyAddr[0] +
                static_cast<HI_U64>(video_frame.u32Stride[0]) *
                    video_frame.u32Height;
        }
        if (video_frame.u64VirAddr[1] == 0 &&
            video_frame.u64VirAddr[0] != 0 &&
            video_frame.u32Stride[0] != 0) {
            video_frame.u64VirAddr[1] =
                video_frame.u64VirAddr[0] +
                static_cast<HI_U64>(video_frame.u32Stride[0]) *
                    video_frame.u32Height;
        }
        return frame_info;
    }

    bool ScaleFrameWithVgs(const hisisdk::YuvFrame &frame,
                           ScaledYvuFrame *scaled_frame) {
        if (scaled_frame == nullptr || scaled_frame->phy_addr == 0 ||
            scaled_frame->vir_addr == nullptr) {
            return false;
        }
        VGS_HANDLE handle = -1;
        HI_S32 ret = HI_MPI_VGS_BeginJob(&handle);
        if (ret != HI_SUCCESS) {
            return false;
        }

        VGS_TASK_ATTR_S task{};
        task.stImgIn = MakeInputVideoFrameInfo(frame);
        task.stImgOut = scaled_frame->frame_info;
        ret = HI_MPI_VGS_AddScaleTask(handle, &task, VGS_SCLCOEF_NORMAL);
        if (ret != HI_SUCCESS) {
            HI_MPI_VGS_CancelJob(handle);
            return false;
        }
        ret = HI_MPI_VGS_EndJob(handle);
        if (ret != HI_SUCCESS) {
            HI_MPI_VGS_CancelJob(handle);
            return false;
        }
        return true;
    }

    bool EnsureU8C3SampleMap(const hisisdk::YuvFrame &frame,
                             uint32_t dst_width, uint32_t dst_height) {
        const uint64_t sample_size =
            static_cast<uint64_t>(dst_width) * dst_height;
        if (sample_size == 0 ||
            sample_size > static_cast<uint64_t>(0xffffffffU)) {
            return false;
        }
        if (sample_frame_width_ == frame.width &&
            sample_frame_height_ == frame.height &&
            sample_stride_y_ == frame.stride_y &&
            sample_stride_uv_ == frame.stride_uv &&
            sample_dst_width_ == dst_width &&
            sample_dst_height_ == dst_height &&
            u8c3_sample_points_.size() == static_cast<size_t>(sample_size)) {
            return true;
        }

        std::vector<U8C3SamplePoint> sample_points;
        sample_points.resize(static_cast<size_t>(sample_size));
        for (uint32_t y = 0; y < dst_height; ++y) {
            const uint32_t src_y_row =
                static_cast<uint64_t>(y) * frame.height / dst_height;
            const uint32_t src_uv_row = src_y_row / 2U;
            for (uint32_t x = 0; x < dst_width; ++x) {
                const uint32_t src_x =
                    static_cast<uint64_t>(x) * frame.width / dst_width;
                const uint32_t chroma_x = (src_x / 2U) * 2U;
                U8C3SamplePoint sample;
                sample.y_offset = src_y_row * frame.stride_y + src_x;
                sample.vu_offset = src_uv_row * frame.stride_uv + chroma_x;
                sample_points[static_cast<size_t>(y) * dst_width + x] =
                    sample;
            }
        }

        u8c3_sample_points_.swap(sample_points);
        sample_frame_width_ = frame.width;
        sample_frame_height_ = frame.height;
        sample_stride_y_ = frame.stride_y;
        sample_stride_uv_ = frame.stride_uv;
        sample_dst_width_ = dst_width;
        sample_dst_height_ = dst_height;
        return true;
    }

    void ClearU8C3SampleMap() {
        u8c3_sample_points_.clear();
        sample_frame_width_ = 0;
        sample_frame_height_ = 0;
        sample_stride_y_ = 0;
        sample_stride_uv_ = 0;
        sample_dst_width_ = 0;
        sample_dst_height_ = 0;
    }

    SVP_SRC_BLOB_S *input_tensor_ = nullptr;
    ScaledYvuFrame scaled_yvu_frame_;
    IveRgbFrame ive_rgb_frame_;
    std::vector<U8C3SamplePoint> u8c3_sample_points_;
    uint32_t sample_frame_width_ = 0;
    uint32_t sample_frame_height_ = 0;
    uint32_t sample_stride_y_ = 0;
    uint32_t sample_stride_uv_ = 0;
    uint32_t sample_dst_width_ = 0;
    uint32_t sample_dst_height_ = 0;
#endif
};

NnieInputWriter::NnieInputWriter() : impl_(new Impl()) {}

NnieInputWriter::~NnieInputWriter() {
    Release();
    delete impl_;
}

#if LIVE_STREAM_HAS_HISI_NNIE
bool NnieInputWriter::Write(const hisisdk::YuvFrame &frame,
                            const AiModelConfig &config,
                            SVP_SRC_BLOB_S *input_tensor) {
    return impl_->Write(frame, config, input_tensor);
}
#else
bool NnieInputWriter::Write(const hisisdk::YuvFrame &frame,
                            const AiModelConfig &config,
                            void *input_tensor) {
    (void)frame;
    (void)config;
    (void)input_tensor;
    return false;
}
#endif

void NnieInputWriter::Release() {
#if LIVE_STREAM_HAS_HISI_NNIE
    impl_->FreeScaledYvuFrame();
    impl_->FreeIveRgbFrame();
    impl_->ClearU8C3SampleMap();
#endif
}

}  // namespace ai_internal
}  // namespace live_stream

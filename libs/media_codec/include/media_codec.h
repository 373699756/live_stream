#ifndef LIVE_STREAM_MEDIA_CODEC_MEDIA_CODEC_H_
#define LIVE_STREAM_MEDIA_CODEC_MEDIA_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {
namespace media_codec {

// 本模块只处理已经编码完成的视频码流，不做图像解码和重新编码；它负责
// 遍历 AnnexB 字节流、提取 codec 元数据，并构造 FLV/HLS/RTP/SDP
// 需要的小型配置记录。
constexpr size_t kMaxNalUnitsPerFrame = 64;
constexpr uint8_t kH264NalTypeIdr = 5;
constexpr uint8_t kH264NalTypeSps = 7;
constexpr uint8_t kH264NalTypePps = 8;
constexpr uint8_t kH264NalTypeAud = 9;
constexpr uint8_t kH265NalTypeIdrWRadl = 19;
constexpr uint8_t kH265NalTypeIdrNLp = 20;
constexpr uint8_t kH265NalTypeCra = 21;
constexpr uint8_t kH265NalTypeVps = 32;
constexpr uint8_t kH265NalTypeSps = 33;
constexpr uint8_t kH265NalTypePps = 34;
constexpr uint8_t kH265NalTypeAud = 35;

struct H264NalUnit {
    // 去掉 0x000001/0x00000001 起始码后的 H.264 NAL 视图。
    // data 指向输入 EncodedFrame 的 payload；释放帧 owner 后不能继续保存该指针。
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint8_t type = 0;
};

struct H265NalUnit {
    // 生命周期约定同 H264NalUnit。type 是 HEVC 6 bit nal_unit_type。
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint8_t type = 0;
};

struct H264NalUnitList {
    H264NalUnit units[kMaxNalUnitsPerFrame];
    size_t count = 0;
    // overflow 表示单帧 NAL 数超过热路径固定数组容量。调用方应把它当作
    // 解析风险处理，不能静默丢弃尾部 NAL。
    bool overflow = false;

    const H264NalUnit *begin() const { return units; }
    const H264NalUnit *end() const { return units + count; }
    bool empty() const { return count == 0; }
    bool Add(const H264NalUnit &unit);
};

struct H265NalUnitList {
    H265NalUnit units[kMaxNalUnitsPerFrame];
    size_t count = 0;
    // overflow 的处理原则同 H264NalUnitList：容量不够时整帧解析失败。
    bool overflow = false;

    const H265NalUnit *begin() const { return units; }
    const H265NalUnit *end() const { return units + count; }
    bool empty() const { return count == 0; }
    bool Add(const H265NalUnit &unit);
};

struct H264ParameterSets {
    // SPS/PPS 需要拷贝出来，因为下游 sequence header 和 SDP 可能晚于
    // 承载它们的原始帧继续存在。
    std::string sps;
    std::string pps;

    bool complete() const { return !sps.empty() && !pps.empty(); }
};

struct H265ParameterSets {
    // HEVC 除 SPS/PPS 外还需要 VPS，三者齐全后浏览器和协议头才算 ready。
    std::string vps;
    std::string sps;
    std::string pps;

    bool complete() const {
        return !vps.empty() && !sps.empty() && !pps.empty();
    }
};

struct AnnexBNalUnit {
    // 共用 AnnexB 扫描器产出的通用 NAL 视图。h264_type/h265_type 都从
    // 同一段 NAL 头部字节解析，便于 H.264/H.265 parser 复用一次起始码遍历。
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint8_t h264_type = 0;
    uint8_t h265_type = 0;
};

class IAnnexBNalUnitSink {
public:
    virtual ~IAnnexBNalUnitSink() = default;
    // last 表示当前 NAL 是本帧最后一个 NAL；sink 返回 false 会中止遍历。
    virtual bool OnAnnexBNalUnit(const AnnexBNalUnit &unit,
                                 bool last) = 0;
};

bool IsH264ParameterSetNal(uint8_t nal_type);
bool IsH265ParameterSetNal(uint8_t nal_type);
bool IsH264IdrNal(uint8_t nal_type);
bool IsH265IdrNal(uint8_t nal_type);

void StripAnnexBStartCode(const uint8_t **payload, size_t *size);

// 遍历一帧 AnnexB payload，向 sink 输出不含起始码的非空 NAL 视图。
// 该函数不复制 payload，调用方必须保证输入缓冲区在 sink 使用期间有效。
bool ForEachAnnexBNalUnit(const uint8_t *data,
                          size_t size,
                          IAnnexBNalUnitSink *sink);

// 解析结果只保存 NAL 视图，不拥有媒体 payload；参数集持久化需使用
// Extract*ParameterSets 单独拷贝。
bool ParseH264AnnexBNalUnits(const uint8_t *data, size_t size,
                             H264NalUnitList *units);

bool ParseH265AnnexBNalUnits(const uint8_t *data, size_t size,
                             H265NalUnitList *units);

bool HasH264ParameterSets(const H264NalUnitList &units);

bool HasH265ParameterSets(const H265NalUnitList &units);

bool HasCompleteH264ParameterSets(const H264NalUnitList &units);

bool HasCompleteH265ParameterSets(const H265NalUnitList &units);

bool HasH264KeyFrame(const H264NalUnitList &units);

bool HasH265KeyFrame(const H265NalUnitList &units);

H264ParameterSets ExtractH264ParameterSets(const H264NalUnitList &units);

H265ParameterSets ExtractH265ParameterSets(const H265NalUnitList &units);

void ExtractH264ParameterSets(const H264NalUnitList &units, std::string *sps,
                              std::string *pps, bool *has_sps,
                              bool *has_pps);

void ExtractH265ParameterSets(const H265NalUnitList &units, std::string *vps,
                              std::string *sps, std::string *pps,
                              bool *has_vps, bool *has_sps, bool *has_pps);

bool WriteNalLengthPrefix(size_t nal_size, uint8_t *out);

// 把一个不带 AnnexB 起始码的 NAL 写成 AVCC/HVCC 常用的
// 4 字节 big-endian length prefix + NAL payload。
bool AppendLengthPrefixedNal(const uint8_t *data,
                             size_t size,
                             std::string *out);

// 构造 AVCDecoderConfigurationRecord，用于 FLV sequence header、SDP/MP4
// 类似的低频 codec 配置输出。
bool BuildH264AvccRecord(const std::string &sps,
                         const std::string &pps,
                         std::string *record);

// 构造 HEVCDecoderConfigurationRecord。这里只输出浏览器播放链路需要的
// VPS/SPS/PPS 数组，不在本模块保存 codec 状态。
bool BuildH265HvccRecord(const std::string &vps,
                         const std::string &sps,
                         const std::string &pps,
                         std::string *record);

}  // namespace media_codec
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_CODEC_MEDIA_CODEC_H_

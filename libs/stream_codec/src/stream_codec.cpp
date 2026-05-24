#include "stream_codec.h"

#include "byte_writer.h"

#include <string>

namespace live_stream {
namespace stream_codec {
namespace {

// Codec payload notes:
// 码流转换要点：
// - 编码器输出一般是 Annex-B：每个 NAL 前面带 00 00 01 或 00 00 00 01
//   起始码。
// - HLS/TS 仍然需要 Annex-B，所以这里会重组为 AUD + 参数集 + 图像 NAL。
// - HTTP-FLV 不要 Annex-B 起始码，而是每个 NAL 前写 4 字节长度前缀；
//   SPS/PPS/VPS 放进 sequence header，普通视频 tag 只放图像 NAL。
//
// Annex-B:
// - H.264/H.265 elementary streams normally use start codes such as
//   00 00 00 01 to separate NAL units.
// - MPEG-TS carries video access units in Annex-B form, so HLS segment output
//   must rebuild access units as AUD + optional parameter sets + frame NALs.
// - FLV does not carry Annex-B start codes in video samples. FLV samples use
//   a 4-byte big-endian length before each NAL, so Build*Sample() strips AUD
//   and parameter sets and writes length-prefixed frame NALs.
//
// Parameter sets:
// - H.264 uses SPS/PPS. NAL type 5 is IDR, 7 is SPS, 8 is PPS, and 9 is AUD.
// - H.265 uses VPS/SPS/PPS. NAL types 19/20/21 are random-access pictures,
//   32 is VPS, 33 is SPS, 34 is PPS, and 35 is AUD.
// - Key frames sent to HLS should include cached parameter sets when the
//   source frame does not already carry them.
//
// FLV samples:
// - FLV sequence headers carry avcC/hvcC records. Those records hold cached
//   SPS/PPS or VPS/SPS/PPS and tell the player that later video samples use
//   4-byte NAL length prefixes.
// - FLV video samples are length-prefixed NAL units, so parameter sets and AUD
//   are stripped from the sample payload.

using byte_writer::AppendBytes;
using byte_writer::AppendU32;

void AppendStartCode(std::string *out) {
    // Annex-B separates NAL units with the 4-byte start code 00 00 00 01.
    AppendU32(out, 1);
}

void AppendH264Aud(std::string *out) {
    static constexpr uint8_t kAud[] = {0x09, 0xf0};
    AppendStartCode(out);
    AppendBytes(out, kAud, sizeof(kAud));
}

void AppendH265Aud(std::string *out) {
    // Access Unit Delimiter for H.265: NAL type 35 followed by pic_type.
    static constexpr uint8_t kAud[] = {0x46, 0x01, 0x50};
    AppendStartCode(out);
    AppendBytes(out, kAud, sizeof(kAud));
}

size_t FindStartCode(const uint8_t *data, size_t size, size_t offset) {
    if (data == nullptr || size < 3 || offset >= size) {
        return std::string::npos;
    }
    for (size_t i = offset; i + 3 <= size; ++i) {
        if (data[i] != 0 || data[i + 1] != 0) {
            continue;
        }
        if (data[i + 2] == 1) {
            return i;
        }
        if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
            return i;
        }
    }
    return std::string::npos;
}

template <typename Units, typename Predicate>
bool AnyNalUnit(const Units &units, Predicate predicate) {
    for (const auto &unit : units) {
        if (predicate(unit)) {
            return true;
        }
    }
    return false;
}

template <typename Units>
bool HasCompleteH264ParameterSetsInUnits(const Units &units) {
    bool has_sps = false;
    bool has_pps = false;
    for (const auto &unit : units) {
        if (unit.type == 7) {
            has_sps = true;
        } else if (unit.type == 8) {
            has_pps = true;
        }
        if (has_sps && has_pps) {
            return true;
        }
    }
    return false;
}

template <typename Units>
bool HasCompleteH265ParameterSetsInUnits(const Units &units) {
    bool has_vps = false;
    bool has_sps = false;
    bool has_pps = false;
    for (const auto &unit : units) {
        if (unit.type == 32) {
            has_vps = true;
        } else if (unit.type == 33) {
            has_sps = true;
        } else if (unit.type == 34) {
            has_pps = true;
        }
        if (has_vps && has_sps && has_pps) {
            return true;
        }
    }
    return false;
}

void AssignNalPayload(const uint8_t *data, size_t size, std::string *out) {
    if (out != nullptr) {
        out->assign(reinterpret_cast<const char *>(data), size);
    }
}

template <typename Units>
void ExtractH264ParameterSetsFromUnits(const Units &units,
                                       std::string *sps,
                                       std::string *pps,
                                       bool *has_sps,
                                       bool *has_pps) {
    bool local_has_sps = false;
    bool local_has_pps = false;
    for (const auto &unit : units) {
        if (unit.type == 7) {
            AssignNalPayload(unit.data, unit.size, sps);
            local_has_sps = true;
        } else if (unit.type == 8) {
            AssignNalPayload(unit.data, unit.size, pps);
            local_has_pps = true;
        }
    }
    if (has_sps != nullptr) {
        *has_sps = local_has_sps;
    }
    if (has_pps != nullptr) {
        *has_pps = local_has_pps;
    }
}

template <typename Units>
void ExtractH265ParameterSetsFromUnits(const Units &units,
                                       std::string *vps,
                                       std::string *sps,
                                       std::string *pps,
                                       bool *has_vps,
                                       bool *has_sps,
                                       bool *has_pps) {
    bool local_has_vps = false;
    bool local_has_sps = false;
    bool local_has_pps = false;
    for (const auto &unit : units) {
        if (unit.type == 32) {
            AssignNalPayload(unit.data, unit.size, vps);
            local_has_vps = true;
        } else if (unit.type == 33) {
            AssignNalPayload(unit.data, unit.size, sps);
            local_has_sps = true;
        } else if (unit.type == 34) {
            AssignNalPayload(unit.data, unit.size, pps);
            local_has_pps = true;
        }
    }
    if (has_vps != nullptr) {
        *has_vps = local_has_vps;
    }
    if (has_sps != nullptr) {
        *has_sps = local_has_sps;
    }
    if (has_pps != nullptr) {
        *has_pps = local_has_pps;
    }
}

template <typename Units>
std::string BuildH264AnnexBAccessUnitFromUnits(const Units &units,
                                               const std::string &sps,
                                               const std::string &pps,
                                               bool prepend_parameter_sets) {
    std::string access_unit;
    AppendH264Aud(&access_unit);
    if (prepend_parameter_sets && !sps.empty() && !pps.empty()) {
        AppendStartCode(&access_unit);
        access_unit.append(sps);
        AppendStartCode(&access_unit);
        access_unit.append(pps);
    }
    for (const auto &unit : units) {
        if (unit.type == 9) {
            continue;
        }
        AppendStartCode(&access_unit);
        access_unit.append(reinterpret_cast<const char *>(unit.data), unit.size);
    }
    return access_unit;
}

template <typename Units>
std::string BuildH265AnnexBAccessUnitFromUnits(const Units &units,
                                               const std::string &vps,
                                               const std::string &sps,
                                               const std::string &pps,
                                               bool prepend_parameter_sets) {
    std::string access_unit;
    AppendH265Aud(&access_unit);
    if (prepend_parameter_sets && !vps.empty() && !sps.empty() && !pps.empty()) {
        AppendStartCode(&access_unit);
        access_unit.append(vps);
        AppendStartCode(&access_unit);
        access_unit.append(sps);
        AppendStartCode(&access_unit);
        access_unit.append(pps);
    }
    for (const auto &unit : units) {
        if (unit.type == 35) {
            continue;
        }
        AppendStartCode(&access_unit);
        access_unit.append(reinterpret_cast<const char *>(unit.data), unit.size);
    }
    return access_unit;
}

}  // namespace

bool IsKeyFrame(FrameType frame_type) {
    return frame_type == FrameType::kIdr || frame_type == FrameType::kI;
}

bool H264NalUnitList::Add(const H264NalUnit &unit) {
    if (count >= kMaxNalUnitsPerFrame) {
        overflow = true;
        return false;
    }
    units[count++] = unit;
    return true;
}

bool H265NalUnitList::Add(const H265NalUnit &unit) {
    if (count >= kMaxNalUnitsPerFrame) {
        overflow = true;
        return false;
    }
    units[count++] = unit;
    return true;
}

bool ForEachAnnexBNalUnit(const uint8_t *data,
                          size_t size,
                          IAnnexBNalUnitSink *sink) {
    if (sink == nullptr) {
        return false;
    }
    size_t offset = 0;
    while (true) {
        const size_t start = FindStartCode(data, size, offset);
        if (start == std::string::npos) {
            break;
        }
        const size_t prefix =
            start + 3 < size && data[start + 2] == 0 && data[start + 3] == 1 ? 4
                                                                             : 3;
        const size_t nal_begin = start + prefix;
        const size_t next = FindStartCode(data, size, nal_begin);
        size_t nal_end = next == std::string::npos ? size : next;
        while (nal_end > nal_begin && data[nal_end - 1] == 0) {
            --nal_end;
        }
        if (nal_end > nal_begin) {
            AnnexBNalUnit unit;
            unit.data = data + nal_begin;
            unit.size = nal_end - nal_begin;
            unit.h264_type = static_cast<uint8_t>(data[nal_begin] & 0x1f);
            if (unit.size > 1) {
                unit.h265_type =
                    static_cast<uint8_t>((data[nal_begin] >> 1) & 0x3f);
            }
            if (!sink->OnAnnexBNalUnit(unit, next == std::string::npos)) {
                return false;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        offset = next;
    }
    return true;
}

void StripAnnexBStartCode(const uint8_t **payload, size_t *size) {
    if (payload == nullptr || *payload == nullptr || size == nullptr) {
        return;
    }
    if (*size >= 4 && (*payload)[0] == 0 && (*payload)[1] == 0 &&
        (*payload)[2] == 0 && (*payload)[3] == 1) {
        *payload += 4;
        *size -= 4;
        return;
    }
    if (*size >= 3 && (*payload)[0] == 0 && (*payload)[1] == 0 &&
        (*payload)[2] == 1) {
        *payload += 3;
        *size -= 3;
    }
}

bool ParseH264AnnexBNalUnits(const uint8_t *data, size_t size,
                             H264NalUnitList *units) {
    if (units == nullptr) {
        return false;
    }
    *units = H264NalUnitList{};
    class H264ListBuilder final : public IAnnexBNalUnitSink {
    public:
        explicit H264ListBuilder(H264NalUnitList *units) : units_(units) {}

        bool OnAnnexBNalUnit(const AnnexBNalUnit &unit, bool last) override {
            (void)last;
            return units_->Add({unit.data, unit.size, unit.h264_type});
        }

    private:
        H264NalUnitList *units_ = nullptr;
    } builder(units);
    return ForEachAnnexBNalUnit(data, size, &builder) && !units->overflow;
}

bool ParseH265AnnexBNalUnits(const uint8_t *data, size_t size,
                             H265NalUnitList *units) {
    if (units == nullptr) {
        return false;
    }
    *units = H265NalUnitList{};
    class H265ListBuilder final : public IAnnexBNalUnitSink {
    public:
        explicit H265ListBuilder(H265NalUnitList *units) : units_(units) {}

        bool OnAnnexBNalUnit(const AnnexBNalUnit &unit, bool last) override {
            (void)last;
            if (unit.size <= 1) {
                return true;
            }
            return units_->Add({unit.data, unit.size, unit.h265_type});
        }

    private:
        H265NalUnitList *units_ = nullptr;
    } builder(units);
    return ForEachAnnexBNalUnit(data, size, &builder) && !units->overflow;
}

bool HasH264ParameterSets(const H264NalUnitList &units) {
    return AnyNalUnit(units, [](const H264NalUnit &unit) {
        return unit.type == 7 || unit.type == 8;
    });
}

bool HasH265ParameterSets(const H265NalUnitList &units) {
    return AnyNalUnit(units, [](const H265NalUnit &unit) {
        return unit.type == 32 || unit.type == 33 || unit.type == 34;
    });
}

bool HasCompleteH264ParameterSets(const H264NalUnitList &units) {
    return HasCompleteH264ParameterSetsInUnits(units);
}

bool HasCompleteH265ParameterSets(const H265NalUnitList &units) {
    return HasCompleteH265ParameterSetsInUnits(units);
}

bool HasH264KeyFrame(const H264NalUnitList &units) {
    return AnyNalUnit(units, [](const H264NalUnit &unit) {
        return unit.type == 5;
    });
}

bool HasH265KeyFrame(const H265NalUnitList &units) {
    return AnyNalUnit(units, [](const H265NalUnit &unit) {
        return unit.type == 19 || unit.type == 20 || unit.type == 21;
    });
}

void ExtractH264ParameterSets(const H264NalUnitList &units, std::string *sps,
                              std::string *pps, bool *has_sps,
                              bool *has_pps) {
    ExtractH264ParameterSetsFromUnits(units, sps, pps, has_sps, has_pps);
}

void ExtractH265ParameterSets(const H265NalUnitList &units, std::string *vps,
                              std::string *sps, std::string *pps,
                              bool *has_vps, bool *has_sps, bool *has_pps) {
    ExtractH265ParameterSetsFromUnits(units, vps, sps, pps, has_vps, has_sps,
                                      has_pps);
}

std::string BuildH264AnnexBAccessUnit(const H264NalUnitList &units,
                                      const std::string &sps,
                                      const std::string &pps,
                                      bool prepend_parameter_sets) {
    return BuildH264AnnexBAccessUnitFromUnits(units, sps, pps,
                                              prepend_parameter_sets);
}

std::string BuildH265AnnexBAccessUnit(const H265NalUnitList &units,
                                      const std::string &vps,
                                      const std::string &sps,
                                      const std::string &pps,
                                      bool prepend_parameter_sets) {
    return BuildH265AnnexBAccessUnitFromUnits(units, vps, sps, pps,
                                              prepend_parameter_sets);
}

}  // namespace stream_codec
}  // namespace live_stream

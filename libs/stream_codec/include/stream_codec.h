#ifndef LIVE_STREAM_STREAM_CODEC_H_
#define LIVE_STREAM_STREAM_CODEC_H_

#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {
namespace stream_codec {

constexpr size_t kMaxNalUnitsPerFrame = 64;

struct H264NalUnit {
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint8_t type = 0;
};

struct H265NalUnit {
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint8_t type = 0;
};

struct H264NalUnitList {
    H264NalUnit units[kMaxNalUnitsPerFrame];
    size_t count = 0;
    bool overflow = false;

    const H264NalUnit *begin() const { return units; }
    const H264NalUnit *end() const { return units + count; }
    bool empty() const { return count == 0; }
    bool Add(const H264NalUnit &unit);
};

struct H265NalUnitList {
    H265NalUnit units[kMaxNalUnitsPerFrame];
    size_t count = 0;
    bool overflow = false;

    const H265NalUnit *begin() const { return units; }
    const H265NalUnit *end() const { return units + count; }
    bool empty() const { return count == 0; }
    bool Add(const H265NalUnit &unit);
};

struct AnnexBNalUnit {
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint8_t h264_type = 0;
    uint8_t h265_type = 0;
};

class IAnnexBNalUnitSink {
public:
    virtual ~IAnnexBNalUnitSink() = default;
    virtual bool OnAnnexBNalUnit(const AnnexBNalUnit &unit,
                                 bool last) = 0;
};

bool IsKeyFrame(FrameType frame_type);

void StripAnnexBStartCode(const uint8_t **payload, size_t *size);

bool ForEachAnnexBNalUnit(const uint8_t *data,
                          size_t size,
                          IAnnexBNalUnitSink *sink);

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

void ExtractH264ParameterSets(const H264NalUnitList &units, std::string *sps,
                              std::string *pps, bool *has_sps,
                              bool *has_pps);

void ExtractH265ParameterSets(const H265NalUnitList &units, std::string *vps,
                              std::string *sps, std::string *pps,
                              bool *has_vps, bool *has_sps, bool *has_pps);

std::string BuildH264AnnexBAccessUnit(const H264NalUnitList &units,
                                      const std::string &sps,
                                      const std::string &pps,
                                      bool prepend_parameter_sets);

std::string BuildH265AnnexBAccessUnit(const H265NalUnitList &units,
                                      const std::string &vps,
                                      const std::string &sps,
                                      const std::string &pps,
                                      bool prepend_parameter_sets);

}  // namespace stream_codec
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_CODEC_H_

#ifndef LIVE_STREAM_STREAM_CODEC_H_
#define LIVE_STREAM_STREAM_CODEC_H_

#include "media/encoded_frame.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

bool IsKeyFrame(FrameType frame_type);

void StripAnnexBStartCode(const uint8_t **payload, size_t *size);

bool ParseH264AnnexBNalUnits(const uint8_t *data, size_t size,
                             H264NalUnitList *units);

bool ParseH265AnnexBNalUnits(const uint8_t *data, size_t size,
                             H265NalUnitList *units);

std::vector<H264NalUnit> ParseH264AnnexBNalUnits(const uint8_t *data,
                                                 size_t size);

std::vector<H265NalUnit> ParseH265AnnexBNalUnits(const uint8_t *data,
                                                 size_t size);

bool HasH264ParameterSets(const H264NalUnitList &units);

bool HasH265ParameterSets(const H265NalUnitList &units);

bool HasH264ParameterSets(const std::vector<H264NalUnit> &units);

bool HasH265ParameterSets(const std::vector<H265NalUnit> &units);

bool HasH264KeyFrame(const H264NalUnitList &units);

bool HasH265KeyFrame(const H265NalUnitList &units);

bool HasH264KeyFrame(const std::vector<H264NalUnit> &units);

bool HasH265KeyFrame(const std::vector<H265NalUnit> &units);

void ExtractH264ParameterSets(const H264NalUnitList &units, std::string *sps,
                              std::string *pps, bool *has_sps,
                              bool *has_pps);

void ExtractH265ParameterSets(const H265NalUnitList &units, std::string *vps,
                              std::string *sps, std::string *pps,
                              bool *has_vps, bool *has_sps, bool *has_pps);

void ExtractH264ParameterSets(const std::vector<H264NalUnit> &units,
                              std::string *sps,
                              std::string *pps,
                              bool *has_sps,
                              bool *has_pps);

void ExtractH265ParameterSets(const std::vector<H265NalUnit> &units,
                              std::string *vps,
                              std::string *sps,
                              std::string *pps,
                              bool *has_vps,
                              bool *has_sps,
                              bool *has_pps);

std::string BuildH264AvccSample(const H264NalUnitList &units);

std::string BuildH265LengthPrefixedSample(const H265NalUnitList &units);

std::string BuildH264AvccSample(const std::vector<H264NalUnit> &units);

std::string BuildH265LengthPrefixedSample(
    const std::vector<H265NalUnit> &units);

std::string BuildH264AnnexBAccessUnit(const H264NalUnitList &units,
                                      const std::string &sps,
                                      const std::string &pps,
                                      bool prepend_parameter_sets);

std::string BuildH265AnnexBAccessUnit(const H265NalUnitList &units,
                                      const std::string &vps,
                                      const std::string &sps,
                                      const std::string &pps,
                                      bool prepend_parameter_sets);

std::string BuildH264AnnexBAccessUnit(
    const std::vector<H264NalUnit> &units, const std::string &sps,
    const std::string &pps, bool prepend_parameter_sets);

std::string BuildH265AnnexBAccessUnit(
    const std::vector<H265NalUnit> &units, const std::string &vps,
    const std::string &sps, const std::string &pps,
    bool prepend_parameter_sets);

}  // namespace stream_codec
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_CODEC_H_

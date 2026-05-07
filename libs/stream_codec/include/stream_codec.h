#ifndef LIVE_STREAM_STREAM_CODEC_H_
#define LIVE_STREAM_STREAM_CODEC_H_

#include "media/encoded_frame.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace stream_codec {

struct H264NalUnit {
  const uint8_t *data = nullptr;
  size_t size = 0;
  uint8_t type = 0;
};

bool IsKeyFrame(FrameType frame_type);

void StripAnnexBStartCode(const uint8_t **payload, size_t *size);

std::vector<H264NalUnit> ParseH264AnnexBNalUnits(const uint8_t *data,
                                                 size_t size);

bool HasH264ParameterSets(const std::vector<H264NalUnit> &units);

void ExtractH264ParameterSets(const std::vector<H264NalUnit> &units,
                              std::string *sps,
                              std::string *pps,
                              bool *has_sps,
                              bool *has_pps);

std::string BuildH264AvccSample(const std::vector<H264NalUnit> &units);

std::string BuildH264AnnexBAccessUnit(
    const std::vector<H264NalUnit> &units, const std::string &sps,
    const std::string &pps, bool prepend_parameter_sets);

}  // namespace stream_codec
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_CODEC_H_

#include "stream_codec.h"

#include <cstdint>
#include <string>

namespace {

bool StartsWith(const std::string& value, const char* prefix, size_t size) {
  return value.size() >= size && value.compare(0, size, prefix, size) == 0;
}

}  // namespace

int main() {
  const uint8_t h264[] = {
      0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f,
      0x00, 0x00, 0x01, 0x68, 0xce, 0x06,
      0x00, 0x00, 0x01, 0x65, 0x88, 0x84,
  };

  live_stream::stream_codec::H264NalUnitList units;
  if (!live_stream::stream_codec::ParseH264AnnexBNalUnits(
          h264, sizeof(h264), &units)) {
    return 1;
  }
  if (units.count != 3 ||
      !live_stream::stream_codec::HasH264ParameterSets(units) ||
      !live_stream::stream_codec::HasCompleteH264ParameterSets(units) ||
      !live_stream::stream_codec::HasH264KeyFrame(units)) {
    return 2;
  }

  std::string sps;
  std::string pps;
  bool has_sps = false;
  bool has_pps = false;
  live_stream::stream_codec::ExtractH264ParameterSets(
      units, &sps, &pps, &has_sps, &has_pps);
  if (!has_sps || !has_pps || sps.empty() || pps.empty()) {
    return 3;
  }

  std::string sample =
      live_stream::stream_codec::BuildH264AnnexBAccessUnit(
          units, sps, pps, true);
  if (!StartsWith(sample, "\x00\x00\x00\x01\x09\xf0", 6)) {
    return 4;
  }
  if (sample.size() <= sizeof(h264)) {
    return 5;
  }

  const uint8_t h265[] = {
      0x00, 0x00, 0x00, 0x01, 0x46, 0x01, 0x50,
      0x00, 0x00, 0x01, 0x40, 0x01, 0x0c,
      0x00, 0x00, 0x01, 0x42, 0x01, 0x0c,
      0x00, 0x00, 0x01, 0x44, 0x01, 0x0c,
      0x00, 0x00, 0x01, 0x26, 0x01, 0x02,
  };

  live_stream::stream_codec::H265NalUnitList h265_units;
  if (!live_stream::stream_codec::ParseH265AnnexBNalUnits(
          h265, sizeof(h265), &h265_units)) {
    return 6;
  }
  if (h265_units.count != 5 ||
      !live_stream::stream_codec::HasH265ParameterSets(h265_units) ||
      !live_stream::stream_codec::HasCompleteH265ParameterSets(h265_units) ||
      !live_stream::stream_codec::HasH265KeyFrame(h265_units)) {
    return 7;
  }

  std::string vps;
  std::string h265_sps;
  std::string h265_pps;
  bool has_vps = false;
  bool has_h265_sps = false;
  bool has_h265_pps = false;
  live_stream::stream_codec::ExtractH265ParameterSets(
      h265_units, &vps, &h265_sps, &h265_pps, &has_vps, &has_h265_sps,
      &has_h265_pps);
  if (!has_vps || !has_h265_sps || !has_h265_pps || vps.empty() ||
      h265_sps.empty() || h265_pps.empty()) {
    return 8;
  }

  std::string h265_access_unit =
      live_stream::stream_codec::BuildH265AnnexBAccessUnit(
          h265_units, vps, h265_sps, h265_pps, true);
  if (!StartsWith(h265_access_unit, "\x00\x00\x00\x01\x46\x01\x50", 7)) {
    return 9;
  }
  if (h265_access_unit.size() <= sizeof(h265)) {
    return 10;
  }
  return 0;
}

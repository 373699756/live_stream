#include "stream_codec.h"

#include <string>
#include <vector>

namespace live_stream {
namespace stream_codec {
namespace {

void AppendU32(std::string *out, uint32_t value) {
  out->push_back(static_cast<char>((value >> 24) & 0xff));
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

void AppendStartCode(std::string *out) { out->append("\x00\x00\x00\x01", 4); }

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

}  // namespace

bool IsKeyFrame(FrameType frame_type) {
  return frame_type == FrameType::kIdr || frame_type == FrameType::kI;
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

std::vector<H264NalUnit> ParseH264AnnexBNalUnits(const uint8_t *data,
                                                 size_t size) {
  std::vector<H264NalUnit> units;
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
      units.push_back({data + nal_begin, nal_end - nal_begin,
                       static_cast<uint8_t>(data[nal_begin] & 0x1f)});
    }
    if (next == std::string::npos) {
      break;
    }
    offset = next;
  }
  return units;
}

bool HasH264ParameterSets(const std::vector<H264NalUnit> &units) {
  for (const H264NalUnit &unit : units) {
    if (unit.type == 7 || unit.type == 8) {
      return true;
    }
  }
  return false;
}

void ExtractH264ParameterSets(const std::vector<H264NalUnit> &units,
                              std::string *sps,
                              std::string *pps,
                              bool *has_sps,
                              bool *has_pps) {
  bool local_has_sps = false;
  bool local_has_pps = false;
  for (const H264NalUnit &unit : units) {
    if (unit.type == 7) {
      if (sps != nullptr) {
        sps->assign(reinterpret_cast<const char *>(unit.data), unit.size);
      }
      local_has_sps = true;
    } else if (unit.type == 8) {
      if (pps != nullptr) {
        pps->assign(reinterpret_cast<const char *>(unit.data), unit.size);
      }
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

std::string BuildH264AvccSample(const std::vector<H264NalUnit> &units) {
  std::string sample;
  for (const H264NalUnit &unit : units) {
    if (unit.type == 7 || unit.type == 8 || unit.type == 9) {
      continue;
    }
    AppendU32(&sample, static_cast<uint32_t>(unit.size));
    sample.append(reinterpret_cast<const char *>(unit.data), unit.size);
  }
  return sample;
}

std::string BuildH264AnnexBAccessUnit(
    const std::vector<H264NalUnit> &units, const std::string &sps,
    const std::string &pps, bool prepend_parameter_sets) {
  std::string access_unit;
  AppendStartCode(&access_unit);
  access_unit.push_back('\x09');
  access_unit.push_back('\xf0');
  if (prepend_parameter_sets && !sps.empty() && !pps.empty()) {
    AppendStartCode(&access_unit);
    access_unit.append(sps);
    AppendStartCode(&access_unit);
    access_unit.append(pps);
  }
  for (const H264NalUnit &unit : units) {
    if (unit.type == 9) {
      continue;
    }
    AppendStartCode(&access_unit);
    access_unit.append(reinterpret_cast<const char *>(unit.data), unit.size);
  }
  return access_unit;
}

}  // namespace stream_codec
}  // namespace live_stream

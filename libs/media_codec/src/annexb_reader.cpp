#include "media_codec.h"

#include <string>

namespace live_stream {
namespace media_codec {
namespace {

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

}  // namespace media_codec
}  // namespace live_stream

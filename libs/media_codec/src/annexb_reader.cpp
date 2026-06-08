#include "media_codec.h"

#include <string>

namespace live_stream {
namespace media_codec {
namespace {

// AnnexB access unit 由 3 字节或 4 字节起始码分隔。扫描器只返回
// prefix 位置，codec 语义留给调用方解释，这样 H.264/H.265 可复用同一遍历。
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
    if (data == nullptr || size == 0 || sink == nullptr) {
        return false;
    }
    bool emitted_unit = false;
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
        if (nal_begin >= size) {
            return false;
        }
        const size_t next = FindStartCode(data, size, nal_begin);
        size_t nal_end = next == std::string::npos ? size : next;
        // 有些编码器会在下一个起始码前填充 0 字节；这些字节不属于
        // RBSP payload，保留下来会干扰下游 NAL 长度判断。
        while (nal_end > nal_begin && data[nal_end - 1] == 0) {
            --nal_end;
        }
        if (nal_end <= nal_begin) {
            return false;
        }
        AnnexBNalUnit unit;
        unit.data = data + nal_begin;
        unit.size = nal_end - nal_begin;
        // H.264/H.265 的 nal_unit_type 位于不同 bit 段；通用扫描阶段同时
        // 填好两个字段，具体 parser 只取自己关心的类型。
        unit.h264_type = static_cast<uint8_t>(data[nal_begin] & 0x1f);
        if (unit.size > 1) {
            unit.h265_type =
                static_cast<uint8_t>((data[nal_begin] >> 1) & 0x3f);
        }
        emitted_unit = true;
        if (!sink->OnAnnexBNalUnit(unit, next == std::string::npos)) {
            return false;
        }
        if (next == std::string::npos) {
            break;
        }
        offset = next;
    }
    return emitted_unit;
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

#include "../src/media_source_stream_state.h"

#include "media/media_buffer.h"

#include <cstddef>
#include <cstdint>

namespace {

constexpr uint8_t kSyncByte = 0x47;
constexpr uint16_t kPatPid = 0x0000;
constexpr uint16_t kPmtPid = 0x1000;
constexpr uint16_t kVideoPid = 0x0100;
constexpr size_t kTsPacketSize = 188;

uint16_t PacketPid(const uint8_t *packet) {
    return static_cast<uint16_t>(((packet[1] & 0x1f) << 8) | packet[2]);
}

bool PacketPayloadUnitStart(const uint8_t *packet) {
    return (packet[1] & 0x40) != 0;
}

bool PacketHasAdaptation(const uint8_t *packet) {
    return (packet[3] & 0x20) != 0;
}

bool PacketHasPayload(const uint8_t *packet) {
    return (packet[3] & 0x10) != 0;
}

uint8_t PacketContinuity(const uint8_t *packet) {
    return packet[3] & 0x0f;
}

size_t PayloadOffset(const uint8_t *packet) {
    size_t offset = 4;
    if (PacketHasAdaptation(packet)) {
        offset += 1 + packet[4];
    }
    return offset;
}

uint32_t MpegCrc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint32_t>(data[i]) << 24;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x80000000U) != 0U) {
                crc = (crc << 1) ^ 0x04c11db7U;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint32_t ReadU32(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

bool ValidatePsiPacket(const uint8_t *packet, uint16_t expected_pid,
                       uint8_t table_id) {
    if (packet[0] != kSyncByte || PacketPid(packet) != expected_pid ||
        !PacketPayloadUnitStart(packet) || !PacketHasPayload(packet)) {
        return false;
    }
    const size_t payload_offset = PayloadOffset(packet);
    if (payload_offset + 8 >= kTsPacketSize || packet[payload_offset] != 0) {
        return false;
    }
    const uint8_t *section = packet + payload_offset + 1;
    if (section[0] != table_id) {
        return false;
    }
    const uint16_t section_length =
        static_cast<uint16_t>(((section[1] & 0x0f) << 8) | section[2]);
    if (section_length < 4 ||
        payload_offset + 1 + 3 + section_length > kTsPacketSize) {
        return false;
    }
    const size_t section_size = 3 + section_length;
    const uint32_t expected_crc =
        ReadU32(section + section_size - sizeof(uint32_t));
    return MpegCrc32(section, section_size - sizeof(uint32_t)) == expected_crc;
}

bool HasStartCodeNal(const uint8_t *data, size_t size, uint8_t nal_type) {
    static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    for (size_t i = 0; i + sizeof(kStartCode) < size; ++i) {
        if (data[i] == kStartCode[0] && data[i + 1] == kStartCode[1] &&
            data[i + 2] == kStartCode[2] && data[i + 3] == kStartCode[3] &&
            (data[i + 4] & 0x1f) == nal_type) {
            return true;
        }
    }
    return false;
}

bool ValidateVideoPackets(const live_stream::MediaSegmentRef &segment) {
    if (segment.body == nullptr || segment.body->data == nullptr ||
        segment.body->size % kTsPacketSize != 0) {
        return false;
    }
    bool saw_payload_unit_start = false;
    bool saw_pcr = false;
    uint8_t expected_continuity = 0;
    bool have_continuity = false;
    for (size_t offset = kTsPacketSize * 2; offset < segment.body->size;
         offset += kTsPacketSize) {
        const uint8_t *packet = segment.body->data + offset;
        if (packet[0] != kSyncByte || PacketPid(packet) != kVideoPid ||
            !PacketHasPayload(packet)) {
            return false;
        }
        if (have_continuity && PacketContinuity(packet) != expected_continuity) {
            return false;
        }
        expected_continuity =
            static_cast<uint8_t>((PacketContinuity(packet) + 1) & 0x0f);
        have_continuity = true;
        if (PacketPayloadUnitStart(packet)) {
            saw_payload_unit_start = true;
            if (!PacketHasAdaptation(packet) || packet[4] < 7 ||
                (packet[5] & 0x10) == 0) {
                return false;
            }
            saw_pcr = true;
            const size_t payload_offset = PayloadOffset(packet);
            if (payload_offset + 19 > kTsPacketSize ||
                packet[payload_offset] != 0x00 ||
                packet[payload_offset + 1] != 0x00 ||
                packet[payload_offset + 2] != 0x01 ||
                packet[payload_offset + 3] != 0xe0) {
                return false;
            }
        }
    }
    return saw_payload_unit_start && saw_pcr &&
           HasStartCodeNal(segment.body->data, segment.body->size, 7) &&
           HasStartCodeNal(segment.body->data, segment.body->size, 8) &&
           HasStartCodeNal(segment.body->data, segment.body->size, 5);
}

live_stream::EncodedFrame MakeH264Frame(live_stream::FrameBuffer *buffer,
                                        live_stream::FrameType frame_type,
                                        int64_t timestamp_us) {
    live_stream::EncodedFrame frame;
    frame.stream_id = live_stream::StreamId::kMain;
    frame.codec = live_stream::Codec::kH264;
    frame.frame_type = frame_type;
    frame.pts_us = timestamp_us;
    frame.dts_us = timestamp_us;
    frame.buffer = buffer;
    frame.offset = 0;
    frame.size = buffer != nullptr ? buffer->size : 0;
    return frame;
}

bool StoreBytes(live_stream::FrameBuffer *buffer, const uint8_t *data,
                size_t size) {
    if (buffer == nullptr || data == nullptr || size > buffer->capacity) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        buffer->data[i] = data[i];
    }
    return live_stream::FrameBufferSetSize(buffer, static_cast<uint32_t>(size));
}

bool AppendFrame(live_stream::media_source_internal::StreamContext *stream,
                 const uint8_t *data, size_t size,
                 live_stream::FrameType frame_type, int64_t timestamp_us) {
    live_stream::FrameBuffer *buffer =
        live_stream::FrameBufferAlloc(static_cast<uint32_t>(size));
    if (!StoreBytes(buffer, data, size)) {
        live_stream::FrameBufferUnref(buffer);
        return false;
    }
    live_stream::EncodedFrame frame =
        MakeH264Frame(buffer, frame_type, timestamp_us);
    live_stream::media_source_internal::ParsedFramePayload payload;
    live_stream::media_source_internal::ParseFramePayload(frame, &payload);
    const live_stream::media_source_internal::PackagedFrameResult result =
        live_stream::media_source_internal::AppendFrameToStream(
            stream, frame, payload, true, false, 2000, 9);
    live_stream::media_source_internal::ParsedFramePayloadUnref(&payload);
    live_stream::FrameBufferUnref(buffer);
    return result.accepted;
}

}  // namespace

int main() {
    static constexpr uint8_t kH264IdrWithParams[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f,
        0x00, 0x00, 0x01, 0x68, 0xce, 0x06,
        0x00, 0x00, 0x01, 0x65, 0x88, 0x84,
    };
    static constexpr uint8_t kH264PFrame[] = {
        0x00, 0x00, 0x01, 0x41, 0x9a, 0x22,
    };
    static constexpr uint8_t kH264IdrWithoutParams[] = {
        0x00, 0x00, 0x01, 0x65, 0x88, 0x21,
    };

    live_stream::media_source_internal::StreamContext stream;
    stream.state = live_stream::StreamState::kRunning;
    stream.codec = live_stream::Codec::kH264;
    if (!AppendFrame(&stream, kH264IdrWithParams, sizeof(kH264IdrWithParams),
                     live_stream::FrameType::kI, 0) ||
        !AppendFrame(&stream, kH264PFrame, sizeof(kH264PFrame),
                     live_stream::FrameType::kP, 1000000) ||
        !AppendFrame(&stream, kH264IdrWithoutParams,
                     sizeof(kH264IdrWithoutParams), live_stream::FrameType::kI,
                     2500000)) {
        return 1;
    }

    const live_stream::MediaHlsPlaylist playlist =
        live_stream::media_source_internal::BuildHlsPlaylist(stream, 2000, 3);
    if (!playlist.supported || playlist.entries.size() != 1 ||
        playlist.first_cached_sequence != 1 || playlist.last_cached_sequence != 1 ||
        playlist.media_sequence != 1) {
        return 2;
    }

    live_stream::MediaSegmentRef segment =
        live_stream::media_source_internal::FindHlsSegmentRef(stream, 1);
    if (!segment.found || segment.duration_us <= 0 ||
        segment.body == nullptr || segment.body->size < kTsPacketSize * 3 ||
        segment.body->size % kTsPacketSize != 0) {
        live_stream::MediaSegmentRefUnref(&segment);
        return 3;
    }
    const uint8_t *body = segment.body->data;
    if (!ValidatePsiPacket(body, kPatPid, 0x00) ||
        !ValidatePsiPacket(body + kTsPacketSize, kPmtPid, 0x02) ||
        !ValidateVideoPackets(segment)) {
        live_stream::MediaSegmentRefUnref(&segment);
        return 4;
    }
    live_stream::MediaSegmentRefUnref(&segment);

    live_stream::MediaSegmentRef missing =
        live_stream::media_source_internal::FindHlsSegmentRef(stream, 99);
    if (missing.found || stream.hls_maker.MissingSegmentCount() != 1) {
        live_stream::MediaSegmentRefUnref(&missing);
        return 5;
    }
    live_stream::MediaSegmentRefUnref(&missing);
    live_stream::media_source_internal::ClearStreamContext(&stream);
    return 0;
}

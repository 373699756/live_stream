#include "hls_maker.h"

#include "byte_writer.h"
#include "media_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace live_stream {
namespace media_source_internal {
namespace {

constexpr size_t kInitialHlsSegmentBytes = 256 * 1024;
constexpr size_t kMaxHlsSegmentBytes = 4 * 1024 * 1024;
constexpr uint16_t kPatPid = 0x0000;
constexpr uint16_t kPmtPid = 0x1000;
constexpr uint16_t kVideoPid = 0x0100;
constexpr uint8_t kTsPacketSize = 188;
constexpr uint8_t kTsStreamTypeH264 = 0x1b;
constexpr uint8_t kTsStreamTypeH265 = 0x24;
constexpr size_t kMaxTsMediaSlices = 192;

using byte_writer::AppendU8;
using byte_writer::AppendU16;
using byte_writer::AppendU24;
using byte_writer::AppendU32;

uint8_t TsStreamType(VideoCodec codec) {
    return codec == VideoCodec::kH265 ? kTsStreamTypeH265 : kTsStreamTypeH264;
}

void FillBytes(char *target, size_t size, uint8_t value) {
    if (target == nullptr || size == 0) {
        return;
    }
    std::memset(target, value, size);
}

bool AppendTsBytes(TsSegmentBuffer *out, const uint8_t *data, size_t size) {
    if (size == 0) {
        return true;
    }
    if (out == nullptr || data == nullptr || out->size > out->capacity ||
        size > out->capacity - out->size) {
        return false;
    }
    std::memcpy(out->data + out->size, data, size);
    out->size += size;
    return true;
}

bool AppendTsString(TsSegmentBuffer *out, const std::string &data) {
    return data.empty() ||
           AppendTsBytes(out, reinterpret_cast<const uint8_t *>(data.data()),
                         data.size());
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

void AppendPsiSectionLength(std::string *out, uint16_t section_length) {
    AppendU8(out, static_cast<uint8_t>(0xb0 | ((section_length >> 8) & 0x0f)));
    AppendU8(out, static_cast<uint8_t>(section_length & 0xff));
}

void AppendPsiPid(std::string *out, uint16_t pid) {
    AppendU8(out, static_cast<uint8_t>(0xe0 | ((pid >> 8) & 0x1f)));
    AppendU8(out, static_cast<uint8_t>(pid & 0xff));
}

std::string BuildPatPacket(uint8_t *continuity_counter) {
    std::string section;
    // PAT 告诉播放器本 TS 里 PMT 所在 PID；这里每个 segment 起始都重新写入，
    // 方便客户端从任意 segment 独立开始解析。
    AppendU8(&section, 0x00);
    AppendPsiSectionLength(&section, 13);
    AppendU16(&section, 1);
    AppendU8(&section, 0xc1);
    AppendU8(&section, 0);
    AppendU8(&section, 0);
    AppendU16(&section, 1);
    AppendPsiPid(&section, kPmtPid);
    const uint32_t crc = MpegCrc32(
        reinterpret_cast<const uint8_t *>(section.data()), section.size());
    AppendU32(&section, crc);

    std::string packet(kTsPacketSize, '\0');
    FillBytes(&packet[0], packet.size(), 0xff);
    packet[0] = static_cast<char>(0x47);
    packet[1] = static_cast<char>(0x40 | ((kPatPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kPatPid & 0xff);
    packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
    *continuity_counter =
        static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
    packet[4] = static_cast<char>(0);
    std::copy(section.begin(), section.end(), packet.begin() + 5);
    return packet;
}

std::string BuildPmtPacket(VideoCodec codec, uint8_t *continuity_counter) {
    std::string section;
    // PMT 描述 video elementary stream 的 codec 和 PID。当前产品只有视频，
    // 所以 PMT 中只登记一个 video PID。
    AppendU8(&section, 0x02);
    AppendPsiSectionLength(&section, 18);
    AppendU16(&section, 1);
    AppendU8(&section, 0xc1);
    AppendU8(&section, 0);
    AppendU8(&section, 0);
    AppendPsiPid(&section, kVideoPid);
    AppendU16(&section, 0xf000);
    AppendU8(&section, TsStreamType(codec));
    AppendPsiPid(&section, kVideoPid);
    AppendU16(&section, 0xf000);
    const uint32_t crc = MpegCrc32(
        reinterpret_cast<const uint8_t *>(section.data()), section.size());
    AppendU32(&section, crc);

    std::string packet(kTsPacketSize, '\0');
    FillBytes(&packet[0], packet.size(), 0xff);
    packet[0] = static_cast<char>(0x47);
    packet[1] = static_cast<char>(0x40 | ((kPmtPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kPmtPid & 0xff);
    packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
    *continuity_counter =
        static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
    packet[4] = static_cast<char>(0);
    std::copy(section.begin(), section.end(), packet.begin() + 5);
    return packet;
}

void AppendPts(std::string *out, uint8_t prefix, uint64_t value) {
    // PES PTS/DTS 使用 33 bit 90kHz 时间基并带 marker bit，不能直接写微秒值。
    const uint64_t pts = value & 0x1ffffffffULL;
    AppendU8(out, static_cast<uint8_t>(
                      (prefix << 4) | (((pts >> 30) & 0x07) << 1) | 0x01));
    AppendU8(out, static_cast<uint8_t>((pts >> 22) & 0xff));
    AppendU8(out,
             static_cast<uint8_t>((((pts >> 15) & 0x7f) << 1) | 0x01));
    AppendU8(out, static_cast<uint8_t>((pts >> 7) & 0xff));
    AppendU8(out, static_cast<uint8_t>(((pts & 0x7f) << 1) | 0x01));
}

std::string BuildPesHeader(uint64_t pts_90k, uint64_t dts_90k) {
    std::string header;
    AppendU24(&header, 0x000001);
    AppendU8(&header, 0xe0);
    AppendU16(&header, 0);
    AppendU8(&header, 0x80);
    const bool has_dts = pts_90k != dts_90k;
    // 无 B 帧时 PTS==DTS，只写 PTS；存在重排时同时写 PTS 和 DTS。
    AppendU8(&header, has_dts ? 0xc0 : 0x80);
    AppendU8(&header, has_dts ? 10 : 5);
    AppendPts(&header, has_dts ? 0x03 : 0x02, pts_90k);
    if (has_dts) {
        AppendPts(&header, 0x01, dts_90k);
    }
    return header;
}

void WritePcr(char *target, uint64_t pcr_90k) {
    // PCR 放在每个 access unit 的首个 TS packet adaptation field 中，
    // 这里直接使用 DTS 的 90kHz 基准，供播放器恢复时钟。
    const uint64_t base = pcr_90k & 0x1ffffffffULL;
    target[0] = static_cast<char>((base >> 25) & 0xff);
    target[1] = static_cast<char>((base >> 17) & 0xff);
    target[2] = static_cast<char>((base >> 9) & 0xff);
    target[3] = static_cast<char>((base >> 1) & 0xff);
    target[4] = static_cast<char>(((base & 0x01) << 7) | 0x7e);
    target[5] = static_cast<char>(0);
}

struct MediaSliceList {
    struct Slice {
        const uint8_t *data = nullptr;
        size_t size = 0;
    };

    Slice slices[kMaxTsMediaSlices];
    size_t count = 0;
    size_t total_size = 0;

    bool Add(const uint8_t *data, size_t size) {
        if (size == 0) {
            return true;
        }
        if (data == nullptr || count >= kMaxTsMediaSlices ||
            size > std::numeric_limits<size_t>::max() - total_size) {
            return false;
        }
        slices[count].data = data;
        slices[count].size = size;
        total_size += size;
        ++count;
        return true;
    }

    bool AddString(const std::string &data) {
        return data.empty() ||
               Add(reinterpret_cast<const uint8_t *>(data.data()),
                   data.size());
    }
};

size_t CopyPesBytes(const MediaSliceList &pes_slices,
                    size_t offset,
                    size_t size,
                    char *target) {
    if (target == nullptr || size == 0) {
        return 0;
    }
    size_t copied = 0;
    size_t slice_base = 0;
    for (size_t i = 0; i < pes_slices.count && copied < size; ++i) {
        const MediaSliceList::Slice &slice = pes_slices.slices[i];
        if (offset >= slice_base + slice.size) {
            slice_base += slice.size;
            continue;
        }
        const size_t slice_offset =
            offset > slice_base ? offset - slice_base : 0;
        const size_t copy_size =
            std::min(size - copied, slice.size - slice_offset);
        std::memcpy(target + copied, slice.data + slice_offset, copy_size);
        copied += copy_size;
        offset += copy_size;
        slice_base += slice.size;
    }
    return copied;
}

bool AppendTsPayloadToBuffer(const MediaSliceList &pes_slices,
                             uint64_t pcr_90k,
                             uint8_t *continuity_counter,
                             TsSegmentBuffer *out) {
    if (out == nullptr || continuity_counter == nullptr) {
        return false;
    }
    const size_t pes_size = pes_slices.total_size;
    const size_t packet_count = pes_size == 0 ? 0 : (pes_size + 175) / 176;
    if (out->size > out->capacity) {
        return false;
    }
    if (packet_count > (out->capacity - out->size) / kTsPacketSize) {
        return false;
    }

    size_t offset = 0;
    while (offset < pes_size) {
        const bool first_packet = offset == 0;
        const size_t remaining = pes_size - offset;
        size_t payload_size = 0;
        if (first_packet) {
            payload_size = std::min(remaining, static_cast<size_t>(176));
        } else {
            payload_size = std::min(remaining, static_cast<size_t>(184));
            if (payload_size == 183) {
                --payload_size;
            }
        }
        const bool use_adaptation = first_packet || payload_size < 184;
        // 首包需要写 PCR，尾包通常需要 stuffing 对齐到 188 字节 TS packet。
        if (use_adaptation && payload_size == 183) {
            --payload_size;
        }
        const size_t adaptation_total = use_adaptation ? 184 - payload_size : 0;

        char packet[kTsPacketSize];
        FillBytes(packet, kTsPacketSize, 0xff);
        packet[0] = static_cast<char>(0x47);
        packet[1] = static_cast<char>((first_packet ? 0x40 : 0x00) |
                                      ((kVideoPid >> 8) & 0x1f));
        packet[2] = static_cast<char>(kVideoPid & 0xff);
        packet[3] = static_cast<char>((use_adaptation ? 0x30 : 0x10) |
                                      (*continuity_counter & 0x0f));
        *continuity_counter =
            static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);

        size_t packet_offset = 4;
        if (use_adaptation) {
            const size_t adaptation_length = adaptation_total - 1;
            packet[packet_offset++] = static_cast<char>(adaptation_length);
            packet[packet_offset++] = first_packet ? static_cast<char>(0x10)
                                                   : static_cast<char>(0);
            size_t stuffing = adaptation_length - 1;
            if (first_packet) {
                WritePcr(&packet[packet_offset], pcr_90k);
                packet_offset += 6;
                stuffing -= 6;
            }
            for (size_t i = 0; i < stuffing; ++i) {
                packet[packet_offset++] = static_cast<char>(0xff);
            }
        }
        (void)CopyPesBytes(pes_slices, offset, payload_size,
                           packet + packet_offset);
        if (!AppendTsBytes(out, reinterpret_cast<const uint8_t *>(packet),
                           sizeof(packet))) {
            return false;
        }
        offset += payload_size;
    }
    return true;
}

bool AddH264AccessUnitSlices(const media_codec::H264NalUnitList &units,
                             const std::string &sps,
                             const std::string &pps,
                             bool prepend_parameter_sets,
                             MediaSliceList *slices) {
    static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    static constexpr uint8_t kAud[] = {0x09, 0xf0};
    if (slices == nullptr) {
        return false;
    }
    if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
        !slices->Add(kAud, sizeof(kAud))) {
        return false;
    }
    // HLS/TS payload 保持 AnnexB 形式。关键帧缺少参数集时，前置当前缓存的
    // SPS/PPS，保证客户端从 segment 边界开始也能解码。
    if (prepend_parameter_sets && !sps.empty() && !pps.empty()) {
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(sps.data()),
                         sps.size()) ||
            !slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(pps.data()),
                         pps.size())) {
            return false;
        }
    }
    for (const media_codec::H264NalUnit &unit : units) {
        if (unit.type == media_codec::kH264NalTypeAud) {
            continue;
        }
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(unit.data, unit.size)) {
            return false;
        }
    }
    return true;
}

bool AddH265AccessUnitSlices(const media_codec::H265NalUnitList &units,
                             const std::string &vps,
                             const std::string &sps,
                             const std::string &pps,
                             bool prepend_parameter_sets,
                             MediaSliceList *slices) {
    static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    static constexpr uint8_t kAud[] = {0x46, 0x01, 0x50};
    if (slices == nullptr) {
        return false;
    }
    if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
        !slices->Add(kAud, sizeof(kAud))) {
        return false;
    }
    // HEVC segment 边界需要 VPS/SPS/PPS 三类参数集；只在关键帧且当前帧未自带
    // 参数集时前置，避免每帧重复扩大 TS 体积。
    if (prepend_parameter_sets && !vps.empty() && !sps.empty() && !pps.empty()) {
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(vps.data()),
                         vps.size()) ||
            !slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(sps.data()),
                         sps.size()) ||
            !slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(pps.data()),
                         pps.size())) {
            return false;
        }
    }
    for (const media_codec::H265NalUnit &unit : units) {
        if (unit.type == media_codec::kH265NalTypeAud) {
            continue;
        }
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(unit.data, unit.size)) {
            return false;
        }
    }
    return true;
}

bool AppendTsSegmentHeader(VideoCodec codec,
                           TsMuxerState *state,
                           TsSegmentBuffer *segment_body) {
    if (state == nullptr || segment_body == nullptr ||
        segment_body->size > segment_body->capacity ||
        segment_body->capacity - segment_body->size < kTsPacketSize * 2U) {
        return false;
    }
    const std::string pat = BuildPatPacket(&state->pat_continuity);
    const std::string pmt = BuildPmtPacket(codec, &state->pmt_continuity);
    return AppendTsString(segment_body, pat) &&
           AppendTsString(segment_body, pmt);
}

bool AppendH264NalUnitsToTsSegmentBuffer(
    const media_codec::H264NalUnitList &units,
    const std::string &sps,
    const std::string &pps,
    bool prepend_parameter_sets,
    int64_t pts_us,
    int64_t dts_us,
    TsMuxerState *state,
    TsSegmentBuffer *segment_body) {
    if (units.empty() || state == nullptr || segment_body == nullptr) {
        return false;
    }
    const uint64_t pts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, pts_us) * 9 / 100);
    const uint64_t dts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, dts_us) * 9 / 100);
    // MPEG-TS/PES 使用 90kHz 时间基；输入是修正后的微秒时间戳。
    const std::string pes_header = BuildPesHeader(pts_90k, dts_90k);
    MediaSliceList pes_slices;
    if (!pes_slices.AddString(pes_header) ||
        !AddH264AccessUnitSlices(units, sps, pps, prepend_parameter_sets,
                                 &pes_slices)) {
        return false;
    }
    return AppendTsPayloadToBuffer(pes_slices, dts_90k,
                                   &state->video_continuity, segment_body);
}

bool AppendH265NalUnitsToTsSegmentBuffer(
    const media_codec::H265NalUnitList &units,
    const std::string &vps,
    const std::string &sps,
    const std::string &pps,
    bool prepend_parameter_sets,
    int64_t pts_us,
    int64_t dts_us,
    TsMuxerState *state,
    TsSegmentBuffer *segment_body) {
    if (units.empty() || state == nullptr || segment_body == nullptr) {
        return false;
    }
    const uint64_t pts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, pts_us) * 9 / 100);
    const uint64_t dts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, dts_us) * 9 / 100);
    // MPEG-TS/PES 使用 90kHz 时间基；输入是修正后的微秒时间戳。
    const std::string pes_header = BuildPesHeader(pts_90k, dts_90k);
    MediaSliceList pes_slices;
    if (!pes_slices.AddString(pes_header) ||
        !AddH265AccessUnitSlices(units, vps, sps, pps, prepend_parameter_sets,
                                 &pes_slices)) {
        return false;
    }
    return AppendTsPayloadToBuffer(pes_slices, dts_90k,
                                   &state->video_continuity, segment_body);
}

}  // namespace

HlsMaker::~HlsMaker() { Reset(); }

void HlsMaker::Reset() {
    ClearSegments();
    UnrefSegmentState(&current_segment_);
    ts_muxer_state_ = TsMuxerState{};
    next_segment_capacity_ = 0;
    next_segment_sequence_ = 1;
    last_pts_us_ = -1;
    last_frame_duration_us_ = 33333;
    requested_ = false;
}

void HlsMaker::MarkRequested() const { requested_ = true; }

bool HlsMaker::Requested() const { return requested_; }

bool HlsMaker::HasSegments() const { return !segments_.empty(); }

size_t HlsMaker::SegmentCount() const { return segments_.size(); }

uint32_t HlsMaker::CurrentSegmentSize() const {
    return current_segment_.body != nullptr ? current_segment_.body->size : 0;
}

MediaHlsPlaylist HlsMaker::BuildPlaylist(
    uint32_t hls_segment_duration_ms, uint32_t hls_playlist_depth) const {
    MediaHlsPlaylist playlist;
    if (segments_.empty()) {
        return playlist;
    }

    playlist.supported = true;
    const size_t playlist_depth =
        std::max<size_t>(1, static_cast<size_t>(hls_playlist_depth));
    const size_t start_index =
        segments_.size() > playlist_depth
            ? segments_.size() - playlist_depth
            : 0;
    playlist.media_sequence = segments_[start_index].sequence;
    int64_t max_duration_us =
        static_cast<int64_t>(hls_segment_duration_ms) * 1000;
    for (size_t i = start_index; i < segments_.size(); ++i) {
        const MediaSegmentRef &segment = segments_[i];
        playlist.entries.push_back(
            MediaHlsEntry{segment.sequence, segment.duration_us});
        max_duration_us = std::max(max_duration_us, segment.duration_us);
    }
    playlist.target_duration_sec = static_cast<uint32_t>(
        std::max<int64_t>(1, (max_duration_us + 999999) / 1000000));
    return playlist;
}

MediaSegmentRef HlsMaker::FindSegmentRef(uint64_t sequence) const {
    for (const MediaSegmentRef &segment : segments_) {
        if (segment.sequence == sequence) {
            return MediaSegmentRefCopy(&segment);
        }
    }
    return MediaSegmentRef{};
}

bool HlsMaker::AppendFrame(const EncodedFrame &frame,
                           const FramePayload &payload,
                           const std::string &vps,
                           const std::string &sps,
                           const std::string &pps,
                           bool keyframe,
                           bool prepend_parameter_sets,
                           uint32_t hls_segment_duration_ms,
                           uint32_t hls_segment_cache_depth,
                           bool *segment_created) {
    if (segment_created != nullptr) {
        *segment_created = false;
    }
    ObserveFrameTiming(frame);

    if (keyframe && current_segment_.started &&
        frame.pts_us - current_segment_.start_pts_us >=
            static_cast<int64_t>(hls_segment_duration_ms) * 1000) {
        // 只在关键帧边界切 segment，避免 playlist 中出现不能独立解码的
        // segment 起点。
        const bool finalized =
            FinalizeCurrentSegment(hls_segment_cache_depth);
        if (segment_created != nullptr) {
            *segment_created = finalized;
        }
    }
    if (keyframe && !current_segment_.started) {
        // HLS 首个 segment 必须从关键帧开始；关键帧前的 P/B 帧直接忽略。
        StartSegment(frame.codec, frame.pts_us);
    }
    if (!current_segment_.started) {
        return true;
    }
    if (!AppendFrameToSegment(payload, vps, sps, pps,
                              prepend_parameter_sets, frame)) {
        UnrefSegmentState(&current_segment_);
        return false;
    }
    current_segment_.last_pts_us = frame.pts_us;
    return true;
}

void HlsMaker::UnrefSegmentState(SegmentState *segment) {
    if (segment == nullptr) {
        return;
    }
    VideoBufferUnref(segment->body);
    *segment = SegmentState{};
}

uint32_t HlsMaker::ClampSegmentCapacity(size_t capacity) {
    if (capacity < kInitialHlsSegmentBytes) {
        return static_cast<uint32_t>(kInitialHlsSegmentBytes);
    }
    if (capacity > kMaxHlsSegmentBytes) {
        return static_cast<uint32_t>(kMaxHlsSegmentBytes);
    }
    return static_cast<uint32_t>(capacity);
}

bool HlsMaker::EnsureSegmentCapacity(SegmentState *segment,
                                     size_t extra_bytes) {
    if (segment == nullptr || segment->body == nullptr ||
        segment->body->size > segment->body->capacity) {
        return false;
    }
    if (extra_bytes <= segment->body->capacity - segment->body->size) {
        return true;
    }
    uint32_t new_capacity = segment->body->capacity;
    while (extra_bytes > new_capacity - segment->body->size) {
        if (new_capacity >= kMaxHlsSegmentBytes) {
            return false;
        }
        const uint32_t doubled = new_capacity * 2U;
        new_capacity = doubled > new_capacity ? doubled : kMaxHlsSegmentBytes;
        if (new_capacity > kMaxHlsSegmentBytes) {
            new_capacity = kMaxHlsSegmentBytes;
        }
    }
    VideoBuffer *new_body = VideoBufferAlloc(new_capacity);
    if (new_body == nullptr) {
        return false;
    }
    std::copy(segment->body->data, segment->body->data + segment->body->size,
              new_body->data);
    if (!VideoBufferSetSize(new_body, segment->body->size)) {
        VideoBufferUnref(new_body);
        return false;
    }
    VideoBufferUnref(segment->body);
    segment->body = new_body;
    return true;
}

TsSegmentBuffer HlsMaker::SegmentBuffer(SegmentState *segment) {
    TsSegmentBuffer buffer;
    if (segment == nullptr || segment->body == nullptr) {
        return buffer;
    }
    buffer.data = segment->body->data;
    buffer.capacity = segment->body->capacity;
    buffer.size = segment->body->size;
    return buffer;
}

bool HlsMaker::CommitSegmentBuffer(
    SegmentState *segment,
    const TsSegmentBuffer &buffer) {
    return segment != nullptr && segment->body != nullptr &&
           buffer.size <= segment->body->capacity &&
           VideoBufferSetSize(segment->body,
                              static_cast<uint32_t>(buffer.size));
}

void HlsMaker::ClearSegments() {
    for (MediaSegmentRef &segment : segments_) {
        MediaSegmentRefUnref(&segment);
    }
    segments_.clear();
}

void HlsMaker::ObserveFrameTiming(const EncodedFrame &frame) {
    if (last_pts_us_ > 0 && frame.pts_us > last_pts_us_) {
        last_frame_duration_us_ = frame.pts_us - last_pts_us_;
    }
    last_pts_us_ = frame.pts_us;
}

bool HlsMaker::AppendFrameToSegment(const FramePayload &payload,
                                    const std::string &vps,
                                    const std::string &sps,
                                    const std::string &pps,
                                    bool prepend_parameter_sets,
                                    const EncodedFrame &frame) {
    if (current_segment_.body == nullptr) {
        return false;
    }
    for (size_t attempt = 0; attempt < 8; ++attempt) {
        TsSegmentBuffer segment_body =
            SegmentBuffer(&current_segment_);
        const size_t original_size = segment_body.size;
        TsMuxerState original_state = ts_muxer_state_;
        bool appended = false;
        if (frame.codec == VideoCodec::kH265) {
            appended = AppendH265NalUnitsToTsSegmentBuffer(
                payload.h265_units, vps, sps, pps, prepend_parameter_sets,
                frame.pts_us, frame.dts_us,
                &ts_muxer_state_, &segment_body);
        } else {
            appended = AppendH264NalUnitsToTsSegmentBuffer(
                payload.h264_units, sps, pps, prepend_parameter_sets,
                frame.pts_us, frame.dts_us, &ts_muxer_state_, &segment_body);
        }
        if (appended && CommitSegmentBuffer(&current_segment_, segment_body)) {
            return true;
        }
        // buffer 容量不足时回滚 TS continuity 和 segment size，再扩容重试，
        // 避免生成半个 PES/TS packet。
        ts_muxer_state_ = original_state;
        (void)VideoBufferSetSize(current_segment_.body,
                                 static_cast<uint32_t>(original_size));
        if (!EnsureSegmentCapacity(&current_segment_,
                                   current_segment_.body->capacity)) {
            return false;
        }
    }
    return false;
}

int64_t HlsMaker::CurrentSegmentDurationUs() const {
    return std::max<int64_t>(last_frame_duration_us_,
                             current_segment_.last_pts_us -
                                 current_segment_.start_pts_us +
                                 last_frame_duration_us_);
}

void HlsMaker::StartSegment(VideoCodec codec, int64_t pts_us) {
    UnrefSegmentState(&current_segment_);
    current_segment_ = SegmentState{};
    current_segment_.started = true;
    current_segment_.sequence = next_segment_sequence_++;
    current_segment_.start_pts_us = pts_us;
    current_segment_.last_pts_us = pts_us;
    const uint32_t segment_capacity =
        ClampSegmentCapacity(next_segment_capacity_);
    current_segment_.body = VideoBufferAlloc(segment_capacity);
    if (current_segment_.body == nullptr) {
        UnrefSegmentState(&current_segment_);
        return;
    }
    TsSegmentBuffer segment_body = SegmentBuffer(&current_segment_);
    if (!AppendTsSegmentHeader(codec, &ts_muxer_state_, &segment_body) ||
        !CommitSegmentBuffer(&current_segment_, segment_body)) {
        UnrefSegmentState(&current_segment_);
    }
}

void HlsMaker::RememberSegmentCapacity(const SegmentState &segment) {
    if (segment.body == nullptr) {
        return;
    }
    next_segment_capacity_ = ClampSegmentCapacity(segment.body->size);
}

void HlsMaker::PopOldestSegment() {
    if (segments_.empty()) {
        return;
    }
    MediaSegmentRefUnref(&segments_.front());
    segments_.pop_front();
}

void HlsMaker::PushFinalizedSegment(uint32_t segment_cache_depth) {
    if (!current_segment_.started || current_segment_.body == nullptr ||
        current_segment_.body->size == 0) {
        return;
    }
    MediaSegmentRef segment;
    segment.found = true;
    segment.sequence = current_segment_.sequence;
    segment.duration_us = CurrentSegmentDurationUs();
    segment.body = current_segment_.body;
    RememberSegmentCapacity(current_segment_);
    current_segment_.body = nullptr;
    segments_.push_back(segment);
    while (segments_.size() > segment_cache_depth) {
        PopOldestSegment();
    }
}

bool HlsMaker::FinalizeCurrentSegment(uint32_t segment_cache_depth) {
    if (!current_segment_.started || current_segment_.body == nullptr ||
        current_segment_.body->size == 0) {
        return false;
    }

    PushFinalizedSegment(segment_cache_depth);
    UnrefSegmentState(&current_segment_);
    return true;
}

}  // namespace media_source_internal
}  // namespace live_stream

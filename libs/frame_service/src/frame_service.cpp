#include "frame_service.h"

#include "media/encoded_frame.h"
#include "media_service.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char *kServiceName = "frame_service";
constexpr uint16_t kPatPid = 0x0000;
constexpr uint16_t kPmtPid = 0x1000;
constexpr uint16_t kVideoPid = 0x0100;
constexpr uint8_t kTsPacketSize = 188;

struct H264NalUnit {
  const uint8_t *data = nullptr;
  size_t size = 0;
  uint8_t type = 0;
};

struct HlsSegmentState {
  bool started = false;
  uint64_t sequence = 0;
  int64_t start_pts_us = 0;
  int64_t last_pts_us = 0;
  std::string body;
};

bool IsStreamSupported(StreamId stream_id) {
  return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
}

bool IsBrowserCodec(VideoCodec codec) { return codec == VideoCodec::kH264; }

bool IsKeyFrame(FrameType frame_type) {
  return frame_type == FrameType::kIdr || frame_type == FrameType::kI;
}

bool HasValidPayload(const EncodedFrame &frame) {
  return frame.buffer != nullptr && frame.size != 0 &&
         frame.offset <= frame.buffer->Size() &&
         frame.size <= frame.buffer->Size() - frame.offset;
}

void AppendU16(std::string *out, uint16_t value) {
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

void AppendU24(std::string *out, uint32_t value) {
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

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

std::vector<H264NalUnit> ParseAnnexBNalUnits(const uint8_t *data, size_t size) {
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
      const uint8_t nal_type = data[nal_begin] & 0x1f;
      units.push_back({data + nal_begin, nal_end - nal_begin, nal_type});
    }
    if (next == std::string::npos) {
      break;
    }
    offset = next;
  }
  return units;
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

std::string BuildPatPacket(uint8_t *continuity_counter) {
  std::string section;
  section.push_back('\x00');
  section.push_back('\xb0');
  section.push_back('\x0d');
  AppendU16(&section, 1);
  section.push_back('\xc1');
  section.push_back('\x00');
  section.push_back('\x00');
  AppendU16(&section, 1);
  section.push_back(static_cast<char>(0xe0 | ((kPmtPid >> 8) & 0x1f)));
  section.push_back(static_cast<char>(kPmtPid & 0xff));
  const uint32_t crc = MpegCrc32(
      reinterpret_cast<const uint8_t *>(section.data()), section.size());
  AppendU32(&section, crc);

  std::string packet(kTsPacketSize, static_cast<char>(0xff));
  packet[0] = '\x47';
  packet[1] = '\x40';
  packet[2] = '\x00';
  packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
  *continuity_counter = static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
  packet[4] = '\x00';
  std::copy(section.begin(), section.end(), packet.begin() + 5);
  return packet;
}

std::string BuildPmtPacket(uint8_t *continuity_counter) {
  std::string section;
  section.push_back('\x02');
  section.push_back('\xb0');
  section.push_back('\x12');
  AppendU16(&section, 1);
  section.push_back('\xc1');
  section.push_back('\x00');
  section.push_back('\x00');
  section.push_back(static_cast<char>(0xe0 | ((kVideoPid >> 8) & 0x1f)));
  section.push_back(static_cast<char>(kVideoPid & 0xff));
  section.push_back('\xf0');
  section.push_back('\x00');
  section.push_back('\x1b');
  section.push_back(static_cast<char>(0xe0 | ((kVideoPid >> 8) & 0x1f)));
  section.push_back(static_cast<char>(kVideoPid & 0xff));
  section.push_back('\xf0');
  section.push_back('\x00');
  const uint32_t crc = MpegCrc32(
      reinterpret_cast<const uint8_t *>(section.data()), section.size());
  AppendU32(&section, crc);

  std::string packet(kTsPacketSize, static_cast<char>(0xff));
  packet[0] = '\x47';
  packet[1] = static_cast<char>(0x40 | ((kPmtPid >> 8) & 0x1f));
  packet[2] = static_cast<char>(kPmtPid & 0xff);
  packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
  *continuity_counter = static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
  packet[4] = '\x00';
  std::copy(section.begin(), section.end(), packet.begin() + 5);
  return packet;
}

void AppendPts(std::string *out, uint8_t prefix, uint64_t value) {
  const uint64_t pts = value & 0x1ffffffffULL;
  out->push_back(
      static_cast<char>((prefix << 4) | (((pts >> 30) & 0x07) << 1) | 0x01));
  out->push_back(static_cast<char>((pts >> 22) & 0xff));
  out->push_back(static_cast<char>((((pts >> 15) & 0x7f) << 1) | 0x01));
  out->push_back(static_cast<char>((pts >> 7) & 0xff));
  out->push_back(static_cast<char>(((pts & 0x7f) << 1) | 0x01));
}

std::string BuildPesPacket(const std::string &access_unit, uint64_t pts_90k,
                           uint64_t dts_90k) {
  std::string pes;
  pes.append("\x00\x00\x01\xe0", 4);
  pes.append("\x00\x00", 2);
  pes.push_back('\x80');
  const bool has_dts = pts_90k != dts_90k;
  pes.push_back(has_dts ? '\xc0' : '\x80');
  pes.push_back(has_dts ? '\x0a' : '\x05');
  AppendPts(&pes, has_dts ? 0x03 : 0x02, pts_90k);
  if (has_dts) {
    AppendPts(&pes, 0x01, dts_90k);
  }
  pes.append(access_unit);
  return pes;
}

void WritePcr(char *target, uint64_t pcr_90k) {
  const uint64_t base = pcr_90k & 0x1ffffffffULL;
  target[0] = static_cast<char>((base >> 25) & 0xff);
  target[1] = static_cast<char>((base >> 17) & 0xff);
  target[2] = static_cast<char>((base >> 9) & 0xff);
  target[3] = static_cast<char>((base >> 1) & 0xff);
  target[4] = static_cast<char>(((base & 0x01) << 7) | 0x7e);
  target[5] = '\x00';
}

void AppendTsPayload(const std::string &pes, uint64_t pcr_90k,
                     uint8_t *continuity_counter, std::string *out) {
  size_t offset = 0;
  while (offset < pes.size()) {
    const bool first_packet = offset == 0;
    const size_t remaining = pes.size() - offset;
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
    if (use_adaptation && payload_size == 183) {
      --payload_size;
    }
    const size_t adaptation_total = use_adaptation ? 184 - payload_size : 0;

    std::string packet(kTsPacketSize, static_cast<char>(0xff));
    packet[0] = '\x47';
    packet[1] = static_cast<char>((first_packet ? 0x40 : 0x00) |
                                  ((kVideoPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kVideoPid & 0xff);
    packet[3] = static_cast<char>(((use_adaptation ? 0x30 : 0x10)) |
                                  (*continuity_counter & 0x0f));
    *continuity_counter =
        static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);

    size_t packet_offset = 4;
    if (use_adaptation) {
      const size_t adaptation_length = adaptation_total - 1;
      packet[packet_offset++] = static_cast<char>(adaptation_length);
      packet[packet_offset++] = first_packet ? '\x10' : '\x00';
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
    std::copy(pes.begin() + static_cast<std::ptrdiff_t>(offset),
              pes.begin() + static_cast<std::ptrdiff_t>(offset + payload_size),
              packet.begin() + static_cast<std::ptrdiff_t>(packet_offset));
    out->append(packet);
    offset += payload_size;
  }
}

std::string BuildFlvFileHeader() {
  std::string header;
  header.append("FLV", 3);
  header.push_back('\x01');
  header.push_back('\x01');
  header.append("\x00\x00\x00\x09", 4);
  header.append("\x00\x00\x00\x00", 4);
  return header;
}

std::string BuildFlvVideoTag(bool keyframe, uint8_t avc_packet_type,
                             int32_t composition_time_ms, uint32_t timestamp_ms,
                             const std::string &payload) {
  std::string tag;
  const uint32_t body_size = 5U + static_cast<uint32_t>(payload.size());
  tag.push_back('\x09');
  AppendU24(&tag, body_size);
  AppendU24(&tag, timestamp_ms & 0x00ffffffU);
  tag.push_back(static_cast<char>((timestamp_ms >> 24) & 0xff));
  tag.append("\x00\x00\x00", 3);
  tag.push_back(static_cast<char>(((keyframe ? 1 : 2) << 4) | 7));
  tag.push_back(static_cast<char>(avc_packet_type));
  AppendU24(&tag, static_cast<uint32_t>(composition_time_ms) & 0x00ffffffU);
  tag.append(payload);
  AppendU32(&tag, body_size + 11U);
  return tag;
}

std::string BuildAvcSequenceHeader(const std::string &sps,
                                   const std::string &pps) {
  std::string config;
  config.push_back('\x01');
  config.push_back(sps.size() > 1 ? sps[1] : '\x64');
  config.push_back(sps.size() > 2 ? sps[2] : '\x00');
  config.push_back(sps.size() > 3 ? sps[3] : '\x1f');
  config.push_back(static_cast<char>(0xff));
  config.push_back(static_cast<char>(0xe1));
  AppendU16(&config, static_cast<uint16_t>(sps.size()));
  config.append(sps);
  config.push_back('\x01');
  AppendU16(&config, static_cast<uint16_t>(pps.size()));
  config.append(pps);
  return config;
}

std::string BuildAvccSample(const std::vector<H264NalUnit> &units) {
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

std::string BuildTsAccessUnit(const std::vector<H264NalUnit> &units,
                              const std::string &sps, const std::string &pps,
                              bool prepend_parameter_sets) {
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

class FrameServiceImpl : public IFrameService, public IFrameSink {
public:
  FrameServiceImpl(FrameServiceOptions options,
                   FrameServiceDependencies dependencies)
      : options_(std::move(options)), dependencies_(dependencies) {}

  ~FrameServiceImpl() override { Stop(); }

  bool Start() override {
    MediaService *media_service = nullptr;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (started_) {
        return true;
      }
      media_service = dependencies_.media_service;
    }
    if (media_service == nullptr) {
      return false;
    }
    if (options_.hls_segment_duration_ms == 0 ||
        options_.hls_playlist_depth == 0 || options_.max_flv_clients == 0) {
      return false;
    }
    FrameSubscribeOptions main_options;
    main_options.stream_id = StreamId::kMain;
    main_options.require_key_frame_first = true;
    main_options.sink_name = kServiceName;
    const FrameSubscriptionId main_subscription_id =
        media_service->SubscribeFrames(main_options, this);

    FrameSubscribeOptions sub_options;
    sub_options.stream_id = StreamId::kSub;
    sub_options.require_key_frame_first = true;
    sub_options.sink_name = kServiceName;
    const FrameSubscriptionId sub_subscription_id =
        media_service->SubscribeFrames(sub_options, this);
    std::lock_guard<std::mutex> guard(mutex_);
    main_subscription_id_ = main_subscription_id;
    sub_subscription_id_ = sub_subscription_id;
    started_ = main_subscription_id_ != 0 || sub_subscription_id_ != 0;
    return started_;
  }

  void Stop() override {
    MediaService *media_service = nullptr;
    FrameSubscriptionId main_subscription_id = 0;
    FrameSubscriptionId sub_subscription_id = 0;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (!started_) {
        return;
      }
      media_service = dependencies_.media_service;
      main_subscription_id = main_subscription_id_;
      sub_subscription_id = sub_subscription_id_;
      main_subscription_id_ = 0;
      sub_subscription_id_ = 0;
      flv_clients_.clear();
      main_stream_ = StreamContext{};
      sub_stream_ = StreamContext{};
      started_ = false;
    }
    if (media_service != nullptr) {
      if (main_subscription_id != 0) {
        (void)media_service->UnsubscribeFrames(main_subscription_id);
      }
      if (sub_subscription_id != 0) {
        (void)media_service->UnsubscribeFrames(sub_subscription_id);
      }
    }
  }

  bool IsHlsSupported(StreamId stream_id) const override {
    std::lock_guard<std::mutex> guard(mutex_);
    const StreamContext *stream = FindStream(stream_id);
    return stream != nullptr && IsBrowserCodec(stream->codec);
  }

  bool IsFlvSupported(StreamId stream_id) const override {
    return IsHlsSupported(stream_id);
  }

  FrameHlsPlaylist GetHlsPlaylist(StreamId stream_id) const override {
    std::lock_guard<std::mutex> guard(mutex_);
    FrameHlsPlaylist playlist;
    const StreamContext *stream = FindStream(stream_id);
    if (stream == nullptr || !IsBrowserCodec(stream->codec) ||
        stream->segments.empty()) {
      return playlist;
    }
    playlist.supported = true;
    playlist.media_sequence = stream->segments.front().sequence;
    int64_t max_duration_us =
        static_cast<int64_t>(options_.hls_segment_duration_ms) * 1000;
    for (const FrameSegment &segment : stream->segments) {
      playlist.entries.push_back(
          FrameHlsEntry{segment.sequence, segment.duration_us});
      max_duration_us = std::max(max_duration_us, segment.duration_us);
    }
    playlist.target_duration_sec = static_cast<uint32_t>(
        std::max<int64_t>(1, (max_duration_us + 999999) / 1000000));
    return playlist;
  }

  FrameSegment GetHlsSegment(StreamId stream_id,
                             uint64_t sequence) const override {
    std::lock_guard<std::mutex> guard(mutex_);
    const StreamContext *stream = FindStream(stream_id);
    if (stream == nullptr || !IsBrowserCodec(stream->codec)) {
      return FrameSegment{};
    }
    for (const FrameSegment &segment : stream->segments) {
      if (segment.sequence == sequence) {
        return segment;
      }
    }
    return FrameSegment{};
  }

  FrameFlvBootstrap GetFlvBootstrap(StreamId stream_id) const override {
    std::lock_guard<std::mutex> guard(mutex_);
    FrameFlvBootstrap bootstrap;
    const StreamContext *stream = FindStream(stream_id);
    if (stream == nullptr || !IsBrowserCodec(stream->codec)) {
      return bootstrap;
    }
    bootstrap.supported = true;
    bootstrap.file_header = BuildFlvFileHeader();
    bootstrap.sequence_header = stream->sequence_header_tag;
    bootstrap.last_keyframe = stream->last_keyframe_tag;
    bootstrap.config_generation = stream->config_generation;
    return bootstrap;
  }

  FrameFlvClientId
  AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                  const std::shared_ptr<IFrameFlvSink> &sink) override {
    if (sink == nullptr) {
      return 0;
    }
    MediaService *media_service = nullptr;
    FrameFlvClientId client_id = 0;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      StreamContext *stream = FindMutableStream(stream_id);
      if (stream == nullptr || !IsBrowserCodec(stream->codec) ||
          flv_clients_.size() >= options_.max_flv_clients) {
        return 0;
      }
      client_id = next_flv_client_id_++;
      FlvClientState client;
      client.stream_id = stream_id;
      client.config_generation = config_generation;
      client.sink = sink;
      flv_clients_[client_id] = client;
      media_service = dependencies_.media_service;
    }
    if (media_service != nullptr) {
      (void)media_service->RequestKeyFrame(stream_id,
                                           KeyFrameReason::kNewClient);
    }
    return client_id;
  }

  bool DetachFlvClient(FrameFlvClientId client_id) override {
    std::lock_guard<std::mutex> guard(mutex_);
    return flv_clients_.erase(client_id) != 0;
  }

  FrameServiceStats GetStats() const override {
    std::lock_guard<std::mutex> guard(mutex_);
    FrameServiceStats stats = stats_;
    stats.enabled = started_;
    stats.active_flv_clients = static_cast<uint32_t>(flv_clients_.size());
    return stats;
  }

  const char *Name() const override { return kServiceName; }

  void OnFrame(const EncodedFrame &frame) override {
    if (!HasValidPayload(frame) || !IsStreamSupported(frame.stream_id)) {
      return;
    }
    const uint8_t *payload = frame.buffer->Data() + frame.offset;
    const size_t size = frame.size;
    const std::vector<H264NalUnit> units = ParseAnnexBNalUnits(payload, size);
    if (units.empty()) {
      return;
    }

    std::vector<FrameFlvClientId> detach_ids;
    std::vector<std::pair<std::shared_ptr<IFrameFlvSink>, bool>> clients;
    std::string sequence_header_tag;
    std::string flv_tag;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      StreamContext *stream = FindMutableStream(frame.stream_id);
      if (stream == nullptr) {
        return;
      }
      if (stream->codec != frame.codec) {
        ResetStream(stream, frame.codec);
      }
      if (!IsBrowserCodec(stream->codec)) {
        return;
      }

      bool has_sps = false;
      bool has_pps = false;
      for (const H264NalUnit &unit : units) {
        if (unit.type == 7) {
          stream->sps.assign(reinterpret_cast<const char *>(unit.data),
                             unit.size);
          has_sps = true;
        } else if (unit.type == 8) {
          stream->pps.assign(reinterpret_cast<const char *>(unit.data),
                             unit.size);
          has_pps = true;
        }
      }
      if (!stream->sps.empty() && !stream->pps.empty() &&
          (has_sps || has_pps)) {
        stream->sequence_header_tag = BuildFlvVideoTag(
            true, 0, 0, static_cast<uint32_t>(frame.dts_us / 1000),
            BuildAvcSequenceHeader(stream->sps, stream->pps));
        ++stream->config_generation;
      }

      const bool keyframe = IsKeyFrame(frame.frame_type);
      if (stream->last_pts_us > 0 && frame.pts_us > stream->last_pts_us) {
        stream->last_frame_duration_us = frame.pts_us - stream->last_pts_us;
      }
      stream->last_pts_us = frame.pts_us;

      bool frame_has_parameter_sets = false;
      for (const H264NalUnit &unit : units) {
        if (unit.type == 7 || unit.type == 8) {
          frame_has_parameter_sets = true;
          break;
        }
      }
      if (keyframe && stream->current_segment.started &&
          frame.pts_us - stream->current_segment.start_pts_us >=
              static_cast<int64_t>(options_.hls_segment_duration_ms) * 1000) {
        FinalizeCurrentSegment(stream);
      }
      if (!stream->current_segment.started) {
        StartSegment(stream, frame.pts_us);
      }
      const std::string access_unit =
          BuildTsAccessUnit(units, stream->sps, stream->pps,
                            keyframe && !frame_has_parameter_sets);
      const uint64_t pts_90k =
          static_cast<uint64_t>(std::max<int64_t>(0, frame.pts_us) * 9 / 100);
      const uint64_t dts_90k =
          static_cast<uint64_t>(std::max<int64_t>(0, frame.dts_us) * 9 / 100);
      AppendTsPayload(BuildPesPacket(access_unit, pts_90k, dts_90k), dts_90k,
                      &stream->video_continuity, &stream->current_segment.body);
      stream->current_segment.last_pts_us = frame.pts_us;

      const std::string avcc_sample = BuildAvccSample(units);
      if (!avcc_sample.empty()) {
        const int64_t composition_time_ms =
            (frame.pts_us - frame.dts_us) / 1000;
        flv_tag = BuildFlvVideoTag(
            keyframe, 1, static_cast<int32_t>(composition_time_ms),
            static_cast<uint32_t>(frame.dts_us / 1000), avcc_sample);
        if (keyframe) {
          stream->last_keyframe_tag = flv_tag;
        }
      }
      sequence_header_tag = stream->sequence_header_tag;
      for (auto &item : flv_clients_) {
        if (item.second.stream_id != frame.stream_id ||
            item.second.sink == nullptr || flv_tag.empty()) {
          continue;
        }
        const bool needs_config =
            item.second.config_generation != stream->config_generation &&
            !sequence_header_tag.empty();
        if (needs_config) {
          item.second.config_generation = stream->config_generation;
        }
        clients.push_back({item.second.sink, needs_config});
      }
    }

    for (const auto &client : clients) {
      if (client.second &&
          !client.first->OnFlvChunk(
              reinterpret_cast<const uint8_t *>(sequence_header_tag.data()),
              sequence_header_tag.size())) {
        detach_ids.push_back(FindClientId(client.first));
        continue;
      }
      if (!client.first->OnFlvChunk(
              reinterpret_cast<const uint8_t *>(flv_tag.data()),
              flv_tag.size())) {
        detach_ids.push_back(FindClientId(client.first));
      }
    }
    for (FrameFlvClientId client_id : detach_ids) {
      if (client_id != 0) {
        (void)DetachFlvClient(client_id);
      }
    }
  }

  void OnSourceStateChanged(StreamId stream_id, StreamState state) override {
    std::lock_guard<std::mutex> guard(mutex_);
    StreamContext *stream = FindMutableStream(stream_id);
    if (stream != nullptr) {
      stream->state = state;
    }
  }

private:
  struct StreamContext {
    VideoCodec codec = VideoCodec::kH264;
    StreamState state = StreamState::kClosed;
    std::string sps;
    std::string pps;
    std::string sequence_header_tag;
    std::string last_keyframe_tag;
    std::deque<FrameSegment> segments;
    HlsSegmentState current_segment;
    uint64_t next_segment_sequence = 1;
    uint64_t config_generation = 0;
    uint8_t pat_continuity = 0;
    uint8_t pmt_continuity = 0;
    uint8_t video_continuity = 0;
    int64_t last_pts_us = -1;
    int64_t last_frame_duration_us = 33333;
  };

  struct FlvClientState {
    StreamId stream_id = StreamId::kMain;
    uint64_t config_generation = 0;
    std::shared_ptr<IFrameFlvSink> sink;
  };

  const StreamContext *FindStream(StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
      return &main_stream_;
    }
    if (stream_id == StreamId::kSub) {
      return &sub_stream_;
    }
    return nullptr;
  }

  StreamContext *FindMutableStream(StreamId stream_id) {
    if (stream_id == StreamId::kMain) {
      return &main_stream_;
    }
    if (stream_id == StreamId::kSub) {
      return &sub_stream_;
    }
    return nullptr;
  }

  void ResetStream(StreamContext *stream, VideoCodec codec) {
    if (stream == nullptr) {
      return;
    }
    const StreamState state = stream->state;
    *stream = StreamContext{};
    stream->codec = codec;
    stream->state = state;
  }

  void StartSegment(StreamContext *stream, int64_t pts_us) {
    if (stream == nullptr) {
      return;
    }
    stream->current_segment = HlsSegmentState{};
    stream->current_segment.started = true;
    stream->current_segment.sequence = stream->next_segment_sequence++;
    stream->current_segment.start_pts_us = pts_us;
    stream->current_segment.last_pts_us = pts_us;
    stream->current_segment.body = BuildPatPacket(&stream->pat_continuity);
    stream->current_segment.body += BuildPmtPacket(&stream->pmt_continuity);
  }

  void FinalizeCurrentSegment(StreamContext *stream) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.body.empty()) {
      return;
    }
    FrameSegment segment;
    segment.found = true;
    segment.sequence = stream->current_segment.sequence;
    segment.duration_us =
        std::max<int64_t>(stream->last_frame_duration_us,
                          stream->current_segment.last_pts_us -
                              stream->current_segment.start_pts_us +
                              stream->last_frame_duration_us);
    segment.body = stream->current_segment.body;
    stream->segments.push_back(std::move(segment));
    while (stream->segments.size() > options_.hls_playlist_depth) {
      stream->segments.pop_front();
    }
    ++stats_.hls_segments_created;
    stream->current_segment = HlsSegmentState{};
  }

  FrameFlvClientId
  FindClientId(const std::shared_ptr<IFrameFlvSink> &sink) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto &item : flv_clients_) {
      if (item.second.sink == sink) {
        return item.first;
      }
    }
    return 0;
  }

  FrameServiceOptions options_;
  FrameServiceDependencies dependencies_;
  mutable std::mutex mutex_;
  StreamContext main_stream_;
  StreamContext sub_stream_;
  std::map<FrameFlvClientId, FlvClientState> flv_clients_;
  FrameServiceStats stats_;
  FrameSubscriptionId main_subscription_id_ = 0;
  FrameSubscriptionId sub_subscription_id_ = 0;
  FrameFlvClientId next_flv_client_id_ = 1;
  bool started_ = false;
};

} // namespace

std::unique_ptr<IFrameService>
CreateFrameService(const FrameServiceOptions &options,
                   const FrameServiceDependencies &dependencies) {
  return std::unique_ptr<IFrameService>(
      new FrameServiceImpl(options, dependencies));
}

} // namespace live_stream

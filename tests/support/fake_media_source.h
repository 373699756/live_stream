#ifndef LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_SOURCE_H_
#define LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_SOURCE_H_

#include "media_source.h"

#include <map>
#include <vector>

namespace live_stream {
namespace test {

class FakeMediaFrameSource : public IMediaFrameSource {
public:
  ~FakeMediaFrameSource() override {
    ClearAllReaders();
  }

  bool IsStreamAvailable(StreamId stream_id) const override {
    return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
  }

  VideoCodec GetStreamCodec(StreamId stream_id) const override {
    (void)stream_id;
    return VideoCodec::kH264;
  }

  MediaFrameReaderId AttachFrameReader(
      const MediaFrameReaderOptions &options) override {
    if (!IsStreamAvailable(options.stream_id)) {
      return 0;
    }
    const MediaFrameReaderId reader_id = next_reader_id_++;
    Reader reader;
    reader.stream_id = options.stream_id;
    reader.generation = next_generation_++;
    readers_[reader_id] = reader;
    if (options.stream_id == StreamId::kMain) {
      main_reader_id = reader_id;
    } else {
      sub_reader_id = reader_id;
    }
    ++attached_readers;
    return reader_id;
  }

  bool DetachFrameReader(MediaFrameReaderId reader_id,
                         MediaFrameReaderCloseReason reason) override {
    (void)reason;
    auto iter = readers_.find(reader_id);
    if (iter == readers_.end()) {
      return false;
    }
    ClearFrames(&iter->second.pending_frames);
    readers_.erase(iter);
    if (reader_id == main_reader_id) {
      main_reader_id = 0;
    }
    if (reader_id == sub_reader_id) {
      sub_reader_id = 0;
    }
    ++detached_readers;
    return true;
  }

  MediaFrameReaderStatus GetFrameReaderStatus(
      MediaFrameReaderId reader_id) const override {
    MediaFrameReaderStatus status;
    auto iter = readers_.find(reader_id);
    if (iter == readers_.end()) {
      status.close_reason = MediaFrameReaderCloseReason::kDetached;
      return status;
    }
    status.attached = true;
    status.stream_id = iter->second.stream_id;
    status.reader_generation = iter->second.generation;
    status.pending_frames =
        static_cast<uint32_t>(iter->second.pending_frames.size());
    return status;
  }

  MediaFrameReaderStartData GetFrameReaderStartData(
      MediaFrameReaderId reader_id) const override {
    MediaFrameReaderStartData start_data;
    auto iter = readers_.find(reader_id);
    if (iter == readers_.end()) {
      return start_data;
    }
    start_data.stream_running = true;
    start_data.gop_complete = true;
    start_data.reader_generation = iter->second.generation;
    start_data.track = TrackForStream(iter->second.stream_id);
    return start_data;
  }

  bool PopFrameReaderFrame(MediaFrameReaderId reader_id,
                           MediaFrameReaderFrame *frame) override {
    if (frame == nullptr) {
      return false;
    }
    auto iter = readers_.find(reader_id);
    if (iter == readers_.end() || iter->second.pending_frames.empty()) {
      return false;
    }
    MediaFrame next_frame;
    if (!MediaFrameMove(&next_frame, &iter->second.pending_frames.front())) {
      return false;
    }
    iter->second.pending_frames.erase(iter->second.pending_frames.begin());
    MediaFrameReaderFrameUnref(frame);
    frame->reader_id = reader_id;
    frame->reader_generation = iter->second.generation;
    frame->starts_on_keyframe = next_frame.key_frame;
    return MediaFrameMove(&frame->frame, &next_frame);
  }

  FrameAttachId AttachFrameSink(
      const FrameAttachOptions &options, IFrameSink *sink) override {
    if (sink == nullptr || !IsStreamAvailable(options.stream_id)) {
      return 0;
    }
    const FrameAttachId sink_id = next_sink_id_++;
    sinks_[sink_id] = Sink{options.stream_id, sink};
    ++attached_sinks;
    return sink_id;
  }

  bool DetachFrameSink(FrameAttachId sink_id) override {
    return sinks_.erase(sink_id) != 0;
  }

  bool RequestKeyFrame(StreamId stream_id,
                       KeyFrameReason reason) override {
    last_key_frame_stream = stream_id;
    last_key_frame_reason = reason;
    ++key_frame_requests;
    return true;
  }

  bool DeliverFrame(const EncodedFrame &encoded_frame) {
    bool delivered = false;
    for (auto &item : readers_) {
      Reader &reader = item.second;
      if (reader.stream_id != encoded_frame.stream_id) {
        continue;
      }
      MediaFrame frame;
      const bool key_frame = encoded_frame.frame_type == FrameType::kIdr ||
                             encoded_frame.frame_type == FrameType::kI;
      if (!MediaFrameSetEncodedFrame(&frame, &encoded_frame,
                                     MediaTrackType::kVideo, key_frame,
                                     40000)) {
        return false;
      }
      reader.pending_frames.push_back(MediaFrame{});
      if (!MediaFrameMove(&reader.pending_frames.back(), &frame)) {
        reader.pending_frames.pop_back();
        return false;
      }
      delivered = true;
    }
    for (auto &item : sinks_) {
      Sink &sink = item.second;
      if (sink.stream_id != encoded_frame.stream_id || sink.sink == nullptr) {
        continue;
      }
      FramePayload payload;
      if (!EncodedFrameRefCopy(&payload.encoded_frame, &encoded_frame)) {
        return false;
      }
      sink.sink->OnFrame(payload);
      FramePayloadUnref(&payload);
      delivered = true;
    }
    return delivered;
  }

  uint32_t ActiveReaderCount() const {
    return static_cast<uint32_t>(readers_.size());
  }

  MediaFrameReaderId main_reader_id = 0;
  MediaFrameReaderId sub_reader_id = 0;
  int attached_readers = 0;
  int detached_readers = 0;
  int attached_sinks = 0;
  StreamId last_key_frame_stream = StreamId::kMain;
  KeyFrameReason last_key_frame_reason = KeyFrameReason::kRecovery;
  int key_frame_requests = 0;

private:
  struct Reader {
    StreamId stream_id = StreamId::kMain;
    uint64_t generation = 0;
    std::vector<MediaFrame> pending_frames;
  };

  struct Sink {
    StreamId stream_id = StreamId::kMain;
    IFrameSink *sink = nullptr;
  };

  static MediaTrack TrackForStream(StreamId stream_id) {
    MediaTrack track;
    track.stream_id = stream_id;
    track.codec = VideoCodec::kH264;
    track.clock_rate = 90000;
    track.codec_generation = 1;
    track.ready = true;
    track.sps.assign(1, '\x67');
    track.pps.assign(1, '\x68');
    return track;
  }

  void ClearAllReaders() {
    for (auto &item : readers_) {
      ClearFrames(&item.second.pending_frames);
    }
    readers_.clear();
    main_reader_id = 0;
    sub_reader_id = 0;
  }

  static void ClearFrames(std::vector<MediaFrame> *frames) {
    if (frames == nullptr) {
      return;
    }
    for (MediaFrame &frame : *frames) {
      MediaFrameUnref(&frame);
    }
    frames->clear();
  }

  MediaFrameReaderId next_reader_id_ = 1;
  FrameAttachId next_sink_id_ = 1;
  uint64_t next_generation_ = 1;
  std::map<MediaFrameReaderId, Reader> readers_;
  std::map<FrameAttachId, Sink> sinks_;
};

}  // namespace test
}  // namespace live_stream

#endif  // LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_SOURCE_H_

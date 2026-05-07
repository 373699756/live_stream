#include "webrtc_engine.h"

#include "webrtc_sdp.h"

#include <yangrtc/YangPeerConnection8.h>
#include <yangrtc/YangPeerInfo.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {
namespace webrtc_internal {
namespace {

constexpr std::size_t kAnswerSdpCapacity = 16 * 1024;
constexpr uint32_t kVideoPacketCapacity = 1024;

bool StartsWith(const std::string &text, const char *prefix) {
  const std::string expected(prefix);
  return text.compare(0, expected.size(), expected) == 0;
}

bool IsSupportedCodec(VideoCodec codec) {
  return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

bool HasValidPayload(const EncodedFrame &frame) {
  return frame.buffer != nullptr && frame.size > 0 &&
         frame.offset <= frame.buffer->Size() &&
         frame.size <= frame.buffer->Size() - frame.offset;
}

YangVideoCodec ToYangVideoCodec(VideoCodec codec) {
  return codec == VideoCodec::kH265 ? Yang_VED_H265 : Yang_VED_H264;
}

int32_t ToYangFrameType(FrameType frame_type) {
  switch (frame_type) {
  case FrameType::kIdr:
  case FrameType::kI:
    return YANG_Frametype_I;
  case FrameType::kB:
    return YANG_Frametype_B;
  case FrameType::kJpeg:
  case FrameType::kP:
  default:
    return YANG_Frametype_P;
  }
}

void CopyCString(const std::string &value, char *target, std::size_t size) {
  if (target == nullptr || size == 0) {
    return;
  }
  std::snprintf(target, size, "%s", value.c_str());
}

bool ParsePort(const std::string &value, uint16_t *port) {
  if (port == nullptr || value.empty()) {
    return false;
  }
  uint32_t parsed = 0;
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10 + static_cast<uint32_t>(ch - '0');
    if (parsed > 65535U) {
      return false;
    }
  }
  *port = static_cast<uint16_t>(parsed);
  return true;
}

bool ParseIceServerUrl(const std::string &url, std::string *scheme,
                       std::string *host, uint16_t *port) {
  if (scheme == nullptr || host == nullptr || port == nullptr) {
    return false;
  }
  const std::size_t scheme_end = url.find(':');
  if (scheme_end == std::string::npos) {
    return false;
  }
  *scheme = url.substr(0, scheme_end);
  if (*scheme != "stun" && *scheme != "turn") {
    return false;
  }

  std::string endpoint = url.substr(scheme_end + 1);
  if (StartsWith(endpoint, "//")) {
    endpoint.erase(0, 2);
  }
  const std::size_t query_pos = endpoint.find('?');
  if (query_pos != std::string::npos) {
    endpoint.erase(query_pos);
  }
  const std::size_t at_pos = endpoint.rfind('@');
  if (at_pos != std::string::npos) {
    endpoint.erase(0, at_pos + 1);
  }
  if (endpoint.empty()) {
    return false;
  }

  if (endpoint.front() == '[') {
    const std::size_t close = endpoint.find(']');
    if (close == std::string::npos) {
      return false;
    }
    *host = endpoint.substr(1, close - 1);
    if (close + 1 < endpoint.size() && endpoint[close + 1] == ':') {
      return ParsePort(endpoint.substr(close + 2), port);
    }
    *port = 3478;
    return true;
  }

  const std::size_t colon = endpoint.rfind(':');
  if (colon == std::string::npos || endpoint.find(':') != colon) {
    *host = endpoint;
    *port = 3478;
    return !host->empty();
  }

  *host = endpoint.substr(0, colon);
  return !host->empty() && ParsePort(endpoint.substr(colon + 1), port);
}

std::string NormalizeMetaRtcSdp(const std::string &value) {
  std::string normalized = value;
  std::size_t pos = 0;
  while ((pos = normalized.find("\\r\\n", pos)) != std::string::npos) {
    normalized.replace(pos, 4, "\r\n");
    pos += 2;
  }
  return normalized;
}

std::string RewriteLocalCandidates(const std::string &sdp,
                                   const std::string &public_ip) {
  if (public_ip.empty()) {
    return sdp;
  }

  std::string rewritten;
  std::size_t cursor = 0;
  while (cursor < sdp.size()) {
    std::size_t line_end = sdp.find("\r\n", cursor);
    const std::string line =
        sdp.substr(cursor, line_end == std::string::npos ? std::string::npos
                                                         : line_end - cursor);
    if (StartsWith(line, "a=candidate:")) {
      rewritten += "a=";
      rewritten += ReplaceHostCandidateIp(line.substr(2), public_ip);
    } else {
      rewritten += line;
    }
    if (line_end == std::string::npos) {
      break;
    }
    rewritten += "\r\n";
    cursor = line_end + 2;
  }
  return rewritten;
}

struct MetaRtcPeerSession {
  WebrtcPeerInfo peer;
  std::unique_ptr<YangPeerConnection8> connection;
  std::unique_ptr<YangRtcPacer> pacer;
  bool local_description_started = false;
};

class MetaRtcEngine : public IWebrtcEngine {
public:
  const char *Name() const override { return "metaRTC"; }
  bool Available() const override { return true; }

  bool Start(const WebrtcServiceOptions &options) override {
    options_ = options;
    next_local_port_ = options.local_port_base;
    peers_.clear();
    return true;
  }

  void Stop() override { peers_.clear(); }

  bool CreatePeer(const WebrtcPeerInfo &peer) override {
    if (peer.peer_id.empty() || !IsSupportedCodec(peer.codec) ||
        peers_.find(peer.peer_id) != peers_.end()) {
      return false;
    }

    YangPeerInfo peer_info;
    std::memset(&peer_info, 0, sizeof(peer_info));
    yang_init_peerInfo(&peer_info);
    peer_info.uid = static_cast<int32_t>(peers_.size() + 1);
    peer_info.direction = YangSendonly;
    peer_info.rtc.sessionTimeout =
        static_cast<int32_t>(options_.session_timeout_ms) * 1000;
    peer_info.rtc.enableSdpCandidate = yangtrue;
    peer_info.rtc.rtcSocketProtocol = options_.prefer_tcp
                                          ? Yang_Socket_Protocol_Tcp
                                          : Yang_Socket_Protocol_Udp;
    peer_info.rtc.turnSocketProtocol = peer_info.rtc.rtcSocketProtocol;
    peer_info.rtc.rtcLocalPort = static_cast<int32_t>(next_local_port_++);
    peer_info.pushVideo.videoEncoderType = ToYangVideoCodec(peer.codec);
    if (!options_.ice_servers.empty()) {
      std::string scheme;
      std::string host;
      uint16_t port = 0;
      if (ParseIceServerUrl(options_.ice_servers.front().url, &scheme, &host,
                            &port)) {
        peer_info.rtc.iceCandidateType =
            scheme == "turn" ? YangIceRelayed : YangIceServerReflexive;
        CopyCString(host, peer_info.rtc.iceServerIP,
                    sizeof(peer_info.rtc.iceServerIP));
        CopyCString(options_.ice_servers.front().username,
                    peer_info.rtc.iceUserName,
                    sizeof(peer_info.rtc.iceUserName));
        CopyCString(options_.ice_servers.front().credential,
                    peer_info.rtc.icePassword,
                    sizeof(peer_info.rtc.icePassword));
        peer_info.rtc.iceServerPort = static_cast<int32_t>(port);
      }
    }

    std::unique_ptr<YangPeerConnection8> connection(new YangPeerConnection8(
        &peer_info, nullptr, nullptr, nullptr, nullptr));
    if (connection->addVideoTrack(ToYangVideoCodec(peer.codec)) != Yang_Ok ||
        connection->addTransceiver(YangMediaVideo, YangSendonly) != Yang_Ok) {
      return false;
    }

    std::unique_ptr<YangRtcPacer> pacer(new YangRtcPacer());
    if (pacer->initVideo(ToYangVideoCodec(peer.codec), kVideoPacketCapacity) !=
        Yang_Ok) {
      return false;
    }

    std::unique_ptr<MetaRtcPeerSession> session(new MetaRtcPeerSession());
    session->peer = peer;
    session->connection = std::move(connection);
    session->pacer = std::move(pacer);
    peers_[peer.peer_id] = std::move(session);
    return true;
  }

  std::string HandleOffer(const WebrtcPeerInfo &peer,
                          const std::string &offer_sdp) override {
    auto it = peers_.find(peer.peer_id);
    if (it == peers_.end() || offer_sdp.empty()) {
      return std::string();
    }

    std::vector<char> offer(offer_sdp.begin(), offer_sdp.end());
    offer.push_back('\0');
    if (it->second->connection->setRemoteDescription(offer.data()) != Yang_Ok) {
      return std::string();
    }
    if (!it->second->local_description_started) {
      char local_marker[] = "metaRTC-local";
      if (it->second->connection->setLocalDescription(local_marker) !=
          Yang_Ok) {
        return std::string();
      }
      it->second->local_description_started = true;
    }

    std::vector<char> answer(kAnswerSdpCapacity, 0);
    if (it->second->connection->createAnswer(answer.data()) != Yang_Ok) {
      return std::string();
    }
    return RewriteLocalCandidates(
        NormalizeMetaRtcSdp(std::string(answer.data())), options_.public_ip);
  }

  bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
    auto it = peers_.find(candidate.peer_id);
    if (it == peers_.end()) {
      return false;
    }
    std::vector<char> candidate_text(candidate.candidate.begin(),
                                     candidate.candidate.end());
    candidate_text.push_back('\0');
    return it->second->connection->addIceCandidate(candidate_text.data()) ==
           Yang_Ok;
  }

  bool ClosePeer(const std::string &peer_id) override {
    peers_.erase(peer_id);
    return true;
  }

  bool SendFrame(const WebrtcPeerInfo &peer,
                 const EncodedFrame &frame) override {
    auto it = peers_.find(peer.peer_id);
    if (it == peers_.end() || !HasValidPayload(frame) ||
        !IsSupportedCodec(frame.codec) ||
        frame.codec != it->second->peer.codec) {
      return false;
    }
    YangFrame video_frame;
    std::memset(&video_frame, 0, sizeof(video_frame));
    video_frame.mediaType = YangFrameTypeVideo;
    video_frame.frametype = ToYangFrameType(frame.frame_type);
    video_frame.nb = static_cast<int32_t>(frame.size);
    video_frame.pts =
        frame.pts_us > 0 ? static_cast<uint64_t>(frame.pts_us) : 0;
    video_frame.payload =
        const_cast<uint8_t *>(frame.buffer->Data() + frame.offset);

    YangPushData *push_data = it->second->pacer->getVideoData(&video_frame);
    if (push_data == nullptr) {
      return false;
    }
    ++sent_frames_;
    return it->second->connection->on_video(push_data) == Yang_Ok;
  }

private:
  WebrtcServiceOptions options_;
  uint16_t next_local_port_ = 0;
  std::map<std::string, std::unique_ptr<MetaRtcPeerSession>> peers_;
  uint64_t sent_frames_ = 0;
};

class FakeWebrtcEngine : public IWebrtcEngine {
public:
  const char *Name() const override { return "fake_webrtc"; }
  bool Available() const override { return true; }

  bool Start(const WebrtcServiceOptions &options) override {
    (void)options;
    return true;
  }

  void Stop() override { peers_.clear(); }

  bool CreatePeer(const WebrtcPeerInfo &peer) override {
    peers_[peer.peer_id] = peer;
    return true;
  }

  std::string HandleOffer(const WebrtcPeerInfo &peer,
                          const std::string &offer_sdp) override {
    if (peers_.find(peer.peer_id) == peers_.end() || offer_sdp.empty()) {
      return std::string();
    }
    return "v=0\r\ns=fake-webrtc-answer\r\n";
  }

  bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
    last_candidate_json_ = BuildCandidateJson(candidate);
    return peers_.find(candidate.peer_id) != peers_.end();
  }

  bool ClosePeer(const std::string &peer_id) override {
    peers_.erase(peer_id);
    return true;
  }

  bool SendFrame(const WebrtcPeerInfo &peer,
                 const EncodedFrame &frame) override {
    if (peers_.find(peer.peer_id) == peers_.end() || !frame.buffer ||
        frame.size == 0) {
      return false;
    }
    ++sent_frames_;
    return true;
  }

private:
  std::map<std::string, WebrtcPeerInfo> peers_;
  std::string last_candidate_json_;
  uint64_t sent_frames_ = 0;
};

} // namespace

std::unique_ptr<IWebrtcEngine> CreateEngine(bool use_fake_engine) {
  if (use_fake_engine) {
    return std::unique_ptr<IWebrtcEngine>(new FakeWebrtcEngine());
  }
  return std::unique_ptr<IWebrtcEngine>(new MetaRtcEngine());
}

} // namespace webrtc_internal
} // namespace live_stream

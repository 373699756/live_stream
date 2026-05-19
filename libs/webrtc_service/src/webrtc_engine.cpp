#include "webrtc_engine.h"

#include "stream_codec.h"
#include "webrtc_sdp.h"

#include <yangrtc/YangPeerConnection8.h>
#include <yangrtc/YangPeerInfo.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace live_stream {
namespace webrtc_internal {
namespace {

constexpr std::size_t kAnswerSdpCapacity = 16 * 1024;
constexpr uint32_t kVideoPacketCapacity = 1024;

struct VideoParameterCache {
    std::vector<uint8_t> h264_sps;
    std::vector<uint8_t> h264_pps;
    std::vector<uint8_t> h265_vps;
    std::vector<uint8_t> h265_sps;
    std::vector<uint8_t> h265_pps;
};

bool StartsWith(const std::string &text, const char *prefix) {
    const std::string expected(prefix);
    return text.compare(0, expected.size(), expected) == 0;
}

bool IsSupportedCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
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

bool IsH264VclNal(uint8_t nal_type) {
    return nal_type >= 1 && nal_type <= 5;
}

bool IsH265VclNal(uint8_t nal_type) { return nal_type <= 31; }

bool IsH265KeyFrameNal(uint8_t nal_type) {
    return nal_type >= 16 && nal_type <= 21;
}

template <typename Unit>
void AppendAnnexBNal(const Unit &unit, std::vector<uint8_t> *out) {
    static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    if (unit.data == nullptr || unit.size == 0 || out == nullptr) {
        return;
    }
    out->insert(out->end(), kStartCode, kStartCode + sizeof(kStartCode));
    out->insert(out->end(), unit.data, unit.data + unit.size);
}

void StoreParameterSet(const uint8_t *data,
                       std::size_t size,
                       std::vector<uint8_t> *out) {
    if (data == nullptr || size == 0 || out == nullptr) {
        return;
    }
    out->assign(data, data + size);
}

YangPushData *QueueMetaRtcPayload(YangRtcPacer *pacer,
                                  const uint8_t *payload,
                                  std::size_t size,
                                  int32_t frame_type,
                                  uint64_t pts_us) {
    if (pacer == nullptr || payload == nullptr || size == 0 ||
        size > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
        return nullptr;
    }

    YangFrame video_frame;
    std::memset(&video_frame, 0, sizeof(video_frame));
    video_frame.mediaType = YangFrameTypeVideo;
    video_frame.frametype = frame_type;
    video_frame.nb = static_cast<int32_t>(size);
    video_frame.pts = pts_us;
    video_frame.payload = const_cast<uint8_t *>(payload);
    return pacer->getVideoData(&video_frame);
}

YangPushData *QueueH264Frame(YangRtcPacer *pacer,
                             const EncodedFrame &frame,
                             VideoParameterCache *cache) {
    const uint8_t *data = frame.PayloadData();
    if (data == nullptr) {
        return nullptr;
    }
    const std::vector<stream_codec::H264NalUnit> nals =
        stream_codec::ParseH264AnnexBNalUnits(data, frame.size);
    const uint64_t pts_us = frame.pts_us > 0 ? static_cast<uint64_t>(frame.pts_us)
                                             : 0;
    if (nals.empty()) {
        std::size_t size = frame.size;
        stream_codec::StripAnnexBStartCode(&data, &size);
        return QueueMetaRtcPayload(pacer, data, size,
                                   ToYangFrameType(frame.frame_type), pts_us);
    }

    const stream_codec::H264NalUnit *sps = nullptr;
    const stream_codec::H264NalUnit *pps = nullptr;
    for (const stream_codec::H264NalUnit &nal : nals) {
        if (nal.type == 7) {
            sps = &nal;
            if (cache != nullptr) {
                StoreParameterSet(nal.data, nal.size, &cache->h264_sps);
            }
        } else if (nal.type == 8) {
            pps = &nal;
            if (cache != nullptr) {
                StoreParameterSet(nal.data, nal.size, &cache->h264_pps);
            }
        }
    }
    const uint8_t *sps_data = sps != nullptr ? sps->data : nullptr;
    std::size_t sps_size = sps != nullptr ? sps->size : 0;
    const uint8_t *pps_data = pps != nullptr ? pps->data : nullptr;
    std::size_t pps_size = pps != nullptr ? pps->size : 0;
    if (sps_data == nullptr && cache != nullptr && !cache->h264_sps.empty()) {
        sps_data = cache->h264_sps.data();
        sps_size = cache->h264_sps.size();
    }
    if (pps_data == nullptr && cache != nullptr && !cache->h264_pps.empty()) {
        pps_data = cache->h264_pps.data();
        pps_size = cache->h264_pps.size();
    }

    std::vector<uint8_t> idr_payload;
    YangPushData *push_data = nullptr;
    bool sent_idr_with_meta = false;
    for (const stream_codec::H264NalUnit &nal : nals) {
        if (!IsH264VclNal(nal.type)) {
            continue;
        }
        if (nal.type == 5 && !sent_idr_with_meta && sps_data != nullptr &&
            pps_data != nullptr) {
            idr_payload.clear();
            idr_payload.reserve(12 + sps_size + pps_size + nal.size);
            AppendAnnexBNal(stream_codec::H264NalUnit{sps_data, sps_size, 7},
                            &idr_payload);
            AppendAnnexBNal(stream_codec::H264NalUnit{pps_data, pps_size, 8},
                            &idr_payload);
            AppendAnnexBNal(nal, &idr_payload);
            push_data = QueueMetaRtcPayload(pacer, idr_payload.data(),
                                            idr_payload.size(),
                                            YANG_Frametype_I, pts_us);
            sent_idr_with_meta = push_data != nullptr;
            continue;
        }
        const int32_t frame_type =
            nal.type == 5 ? YANG_Frametype_I : YANG_Frametype_P;
        YangPushData *current_push_data =
            QueueMetaRtcPayload(pacer, nal.data, nal.size, frame_type, pts_us);
        if (current_push_data != nullptr) {
            push_data = current_push_data;
        }
    }
    return push_data;
}

YangPushData *QueueH265Frame(YangRtcPacer *pacer,
                             const EncodedFrame &frame,
                             VideoParameterCache *cache) {
    const uint8_t *data = frame.PayloadData();
    if (data == nullptr) {
        return nullptr;
    }
    const std::vector<stream_codec::H265NalUnit> nals =
        stream_codec::ParseH265AnnexBNalUnits(data, frame.size);
    const uint64_t pts_us = frame.pts_us > 0 ? static_cast<uint64_t>(frame.pts_us)
                                             : 0;
    if (nals.empty()) {
        std::size_t size = frame.size;
        stream_codec::StripAnnexBStartCode(&data, &size);
        return QueueMetaRtcPayload(pacer, data, size,
                                   ToYangFrameType(frame.frame_type), pts_us);
    }

    const stream_codec::H265NalUnit *vps = nullptr;
    const stream_codec::H265NalUnit *sps = nullptr;
    const stream_codec::H265NalUnit *pps = nullptr;
    for (const stream_codec::H265NalUnit &nal : nals) {
        if (nal.type == 32) {
            vps = &nal;
            if (cache != nullptr) {
                StoreParameterSet(nal.data, nal.size, &cache->h265_vps);
            }
        } else if (nal.type == 33) {
            sps = &nal;
            if (cache != nullptr) {
                StoreParameterSet(nal.data, nal.size, &cache->h265_sps);
            }
        } else if (nal.type == 34) {
            pps = &nal;
            if (cache != nullptr) {
                StoreParameterSet(nal.data, nal.size, &cache->h265_pps);
            }
        }
    }
    const uint8_t *vps_data = vps != nullptr ? vps->data : nullptr;
    std::size_t vps_size = vps != nullptr ? vps->size : 0;
    const uint8_t *sps_data = sps != nullptr ? sps->data : nullptr;
    std::size_t sps_size = sps != nullptr ? sps->size : 0;
    const uint8_t *pps_data = pps != nullptr ? pps->data : nullptr;
    std::size_t pps_size = pps != nullptr ? pps->size : 0;
    if (vps_data == nullptr && cache != nullptr && !cache->h265_vps.empty()) {
        vps_data = cache->h265_vps.data();
        vps_size = cache->h265_vps.size();
    }
    if (sps_data == nullptr && cache != nullptr && !cache->h265_sps.empty()) {
        sps_data = cache->h265_sps.data();
        sps_size = cache->h265_sps.size();
    }
    if (pps_data == nullptr && cache != nullptr && !cache->h265_pps.empty()) {
        pps_data = cache->h265_pps.data();
        pps_size = cache->h265_pps.size();
    }

    std::vector<uint8_t> key_payload;
    YangPushData *push_data = nullptr;
    bool sent_key_frame_with_meta = false;
    for (const stream_codec::H265NalUnit &nal : nals) {
        if (!IsH265VclNal(nal.type)) {
            continue;
        }
        if (IsH265KeyFrameNal(nal.type) && !sent_key_frame_with_meta &&
            vps_data != nullptr && sps_data != nullptr && pps_data != nullptr) {
            key_payload.clear();
            key_payload.reserve(16 + vps_size + sps_size + pps_size + nal.size);
            AppendAnnexBNal(stream_codec::H265NalUnit{vps_data, vps_size, 32},
                            &key_payload);
            AppendAnnexBNal(stream_codec::H265NalUnit{sps_data, sps_size, 33},
                            &key_payload);
            AppendAnnexBNal(stream_codec::H265NalUnit{pps_data, pps_size, 34},
                            &key_payload);
            AppendAnnexBNal(nal, &key_payload);
            push_data = QueueMetaRtcPayload(pacer, key_payload.data(),
                                            key_payload.size(),
                                            YANG_Frametype_I, pts_us);
            sent_key_frame_with_meta = push_data != nullptr;
            continue;
        }
        const int32_t frame_type =
            IsH265KeyFrameNal(nal.type) ? YANG_Frametype_I : YANG_Frametype_P;
        YangPushData *current_push_data =
            QueueMetaRtcPayload(pacer, nal.data, nal.size, frame_type, pts_us);
        if (current_push_data != nullptr) {
            push_data = current_push_data;
        }
    }
    return push_data;
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

class MetaRtcEngine;
class MetaRtcPeerCallbacks;

struct MetaRtcPeerSession {
    WebrtcPeerInfo peer;
    std::shared_ptr<MetaRtcPeerCallbacks> callbacks;
    std::unique_ptr<YangPeerConnection8> connection;
    std::unique_ptr<YangRtcPacer> pacer;
    VideoParameterCache parameters;
    std::mutex mutex;
};

class MetaRtcPeerCallbacks : public YangCallbackIce,
                             public YangCallbackRtc,
                             public YangCallbackSslAlert {
public:
    MetaRtcPeerCallbacks(MetaRtcEngine *engine, std::string peer_id)
        : engine_(engine), peer_id_(std::move(peer_id)) {}

    void onIceStateChange(int32_t uid, YangIceCandidateState ice_state) override {
        (void)uid;
        (void)ice_state;
    }

    void
    onConnectionStateChange(int32_t uid,
                            YangRtcConnectionState connection_state) override;

    void onIceCandidate(int32_t uid, char *sdp) override {
        (void)uid;
        (void)sdp;
    }

    void onIceGatheringState(int32_t uid,
                             YangIceGatheringState gather_state) override {
        (void)uid;
        (void)gather_state;
    }

    void setMediaConfig(int32_t uid, YangAudioParam *audio,
                        YangVideoParam *video) override {
        (void)uid;
        (void)audio;
        (void)video;
    }

    void sendRequest(int32_t uid, uint32_t ssrc, YangRequestType req) override;

    void sslCloseAlert(int32_t uid) override;

private:
    MetaRtcEngine *engine_ = nullptr;
    std::string peer_id_;
};

class MetaRtcEngine : public IWebrtcEngine {
public:
    const char *Name() const override { return "metaRTC"; }
    bool Available() const override { return true; }

    bool Start(const WebrtcServiceOptions &options,
               const WebrtcEngineCallbacks &callbacks) override {
        std::map<std::string, std::shared_ptr<MetaRtcPeerSession>> peers_to_drop;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            options_ = options;
            callbacks_ = callbacks;
            next_local_port_ = options.local_port_base;
            peers_to_drop.swap(peers_);
            sent_frames_ = 0;
        }
        peers_to_drop.clear();
        return true;
    }

    void Stop() override {
        std::map<std::string, std::shared_ptr<MetaRtcPeerSession>> peers_to_drop;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peers_to_drop.swap(peers_);
            sent_frames_ = 0;
        }
        peers_to_drop.clear();
    }

    bool CreatePeer(const WebrtcPeerInfo &peer) override {
        WebrtcServiceOptions options;
        uint16_t local_port = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peer.peer_id.empty() || !IsSupportedCodec(peer.codec) ||
                peers_.find(peer.peer_id) != peers_.end()) {
                return false;
            }
            options = options_;
            local_port = next_local_port_++;
        }

        YangPeerInfo peer_info;
        std::memset(&peer_info, 0, sizeof(peer_info));
        yang_init_peerInfo(&peer_info);
        peer_info.uid = static_cast<int32_t>(local_port);
        peer_info.direction = YangSendonly;
        peer_info.rtc.sessionTimeout =
            static_cast<int32_t>(options.session_timeout_ms) * 1000;
        peer_info.rtc.enableSdpCandidate = yangtrue;
        peer_info.rtc.rtcSocketProtocol = options.prefer_tcp
                                              ? Yang_Socket_Protocol_Tcp
                                              : Yang_Socket_Protocol_Udp;
        peer_info.rtc.turnSocketProtocol = peer_info.rtc.rtcSocketProtocol;
        peer_info.rtc.rtcLocalPort = static_cast<int32_t>(local_port);
        peer_info.pushVideo.videoEncoderType = ToYangVideoCodec(peer.codec);
        if (!options.ice_servers.empty()) {
            std::string scheme;
            std::string host;
            uint16_t port = 0;
            if (ParseIceServerUrl(options.ice_servers.front().url, &scheme, &host,
                                  &port)) {
                peer_info.rtc.iceCandidateType =
                    scheme == "turn" ? YangIceRelayed : YangIceServerReflexive;
                CopyCString(host, peer_info.rtc.iceServerIP,
                            sizeof(peer_info.rtc.iceServerIP));
                CopyCString(options.ice_servers.front().username,
                            peer_info.rtc.iceUserName,
                            sizeof(peer_info.rtc.iceUserName));
                CopyCString(options.ice_servers.front().credential,
                            peer_info.rtc.icePassword,
                            sizeof(peer_info.rtc.icePassword));
                peer_info.rtc.iceServerPort = static_cast<int32_t>(port);
            }
        }

        std::shared_ptr<MetaRtcPeerCallbacks> callbacks(
            new MetaRtcPeerCallbacks(this, peer.peer_id));
        std::unique_ptr<YangPeerConnection8> connection(
            new YangPeerConnection8(&peer_info, nullptr, callbacks.get(),
                                    callbacks.get(), callbacks.get()));
        if (connection->addVideoTrack(ToYangVideoCodec(peer.codec)) != Yang_Ok ||
            connection->addTransceiver(YangMediaVideo, YangSendonly) != Yang_Ok) {
            return false;
        }

        std::unique_ptr<YangRtcPacer> pacer(new YangRtcPacer());
        if (pacer->initVideo(ToYangVideoCodec(peer.codec), kVideoPacketCapacity) !=
            Yang_Ok) {
            return false;
        }

        std::shared_ptr<MetaRtcPeerSession> session(new MetaRtcPeerSession());
        session->peer = peer;
        session->callbacks = callbacks;
        session->connection = std::move(connection);
        session->pacer = std::move(pacer);

        std::lock_guard<std::mutex> guard(mutex_);
        if (peers_.find(peer.peer_id) != peers_.end()) {
            return false;
        }
        peers_[peer.peer_id] = session;
        return true;
    }

    std::string HandleOffer(const WebrtcPeerInfo &peer,
                            const std::string &offer_sdp) override {
        std::shared_ptr<MetaRtcPeerSession> session;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer.peer_id);
            if (it == peers_.end()) {
                return std::string();
            }
            session = it->second;
        }
        if (!session || offer_sdp.empty()) {
            return std::string();
        }

        std::vector<char> answer(kAnswerSdpCapacity, 0);
        {
            std::lock_guard<std::mutex> session_guard(session->mutex);
            std::vector<char> offer(offer_sdp.begin(), offer_sdp.end());
            offer.push_back('\0');
            if (session->connection->setRemoteDescription(offer.data()) != Yang_Ok) {
                return std::string();
            }
            if (session->connection->createAnswer(answer.data()) != Yang_Ok) {
                return std::string();
            }
            if (session->connection->setLocalDescription(answer.data()) != Yang_Ok) {
                return std::string();
            }
        }
        return RewriteLocalCandidates(
            NormalizeMetaRtcSdp(std::string(answer.data())), options_.public_ip);
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        std::shared_ptr<MetaRtcPeerSession> session;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(candidate.peer_id);
            if (it == peers_.end()) {
                return false;
            }
            session = it->second;
        }

        const std::string candidate_json = BuildCandidateJson(candidate);
        std::vector<char> candidate_text(candidate_json.begin(),
                                         candidate_json.end());
        candidate_text.push_back('\0');
        std::lock_guard<std::mutex> session_guard(session->mutex);
        return session->connection->addIceCandidate(candidate_text.data()) ==
               Yang_Ok;
    }

    bool ClosePeer(const std::string &peer_id) override {
        std::shared_ptr<MetaRtcPeerSession> session;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end()) {
                return false;
            }
            session = it->second;
            peers_.erase(it);
        }
        session.reset();
        return true;
    }

    bool SendFrame(const WebrtcPeerInfo &peer,
                   const EncodedFrame &frame) override {
        std::shared_ptr<MetaRtcPeerSession> session;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer.peer_id);
            if (it == peers_.end()) {
                return false;
            }
            session = it->second;
        }
        if (!session || !frame.HasValidPayload() ||
            !IsSupportedCodec(frame.codec) || frame.codec != session->peer.codec) {
            return false;
        }

        {
            std::lock_guard<std::mutex> session_guard(session->mutex);
            YangPushData *push_data =
                frame.codec == VideoCodec::kH265
                    ? QueueH265Frame(session->pacer.get(), frame,
                                     &session->parameters)
                    : QueueH264Frame(session->pacer.get(), frame,
                                     &session->parameters);
            if (push_data == nullptr) {
                return false;
            }
            if (session->connection->on_video(push_data) != Yang_Ok) {
                return false;
            }
        }

        std::lock_guard<std::mutex> guard(mutex_);
        ++sent_frames_;
        return true;
    }

    void NotifyPeerState(const char *peer_id, WebrtcPeerState state) {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerStateChanged != nullptr && peer_id != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer_id, state);
        }
    }

    void NotifyKeyFrameRequest(const char *peer_id) {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerKeyFrameRequested != nullptr && peer_id != nullptr) {
            callbacks.OnPeerKeyFrameRequested(callbacks.user, peer_id);
        }
    }

private:
    mutable std::mutex mutex_;
    WebrtcEngineCallbacks callbacks_;
    WebrtcServiceOptions options_;
    uint16_t next_local_port_ = 0;
    std::map<std::string, std::shared_ptr<MetaRtcPeerSession>> peers_;
    uint64_t sent_frames_ = 0;
};

void MetaRtcPeerCallbacks::onConnectionStateChange(
    int32_t uid, YangRtcConnectionState connection_state) {
    (void)uid;
    if (engine_ == nullptr) {
        return;
    }
    switch (connection_state) {
        case Yang_Conn_State_Connecting:
            engine_->NotifyPeerState(peer_id_.c_str(), WebrtcPeerState::kConnecting);
            break;
        case Yang_Conn_State_Connected:
            engine_->NotifyPeerState(peer_id_.c_str(), WebrtcPeerState::kConnected);
            break;
        case Yang_Conn_State_Failed:
            engine_->NotifyPeerState(peer_id_.c_str(), WebrtcPeerState::kFailed);
            break;
        case Yang_Conn_State_Disconnected:
        case Yang_Conn_State_Closed:
            engine_->NotifyPeerState(peer_id_.c_str(), WebrtcPeerState::kClosed);
            break;
        case Yang_Conn_State_New:
        default:
            break;
    }
}

void MetaRtcPeerCallbacks::sendRequest(int32_t uid, uint32_t ssrc,
                                       YangRequestType req) {
    (void)uid;
    (void)ssrc;
    if (engine_ == nullptr) {
        return;
    }
    if (req == Yang_Req_Sendkeyframe) {
        engine_->NotifyKeyFrameRequest(peer_id_.c_str());
    } else if (req == Yang_Req_Connected) {
        engine_->NotifyPeerState(peer_id_.c_str(), WebrtcPeerState::kConnected);
    }
}

void MetaRtcPeerCallbacks::sslCloseAlert(int32_t uid) {
    (void)uid;
    if (engine_ != nullptr) {
        engine_->NotifyPeerState(peer_id_.c_str(), WebrtcPeerState::kClosed);
    }
}

class FakeWebrtcEngine : public IWebrtcEngine {
public:
    const char *Name() const override { return "fake_webrtc"; }
    bool Available() const override { return true; }

    bool Start(const WebrtcServiceOptions &options,
               const WebrtcEngineCallbacks &callbacks) override {
        (void)options;
        std::lock_guard<std::mutex> guard(mutex_);
        callbacks_ = callbacks;
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> guard(mutex_);
        peers_.clear();
    }

    bool CreatePeer(const WebrtcPeerInfo &peer) override {
        std::lock_guard<std::mutex> guard(mutex_);
        peers_[peer.peer_id] = peer;
        return true;
    }

    std::string HandleOffer(const WebrtcPeerInfo &peer,
                            const std::string &offer_sdp) override {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (peers_.find(peer.peer_id) == peers_.end() || offer_sdp.empty()) {
                return std::string();
            }
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer.peer_id.c_str(),
                                         WebrtcPeerState::kConnected);
        }
        return "v=0\r\ns=fake-webrtc-answer\r\n";
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        std::lock_guard<std::mutex> guard(mutex_);
        last_candidate_json_ = BuildCandidateJson(candidate);
        return peers_.find(candidate.peer_id) != peers_.end();
    }

    bool ClosePeer(const std::string &peer_id) override {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            peers_.erase(peer_id);
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer_id.c_str(),
                                         WebrtcPeerState::kClosed);
        }
        return true;
    }

    bool SendFrame(const WebrtcPeerInfo &peer,
                   const EncodedFrame &frame) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (peers_.find(peer.peer_id) == peers_.end() || !frame.buffer ||
            frame.size == 0) {
            return false;
        }
        ++sent_frames_;
        return true;
    }

private:
    mutable std::mutex mutex_;
    WebrtcEngineCallbacks callbacks_;
    std::map<std::string, WebrtcPeerInfo> peers_;
    std::string last_candidate_json_;
    uint64_t sent_frames_ = 0;
};

}  // namespace

std::unique_ptr<IWebrtcEngine> CreateEngine(bool use_fake_engine) {
    if (use_fake_engine) {
        return std::unique_ptr<IWebrtcEngine>(new FakeWebrtcEngine());
    }
    return std::unique_ptr<IWebrtcEngine>(new MetaRtcEngine());
}

}  // namespace webrtc_internal
}  // namespace live_stream

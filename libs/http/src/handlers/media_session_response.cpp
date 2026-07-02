#include "handlers/media_session_response.h"

#include "http_stream_id_json.h"

#include "media/media_streams.h"
#include "net_stat.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace {

const char *CodecToJsonString(Codec codec) {
    switch (codec) {
        case Codec::kH264:
            return "h264";
        case Codec::kH265:
            return "h265";
        case Codec::kJpeg:
            return "jpeg";
        case Codec::kMjpeg:
            return "mjpeg";
    }
    return "unknown";
}

const char *RtspTransportToJsonString(RtspTransportMode transport) {
    return transport == RtspTransportMode::kUdp ? "udp"
                                                : "tcp_interleaved";
}

const char *WebrtcPeerStateToJsonString(WebrtcPeerState state) {
    switch (state) {
        case WebrtcPeerState::kCreated:
            return "created";
        case WebrtcPeerState::kOfferReceived:
            return "offer_received";
        case WebrtcPeerState::kConnecting:
            return "connecting";
        case WebrtcPeerState::kConnected:
            return "connected";
        case WebrtcPeerState::kClosing:
            return "closing";
        case WebrtcPeerState::kClosed:
            return "closed";
        case WebrtcPeerState::kFailed:
            return "failed";
    }
    return "unknown";
}

void AddHttpStreamingMediaStatus(Json *root,
                                 MediaStreams *media_streams,
                                 StreamId stream_id) {
    if (root == nullptr || media_streams == nullptr) {
        return;
    }
    const MediaStreamInfo stream_info =
        media_streams->GetStreamInfo(stream_id);
    (*root)["media_running"] = stream_info.running;
    (*root)["media_track_ready"] = stream_info.track_ready;
    (*root)["media_codec"] = CodecToJsonString(stream_info.codec);
    (*root)["media_codec_generation"] = stream_info.codec_generation;
    (*root)["media_http_flv_ready"] = stream_info.flv_ready;
    (*root)["media_mjpeg_ready"] = stream_info.mjpeg_ready;
    (*root)["media_last_dts"] = stream_info.last_dts_us;
    (*root)["media_last_reset_reason"] = stream_info.last_reset_reason;
}

const char *NetQueueLevelToJsonString(NetQueueLevel level) {
    switch (level) {
        case NetQueueLevel::kNormal:
            return "normal";
        case NetQueueLevel::kWarning:
            return "warning";
        case NetQueueLevel::kCritical:
            return "critical";
    }
    return "unknown";
}

}  // namespace

Json BuildRtspSessionResponse(const RtspSessionInfo &session) {
    Json root = Json::object();
    root["protocol"] = "rtsp";
    root["session_id"] = std::to_string(session.session_id);
    root["stream"] = StreamIdToJsonString(session.stream_id);
    root["transport"] = RtspTransportToJsonString(session.transport);
    root["remote_address"] = session.remote_address;
    root["local_address"] = session.local_address;
    root["subscription_id"] = session.subscription_id;
    root["subscription_open"] = session.subscription_open;
    root["subscription_generation"] = session.subscription_generation;
    root["subscription_pending_frames"] = session.subscription_pending_frames;
    root["subscription_waiting_keyframe"] =
        session.subscription_waiting_keyframe;
    root["subscription_slow"] = session.subscription_slow;
    root["subscription_close_reason"] = session.subscription_close_reason;
    root["pending_bytes"] = session.pending_bytes;
    root["rtp_packets"] = session.rtp_packets;
    root["rtp_bytes"] = session.rtp_bytes;
    root["rtcp_packets"] = session.rtcp_packets;
    root["rtcp_bytes"] = session.rtcp_bytes;
    root["last_rtcp_ms"] = session.last_rtcp_ms;
    root["close_reason"] = session.close_reason;
    return root;
}

Json BuildWebrtcSessionResponse(const WebrtcPeerInfo &peer) {
    Json root = Json::object();
    root["protocol"] = "webrtc";
    root["session_id"] = peer.peer_id;
    root["peer_id"] = peer.peer_id;
    root["stream"] = StreamIdToJsonString(peer.stream_id);
    root["state"] = WebrtcPeerStateToJsonString(peer.state);
    root["client_ip"] = peer.client_ip;
    root["user_name"] = peer.user_name;
    root["subscription_id"] = peer.subscription_id;
    root["subscription_open"] = peer.subscription_open;
    root["subscription_generation"] = peer.subscription_generation;
    root["subscription_pending_frames"] = peer.subscription_pending_frames;
    root["subscription_waiting_keyframe"] =
        peer.subscription_waiting_keyframe;
    root["subscription_slow"] = peer.subscription_slow;
    root["subscription_close_reason"] = peer.subscription_close_reason;
    root["ice_selected"] = peer.ice_selected;
    root["dtls_state"] = peer.dtls_state;
    root["srtp_ready"] = peer.srtp_ready;
    root["rtp_packets"] = peer.rtp_packets;
    root["rtp_bytes"] = peer.rtp_bytes;
    root["rtcp_packets"] = peer.rtcp_packets;
    root["rtcp_bytes"] = peer.rtcp_bytes;
    root["rtcp_pli_packets"] = peer.rtcp_pli_packets;
    root["rtcp_fir_packets"] = peer.rtcp_fir_packets;
    root["rtcp_nack_packets"] = peer.rtcp_nack_packets;
    root["rtcp_transport_cc_packets"] = peer.rtcp_transport_cc_packets;
    root["rtcp_keyframe_requests"] = peer.rtcp_keyframe_requests;
    root["last_error"] = peer.last_error;
    root["created_at_ms"] = peer.created_at_ms;
    root["updated_at_ms"] = peer.updated_at_ms;
    return root;
}

Json BuildHttpStreamingSessionResponse(const HttpStreamSessionInfo &session,
                                       MediaStreams *media_streams) {
    Json root = Json::object();
    root["protocol"] = session.protocol;
    root["session_id"] = session.session_id;
    root["connection_id"] = session.connection_id;
    root["client_id"] = session.client_id;
    root["stream"] = StreamIdToJsonString(session.stream_id);
    root["stream_state"] = session.stream_state;
    if (session.open && session.stream_state == "opening") {
        root["state"] = "opening";
    } else if (session.open && session.stream_state == "closing") {
        root["state"] = "closing";
    } else {
        root["state"] = session.open ? "streaming" : "closed";
    }
    root["client_ip"] = session.client_ip;
    root["remote_address"] = session.remote_address;
    root["local_address"] = session.local_address;
    root["pending_bytes"] = session.pending_bytes;
    root["send_queue_length"] = session.send_queue_length;
    root["last_write_at_ms"] = session.last_write_at_ms;
    root["close_reason"] = session.close_reason;
    AddHttpStreamingMediaStatus(&root, media_streams, session.stream_id);
    return root;
}

bool IsMediaStreamingSession(const HttpStreamSessionInfo &session) {
    return session.protocol == "http_flv" || session.protocol == "mjpeg";
}

void AddWebrtcStatsToResponse(Json *root, const WebrtcStats &stats) {
    if (root == nullptr) {
        return;
    }
    (*root)["webrtc_active_peers"] = stats.active_peers;
    (*root)["webrtc_enabled"] = stats.enabled;
    (*root)["webrtc_signaling_ready"] = stats.signaling_ready;
    (*root)["webrtc_ice_ready"] = stats.ice_ready;
    (*root)["webrtc_dtls_ready"] = stats.dtls_ready;
    (*root)["webrtc_srtp_ready"] = stats.srtp_ready;
    (*root)["webrtc_public_ip"] = stats.public_ip;
    (*root)["webrtc_local_port_base"] = stats.local_port_base;
    (*root)["webrtc_max_peers"] = stats.max_peers;
    (*root)["webrtc_ice_servers"] = stats.ice_servers;
    (*root)["webrtc_selected_ice_pairs"] = stats.selected_ice_pairs;
}

void AddNetStatToResponse(Json *root, INetStat *net_stat) {
    if (root == nullptr || net_stat == nullptr) {
        return;
    }
    // /api/media/sessions 是给 Web 频繁刷新的诊断接口，只输出 NetStat 聚合摘要；
    // 慢客户端详情仍留在 INetStat 查询接口，避免会话列表携带大历史对象。
    const NetStatSnapshot snapshot = net_stat->GetSnapshot();
    (*root)["net_stat_enabled"] = snapshot.enabled;
    (*root)["net_stat_checks"] = snapshot.checks;
    (*root)["net_queue_level"] = NetQueueLevelToJsonString(snapshot.level);
    (*root)["net_queue_level_value"] =
        static_cast<uint32_t>(snapshot.level);
    (*root)["net_queue_checked_connections"] =
        snapshot.checked_connections;
    (*root)["net_queue_tracked"] = snapshot.tracked_connection_queues;
    (*root)["net_queue_warning"] = snapshot.warning_connection_queues;
    (*root)["net_queue_recovering"] =
        snapshot.recovering_connection_queues;
    (*root)["net_queue_critical"] = snapshot.critical_connection_queues;
    (*root)["net_queue_critical_connections"] =
        snapshot.critical_connections;
    (*root)["net_slow_clients"] = snapshot.slow_clients;
    (*root)["net_slow_client_history"] =
        snapshot.slow_client_history_entries;
    (*root)["webrtc_open_peers"] = snapshot.open_webrtc_peers;
}

}  // namespace live_stream

import type { StreamName } from '../core';

export interface WebrtcConfig {
    enabled: boolean;
    local_port_base: number;
    public_ip: string;
    ice_servers: Array<{
        url: string;
        username?: string;
        credential?: string;
    }>;
    max_peers: number;
    prefer_tcp: boolean;
}

export type WebrtcPeerState =
    | 'created'
    | 'offer_received'
    | 'connecting'
    | 'connected'
    | 'closing'
    | 'closed'
    | 'failed'
    | 'unknown';

export interface WebrtcPeerInfo {
    peer_id: string;
    stream: StreamName;
    codec: string;
    state: WebrtcPeerState;
    client_id: string;
    session_id: string;
    user_name: string;
    client_ip: string;
    subscription_id: number;
    subscription_open: boolean;
    subscription_generation: number;
    subscription_pending_frames: number;
    subscription_waiting_keyframe: boolean;
    subscription_slow: boolean;
    subscription_close_reason: string;
    ice_selected: boolean;
    dtls_state: string;
    srtp_ready: boolean;
    rtp_packets: number;
    rtp_bytes: number;
    rtcp_packets: number;
    rtcp_bytes: number;
    rtcp_pli_packets: number;
    rtcp_fir_packets: number;
    rtcp_nack_packets: number;
    rtcp_transport_cc_packets: number;
    rtcp_keyframe_requests: number;
    last_error: string;
    created_at_ms: number;
    updated_at_ms: number;
}

export interface WebrtcOfferAnswer {
    peer_id: string;
    sdp: string;
    state: WebrtcPeerState;
}

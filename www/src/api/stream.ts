// Streaming API: WebRTC signaling, RTSP config, WebRTC config

import { mockRtspConfig, mockWebrtcConfig } from './mock';
import { postJson, requestJson } from './client';
import type { RtspConfig, WebrtcConfig } from './types';

// RTSP & WebRTC read-only config
export function getRtspConfig(): Promise<RtspConfig> {
  return requestJson<RtspConfig>('/api/config/rtsp', mockRtspConfig);
}

export function getWebrtcConfig(): Promise<WebrtcConfig> {
  return requestJson<WebrtcConfig>('/api/config/webrtc', mockWebrtcConfig);
}

// WebRTC signaling
export function createWebrtcPeer(stream: string) {
  return postJson('/api/webrtc/peers', { stream, client_id: 'web' }, {
    peer_id: '',
    stream,
  });
}

export function sendWebrtcOffer(peerId: string, sdp: string) {
  return postJson('/api/webrtc/offer', { peer_id: peerId, sdp }, {
    peer_id: peerId,
    sdp: '',
  });
}

export async function sendWebrtcCandidate(
  peerId: string,
  candidate: RTCIceCandidateInit,
) {
  await postJson('/api/webrtc/candidate', {
    peer_id: peerId,
    candidate: candidate.candidate || '',
    sdp_mid: candidate.sdpMid || '0',
    sdp_mline_index: candidate.sdpMLineIndex || 0,
  }, { ok: true });
}

export async function closeWebrtcPeer(peerId: string) {
  if (!peerId) {
    return;
  }
  try {
    await postJson('/api/webrtc/close', { peer_id: peerId }, { ok: true });
  } catch {
    // Best-effort cleanup.
  }
}

// Streaming API: WebRTC signaling, RTSP config, WebRTC config

import { mockRtspConfig, mockWebrtcConfig } from './mock';
import { postJson, requestJson, type ApiRequestOptions } from './client';
import type { RtspConfig, WebrtcConfig } from './types';

// RTSP & WebRTC read-only config
export function getRtspConfig(): Promise<RtspConfig> {
  return requestJson<RtspConfig>('/api/config/rtsp', mockRtspConfig);
}

export function getWebrtcConfig(
  init?: ApiRequestOptions,
): Promise<WebrtcConfig> {
  return requestJson<WebrtcConfig>('/api/config/webrtc', mockWebrtcConfig, init);
}

// WebRTC signaling
export function createWebrtcPeer(stream: string, init?: ApiRequestOptions) {
  return postJson(
    '/api/webrtc/peers',
    { stream, client_id: 'web' },
    {
      peer_id: '',
      stream,
    },
    init,
  );
}

export function sendWebrtcOffer(
  peerId: string,
  sdp: string,
  init?: ApiRequestOptions,
) {
  return postJson(
    '/api/webrtc/offer',
    { peer_id: peerId, sdp },
    {
      peer_id: peerId,
      sdp: '',
    },
    init,
  );
}

export async function sendWebrtcCandidate(
  peerId: string,
  candidate: RTCIceCandidateInit,
  init?: ApiRequestOptions,
) {
  await postJson(
    '/api/webrtc/candidate',
    {
      peer_id: peerId,
      candidate: candidate.candidate || '',
      sdp_mid: candidate.sdpMid || '0',
      sdp_mline_index: candidate.sdpMLineIndex || 0,
      username_fragment: candidate.usernameFragment || '',
      sdpMid: candidate.sdpMid || '0',
      sdpMLineIndex: candidate.sdpMLineIndex || 0,
      usernameFragment: candidate.usernameFragment || '',
    },
    { ok: true },
    init,
  );
}

export async function closeWebrtcPeer(peerId: string, init?: ApiRequestOptions) {
  if (!peerId) {
    return;
  }
  try {
    await postJson(
      '/api/webrtc/close',
      { peer_id: peerId },
      { ok: true },
      init,
    );
  } catch {
    // Best-effort cleanup.
  }
}

// Streaming API: WebRTC signaling, RTSP config, WebRTC config

import { mockRtspConfig, mockWebrtcConfig } from './mockStream';
import {
  authQuery,
  postJson,
  requestJson,
  type ApiRequestOptions,
} from './client';
import type {
  RtspConfig,
  StreamName,
  WebrtcAnswerResponse,
  WebrtcCommandResponse,
  WebrtcConfig,
  WebrtcPeerResponse,
} from './types';

interface WebrtcCreatePeerRequest {
  client_id: string;
  stream: string;
}

interface WebrtcOfferRequest {
  peer_id: string;
  sdp: string;
}

// RTSP & WebRTC read-only config
export function getRtspConfig(
  init?: ApiRequestOptions,
): Promise<RtspConfig> {
  return requestJson<RtspConfig>('/api/config/rtsp', mockRtspConfig, init);
}

export function getWebrtcConfig(
  init?: ApiRequestOptions,
): Promise<WebrtcConfig> {
  return requestJson<WebrtcConfig>('/api/config/webrtc', mockWebrtcConfig, init);
}

// WebRTC signaling
export function createWebrtcPeer(stream: StreamName, init?: ApiRequestOptions) {
  return postJson<WebrtcCreatePeerRequest, WebrtcPeerResponse>(
    '/api/webrtc/peers',
    { stream, client_id: 'web' },
    {
      ok: false,
      peer_id: '',
      stream,
      state: 'failed',
    },
    init,
  );
}

export function sendWebrtcOffer(
  peerId: string,
  sdp: string,
  init?: ApiRequestOptions,
) {
  return postJson<WebrtcOfferRequest, WebrtcAnswerResponse>(
    '/api/webrtc/offer',
    { peer_id: peerId, sdp },
    {
      ok: false,
      peer_id: peerId,
      sdp: '',
      state: 'failed',
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
    { ok: true, peer_id: peerId } satisfies WebrtcCommandResponse,
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
      { ok: true, peer_id: peerId } satisfies WebrtcCommandResponse,
      init,
    );
  } catch {
    // Best-effort cleanup.
  }
}

export function hlsPlaylistUrl(stream: string, includeToken = true): string {
  const query = authQuery({ includeToken });
  return `/api/hls/${stream}/index.m3u8${query ? `?${query}` : ''}`;
}

export function flvStreamUrl(stream: string, includeToken = true): string {
  const query = authQuery({ includeToken });
  return `/api/flv/${stream}.flv${query ? `?${query}` : ''}`;
}

export function mjpegStreamUrl(stream: string, includeToken = true): string {
  const query = authQuery({ includeToken });
  return `/api/mjpeg/${stream}.mjpg${query ? `?${query}` : ''}`;
}

// Streaming API: media runtime, playback URLs, WebRTC signaling, RTSP config.

import {
  deleteJson,
  postJson,
  requestJson,
  type ApiRequestOptions,
} from './client';
import {
  mockMediaPlaybackUrls,
  mockMediaSessions,
  mockMediaStreams,
  mockRtspConfig,
  mockWebrtcConfig,
  mockWebrtcPeer,
} from './mockStream';
import type {
  MediaPlaybackUrls,
  MediaSessionsResponse,
  MediaStreamsResponse,
  MediaSessionInfo,
  MediaStreamRuntime,
  RtspConfig,
  StreamName,
  WebrtcConfig,
  WebrtcOfferAnswer,
  WebrtcPeerInfo,
} from './types';

interface WebrtcCreatePeerRequest {
  client_id: string;
  stream: string;
}

interface WebrtcOfferRequest {
  sdp: string;
}

const webrtcPeerCloseTimeoutMs = 3000;

let webrtcClientSequence = 0;
let pendingWebrtcPeerClose: Promise<void> = Promise.resolve();

function nextWebrtcClientId(stream: StreamName): string {
  webrtcClientSequence += 1;
  return `web-${stream}-${Date.now().toString(36)}-${webrtcClientSequence}`;
}

function normalizeMediaSessions(
  response: MediaSessionInfo[] | MediaSessionsResponse,
): MediaSessionInfo[] {
  if (Array.isArray(response)) {
    return response;
  }
  if (Array.isArray(response.items)) {
    return response.items;
  }
  return [];
}

function normalizeMediaStreams(
  response: MediaStreamRuntime[] | MediaStreamsResponse,
): MediaStreamRuntime[] {
  if (Array.isArray(response)) {
    return response;
  }
  if (Array.isArray(response.items)) {
    return response.items;
  }
  return [];
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

export function getMediaStreams(
  init?: ApiRequestOptions,
): Promise<MediaStreamRuntime[]> {
  return requestJson<MediaStreamRuntime[] | MediaStreamsResponse>(
    '/api/media/streams',
    { items: mockMediaStreams },
    init,
  ).then(normalizeMediaStreams);
}

export function getMediaStream(
  stream: StreamName,
  init?: ApiRequestOptions,
): Promise<MediaStreamRuntime> {
  const fallback =
    mockMediaStreams.find((item) => item.stream === stream) || mockMediaStreams[0];
  return requestJson<MediaStreamRuntime>(
    `/api/media/streams/${stream}`,
    fallback,
    init,
  );
}

export function getMediaPlaybackUrls(
  stream: StreamName,
  init?: ApiRequestOptions,
): Promise<MediaPlaybackUrls> {
  return requestJson<MediaPlaybackUrls>(
    `/api/media/streams/${stream}/urls`,
    mockMediaPlaybackUrls[stream],
    init,
  );
}

export function getMediaSessions(
  init?: ApiRequestOptions,
): Promise<MediaSessionInfo[]> {
  return requestJson<MediaSessionInfo[] | MediaSessionsResponse>(
    '/api/media/sessions',
    { items: mockMediaSessions },
    init,
  ).then(normalizeMediaSessions);
}

// WebRTC signaling
export async function createWebrtcPeer(
  stream: StreamName,
  init?: ApiRequestOptions,
) {
  // 后端默认只允许一个 WebRTC peer；快速切流时必须串行关闭旧 peer 再创建新 peer。
  await pendingWebrtcPeerClose;
  return postJson<WebrtcCreatePeerRequest, WebrtcPeerInfo>(
    '/api/webrtc/peers',
    { stream, client_id: nextWebrtcClientId(stream) },
    mockWebrtcPeer(stream),
    init,
  );
}

export function sendWebrtcOffer(
  peerId: string,
  sdp: string,
  init?: ApiRequestOptions,
) {
  return postJson<WebrtcOfferRequest, WebrtcOfferAnswer>(
    `/api/webrtc/peers/${encodeURIComponent(peerId)}/offer`,
    { sdp },
    {
      peer_id: peerId,
      sdp: '',
      state: 'offer_received',
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
    `/api/webrtc/peers/${encodeURIComponent(peerId)}/candidates`,
    {
      candidate: candidate.candidate || '',
      sdp_mid: candidate.sdpMid || '0',
      sdp_mline_index: candidate.sdpMLineIndex || 0,
      username_fragment: candidate.usernameFragment || '',
    },
    { peer_id: peerId },
    init,
  );
}

export async function closeWebrtcPeer(peerId: string, init?: ApiRequestOptions) {
  if (!peerId) {
    return;
  }
  const closeInit: ApiRequestOptions = {
    timeoutMs: webrtcPeerCloseTimeoutMs,
    ...init,
  };
  const closeRequest = pendingWebrtcPeerClose
    .catch(() => {})
    .then(async () => {
      try {
        await deleteJson(
          `/api/webrtc/peers/${encodeURIComponent(peerId)}`,
          { peer_id: peerId },
          closeInit,
        );
      } catch {
        // 关闭是切流清理动作，失败只影响本次释放，不能卡住后续预览链路。
      }
    });
  pendingWebrtcPeerClose = closeRequest.catch(() => {});
  await closeRequest;
}

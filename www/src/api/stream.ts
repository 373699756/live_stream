// Streaming API: media runtime, playback URLs, WebRTC signaling, RTSP config.

import {
  deleteJson,
  postJson,
  putJson,
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

let pendingWebrtcPeerClose: Promise<void> = Promise.resolve();
const webrtcClientIds: Partial<Record<StreamName, string>> = {};

function nextWebrtcClientId(stream: StreamName): string {
  if (!webrtcClientIds[stream]) {
    webrtcClientIds[stream] = `web-${stream}-${Date.now().toString(36)}`;
  }
  return webrtcClientIds[stream];
}

const emptyMediaSessions: MediaSessionsResponse = {
  items: [],
  http_flv_active_clients: 0,
  mjpeg_active_clients: 0,
  rtsp_active_sessions: 0,
  webrtc_active_peers: 0,
  webrtc_dtls_ready: false,
  webrtc_enabled: false,
  webrtc_ice_ready: false,
  webrtc_ice_server_count: 0,
  webrtc_local_port_base: 0,
  webrtc_max_peers: 0,
  webrtc_public_ip: '',
  webrtc_selected_ice_pairs: 0,
  webrtc_signaling_ready: false,
  webrtc_srtp_ready: false,
};

function normalizeMediaSessions(
  response: MediaSessionInfo[] | MediaSessionsResponse,
): MediaSessionsResponse {
  if (Array.isArray(response)) {
    return { ...emptyMediaSessions, items: response };
  }
  return {
    ...emptyMediaSessions,
    ...response,
    items: Array.isArray(response.items) ? response.items : [],
  };
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

export function saveWebrtcConfig(
  value: WebrtcConfig,
  init?: ApiRequestOptions,
): Promise<void> {
  return putJson('/api/config/webrtc', value, init);
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
): Promise<MediaSessionsResponse> {
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

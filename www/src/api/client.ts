import {
  mockImageConfig,
  mockMediaCapabilities,
  mockNetworkConfig,
  mockOsdConfig,
  mockRtspConfig,
  mockSnapshotConfig,
  mockStreamStatus,
  mockSystemStatus,
  mockUpgradeStatus,
  mockVideoConfig,
  mockWebrtcConfig,
} from './mock';
import type {
  ImageConfig,
  MediaCapabilities,
  NetworkConfig,
  OperationRecord,
  OsdConfig,
  RtspConfig,
  SnapshotConfig,
  StreamStatus,
  SystemStatus,
  UpgradePackageInfo,
  UpgradeRequest,
  UpgradeStatus,
  VideoConfig,
  WebrtcConfig,
} from './types';

const headers = { 'Content-Type': 'application/json' };
const tokenKey = 'live_stream_token';
const useMockFallback = import.meta.env.DEV;

async function readError(response: Response) {
  try {
    const body = (await response.json()) as { error?: string };
    if (body.error) {
      return body.error;
    }
  } catch {
    // Ignore JSON parse failures for error responses.
  }
  return `${response.status} ${response.statusText}`;
}

function authHeaders(init?: RequestInit): HeadersInit {
  const token = window.localStorage.getItem(tokenKey);
  return {
    ...headers,
    ...(token ? { Authorization: `Bearer ${token}` } : {}),
    ...(init?.headers || {}),
  };
}

async function requestJson<T>(path: string, fallback: T, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    ...init,
    headers: authHeaders(init),
  });
  if (!response.ok) {
    if (useMockFallback) {
      return fallback;
    }
    throw new Error(await readError(response));
  }
  return (await response.json()) as T;
}

async function postJson<TRequest, TResponse>(
  path: string,
  value: TRequest,
  fallback: TResponse,
): Promise<TResponse> {
  const response = await fetch(path, {
    method: 'POST',
    headers: authHeaders(),
    body: JSON.stringify(value),
  });
  if (!response.ok) {
    if (useMockFallback) {
      return fallback;
    }
    throw new Error(await readError(response));
  }
  return (await response.json()) as TResponse;
}

async function putJson<T>(path: string, value: T): Promise<boolean> {
  const response = await fetch(path, {
    method: 'PUT',
    headers: authHeaders(),
    body: JSON.stringify(value),
  });
  return response.ok;
}

export function snapshotUrl(stream: string, tick = 0) {
  const token = window.localStorage.getItem(tokenKey);
  const params = new URLSearchParams();
  if (tick > 0) {
    params.set('t', String(tick));
  }
  if (token) {
    params.set('token', token);
  }
  const query = params.toString();
  return `/api/snapshot/${stream}.jpg${query ? `?${query}` : ''}`;
}

export function operationsExportUrl() {
  const token = window.localStorage.getItem(tokenKey);
  if (!token) {
    return '/api/operations/export';
  }
  const params = new URLSearchParams();
  params.set('token', token);
  return `/api/operations/export?${params.toString()}`;
}

export async function login(userName: string, password: string): Promise<boolean> {
  try {
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      headers,
      body: JSON.stringify({ user_name: userName, password }),
    });
    if (!response.ok) {
      return false;
    }
    const body = (await response.json()) as { token?: string };
    if (!body.token) {
      return false;
    }
    window.localStorage.setItem(tokenKey, body.token);
    return true;
  } catch {
    return false;
  }
}

export function logout() {
  window.localStorage.removeItem(tokenKey);
}

export function hasToken() {
  return Boolean(window.localStorage.getItem(tokenKey));
}

export async function createWebrtcPeer(stream: string) {
  return postJson('/api/webrtc/peers', { stream, client_id: 'web' }, {
    peer_id: '',
    stream,
  });
}

export async function sendWebrtcOffer(peerId: string, sdp: string) {
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

function mockUpgradePackage(file: File): UpgradePackageInfo {
  const stem = file.name.replace(/\.[^.]+$/, '') || 'firmware';
  return {
    package_path: `/tmp/live_stream/upgrade/uploads/${file.name}`,
    version: stem,
    size_bytes: file.size,
    digest: 'mock-digest',
    build_time_ms: Date.now(),
    target_model: 'live_stream_ipc',
    requires_reboot: true,
  };
}

export async function uploadUpgradePackage(file: File): Promise<UpgradePackageInfo> {
  const response = await fetch(
    `/api/upgrade/upload?filename=${encodeURIComponent(file.name)}`,
    {
      method: 'POST',
      headers: authHeaders({
        headers: { 'Content-Type': 'application/octet-stream' },
      }),
      body: file,
    },
  );
  if (!response.ok) {
    if (useMockFallback) {
      return mockUpgradePackage(file);
    }
    throw new Error(await readError(response));
  }
  return (await response.json()) as UpgradePackageInfo;
}

export const api = {
  getVideoConfig: () => requestJson<VideoConfig>('/api/config/video', mockVideoConfig),
  saveVideoConfig: (value: VideoConfig) => putJson('/api/config/video', value),
  getMediaCapabilities: () =>
    requestJson<MediaCapabilities>('/api/media/capabilities', mockMediaCapabilities),
  getImageConfig: () => requestJson<ImageConfig>('/api/config/image', mockImageConfig),
  saveImageConfig: (value: ImageConfig) => putJson('/api/config/image', value),
  getOsdConfig: () => requestJson<OsdConfig>('/api/config/osd', mockOsdConfig),
  saveOsdConfig: (value: OsdConfig) => putJson('/api/config/osd', value),
  getNetworkConfig: () => requestJson<NetworkConfig>('/api/config/network', mockNetworkConfig),
  saveNetworkConfig: (value: NetworkConfig) => putJson('/api/config/network', value),
  getSnapshotConfig: () => requestJson<SnapshotConfig>('/api/config/snapshot', mockSnapshotConfig),
  saveSnapshotConfig: (value: SnapshotConfig) => putJson('/api/config/snapshot', value),
  getRtspConfig: () => requestJson<RtspConfig>('/api/config/rtsp', mockRtspConfig),
  getWebrtcConfig: () => requestJson<WebrtcConfig>('/api/config/webrtc', mockWebrtcConfig),
  getSystemStatus: () => requestJson<SystemStatus>('/api/system/status', mockSystemStatus),
  getUpgradeStatus: () => requestJson<UpgradeStatus>('/api/upgrade/status', mockUpgradeStatus),
  uploadUpgradePackage,
  startUpgrade: (value: UpgradeRequest) =>
    postJson<UpgradeRequest, UpgradeStatus>('/api/upgrade/start', value, {
      ...mockUpgradeStatus,
      state: 'validating',
      current_stage: 'validating',
      target_version: value.expected_version,
      started_at_ms: Date.now(),
    }),
  cancelUpgrade: () =>
    postJson<Record<string, never>, UpgradeStatus>('/api/upgrade/cancel', {}, {
      ...mockUpgradeStatus,
      state: 'canceled',
      current_stage: 'canceled',
      error_message: 'canceled',
      finished_at_ms: Date.now(),
    }),
  confirmUpgradeReboot: () =>
    postJson<Record<string, never>, UpgradeStatus>(
      '/api/upgrade/confirm-reboot',
      {},
      {
        ...mockUpgradeStatus,
        state: 'completed',
        current_stage: 'completed',
        progress_percent: 100,
        finished_at_ms: Date.now(),
      },
    ),
  getStreamStatus: () => requestJson<StreamStatus[]>('/api/status/streams', mockStreamStatus),
  getOperations: () =>
    requestJson<{ items: OperationRecord[] }>('/api/operations', { items: [] }),
};

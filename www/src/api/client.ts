import {
  mockImageConfig,
  mockNetworkConfig,
  mockOsdConfig,
  mockMediaCapabilities,
  mockSnapshotConfig,
  mockStreamStatus,
  mockSystemStatus,
  mockVideoConfig,
} from './mock';
import type {
  ImageConfig,
  MediaCapabilities,
  NetworkConfig,
  OsdConfig,
  SnapshotConfig,
  StreamStatus,
  SystemStatus,
  VideoConfig,
} from './types';

const headers = { 'Content-Type': 'application/json' };

async function requestJson<T>(path: string, fallback: T, init?: RequestInit): Promise<T> {
  try {
    const response = await fetch(path, {
      ...init,
      headers: { ...headers, ...(init?.headers || {}) },
    });
    if (!response.ok) {
      throw new Error(`${response.status} ${response.statusText}`);
    }
    return (await response.json()) as T;
  } catch {
    return fallback;
  }
}

async function putJson<T>(path: string, value: T): Promise<boolean> {
  try {
    const response = await fetch(path, {
      method: 'PUT',
      headers,
      body: JSON.stringify(value),
    });
    return response.ok;
  } catch {
    return false;
  }
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
  getSystemStatus: () => requestJson<SystemStatus>('/api/system/status', mockSystemStatus),
  getStreamStatus: () => requestJson<StreamStatus[]>('/api/status/streams', mockStreamStatus),
};

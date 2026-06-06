import type { SystemStatus } from './types';

export const mockSystemStatus: SystemStatus = {
  deviceName: 'IPC Camera',
  model: 'live_stream_ipc',
  firmware: '0.1.0',
  uptime: '3d 06:18:42',
  cpu: 34,
  memory: 51,
  temperature: 48,
  services: [
    { name: 'config_service', state: 'running' },
    { name: 'auth_service', state: 'running' },
    { name: 'media_service', state: 'pending' },
    { name: 'http_service', state: 'running' },
    { name: 'webrtc_service', state: 'pending' },
  ],
};

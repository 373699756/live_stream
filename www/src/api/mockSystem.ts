import type { SystemStatus } from './types';

export const mockSystemStatus: SystemStatus = {
  deviceName: 'IPC Camera',
  model: 'live_stream_ipc',
  firmware: '0.1.0',
  uptime: '3d 06:18:42',
  cpu: 34,
  memory: 51,
  temperature: 48,
  modules: [
    { name: 'config', state: 'running' },
    { name: 'auth', state: 'running' },
    { name: 'device', state: 'pending' },
    { name: 'http', state: 'running' },
    { name: 'webrtc', state: 'pending' },
  ],
};

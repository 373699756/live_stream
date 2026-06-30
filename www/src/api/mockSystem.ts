import type { SystemInfo } from './types/system';

export const mockSystemInfo: SystemInfo = {
    deviceName: 'Binary',
    model: 'live_stream_ipc',
    firmware: '1.0.2',
    software: '1.0.2',
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

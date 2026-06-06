import type { RtspConfig, WebrtcConfig } from './types';

export const mockRtspConfig: RtspConfig = {
  enabled: true,
  port: 554,
  auth_required: true,
  paths: {
    main: '/live/main',
    sub: '/live/sub',
  },
  max_sessions: 8,
  session_timeout_sec: 60,
};

export const mockWebrtcConfig: WebrtcConfig = {
  enabled: true,
  signaling_path: '/api/webrtc',
  ice_servers: [],
  max_peers: 4,
  prefer_tcp: false,
};

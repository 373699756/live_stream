import type { NetworkConfig } from './types';

export const mockNetworkConfig: NetworkConfig = {
  hostname: 'live-stream-ipc',
  interfaces: {
    eth0: {
      enabled: true,
      dhcp: true,
      static_ipv4: {
        address: '192.168.1.100',
        netmask: '255.255.255.0',
        gateway: '192.168.1.1',
      },
    },
  },
  ports: { http: 80, https: 443, rtsp: 554, onvif: 8000 },
};

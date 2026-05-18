/**
 * useLiveView — fetch stream statuses (with 5-second polling) and RTSP config
 * for the live-view page.
 */

import { useEffect, useState } from 'react';
import { getStreamStatus } from '../api/video';
import { getRtspConfig } from '../api/stream';
import type { RtspConfig, StreamStatus } from '../api/types';

export function useLiveView() {
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [rtspConfig, setRtspConfig] = useState<RtspConfig | null>(null);

  useEffect(() => {
    void getStreamStatus().then(setStatuses);
    void getRtspConfig().then(setRtspConfig);
    const timer = window.setInterval(() => {
      void getStreamStatus().then(setStatuses);
    }, 5000);
    return () => window.clearInterval(timer);
  }, []);

  return { statuses, rtspConfig };
}

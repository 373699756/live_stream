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
  const [error, setError] = useState('');
  const [lastUpdatedAt, setLastUpdatedAt] = useState<number | null>(null);

  useEffect(() => {
    const refreshStatuses = () => {
      void getStreamStatus()
        .then((nextStatuses) => {
          setStatuses(nextStatuses);
          setLastUpdatedAt(Date.now());
          setError('');
        })
        .catch((err: unknown) => {
          setError(err instanceof Error ? err.message : '码流状态刷新失败');
        });
    };
    refreshStatuses();
    void getRtspConfig()
      .then(setRtspConfig)
      .catch((err: unknown) => {
        setError(err instanceof Error ? err.message : 'RTSP 配置加载失败');
      });
    const timer = window.setInterval(() => {
      refreshStatuses();
    }, 5000);
    return () => window.clearInterval(timer);
  }, []);

  return { statuses, rtspConfig, error, lastUpdatedAt };
}

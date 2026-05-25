/**
 * useLiveView — fetch stream statuses and RTSP config for the live-view page.
 * It refreshes faster right after page entry so preview readiness catches up.
 */

import { useEffect, useState } from 'react';
import { getStreamStatus } from '../api/video';
import { getRtspConfig } from '../api/stream';
import type { RtspConfig, StreamStatus } from '../api/types';

const configTimeoutMs = 3000;
const statusTimeoutMs = 1800;
const fastRefreshIntervalMs = 2000;
const steadyRefreshIntervalMs = 5000;
const fastRefreshCount = 4;

export function useLiveView() {
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [rtspConfig, setRtspConfig] = useState<RtspConfig | null>(null);
  const [error, setError] = useState('');
  const [lastUpdatedAt, setLastUpdatedAt] = useState<number | null>(null);

  useEffect(() => {
    let fastRefreshes = 0;
    const refreshStatuses = () => {
      void getStreamStatus({ timeoutMs: statusTimeoutMs })
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
    void getRtspConfig({ timeoutMs: configTimeoutMs })
      .then(setRtspConfig)
      .catch((err: unknown) => {
        setError(err instanceof Error ? err.message : 'RTSP 配置加载失败');
      });
    const fastTimer = window.setInterval(() => {
      fastRefreshes += 1;
      refreshStatuses();
      if (fastRefreshes >= fastRefreshCount) {
        window.clearInterval(fastTimer);
      }
    }, fastRefreshIntervalMs);
    const steadyTimer = window.setInterval(() => {
      refreshStatuses();
    }, steadyRefreshIntervalMs);
    return () => {
      window.clearInterval(fastTimer);
      window.clearInterval(steadyTimer);
    };
  }, []);

  return { statuses, rtspConfig, error, lastUpdatedAt };
}

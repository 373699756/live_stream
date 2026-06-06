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
    let mounted = true;
    let fastRefreshes = 0;
    let statusRequestRunning = false;
    const controller = new AbortController();
    const refreshStatuses = () => {
      if (statusRequestRunning) {
        return;
      }
      statusRequestRunning = true;
      void getStreamStatus({
        signal: controller.signal,
        timeoutMs: statusTimeoutMs,
      })
        .then((nextStatuses) => {
          if (mounted) {
            setStatuses(nextStatuses);
            setLastUpdatedAt(Date.now());
            setError('');
          }
        })
        .catch((err: unknown) => {
          if (mounted) {
            setError(err instanceof Error ? err.message : '码流状态刷新失败');
          }
        })
        .finally(() => {
          statusRequestRunning = false;
        });
    };
    refreshStatuses();
    void getRtspConfig({
      signal: controller.signal,
      timeoutMs: configTimeoutMs,
    })
      .then((nextConfig) => {
        if (mounted) {
          setRtspConfig(nextConfig);
        }
      })
      .catch((err: unknown) => {
        if (mounted) {
          setError(err instanceof Error ? err.message : 'RTSP 配置加载失败');
        }
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
      mounted = false;
      controller.abort();
      window.clearInterval(fastTimer);
      window.clearInterval(steadyTimer);
    };
  }, []);

  return { statuses, rtspConfig, error, lastUpdatedAt };
}

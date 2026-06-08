/**
 * useLiveView — fetch media runtime and playback URLs for the live-view page.
 * It refreshes faster right after page entry so preview readiness catches up.
 */

import { useEffect, useState } from 'react';
import { openMediaEvents } from '../api/mediaEvents';
import { getMediaPlaybackUrls, getMediaStreams } from '../api/stream';
import type {
  MediaPlaybackUrls,
  MediaStreamRuntime,
  StreamName,
} from '../api/types';

const configTimeoutMs = 3000;
const statusTimeoutMs = 1800;
const fastRefreshIntervalMs = 2000;
const steadyRefreshIntervalMs = 12000;
const fastRefreshCount = 4;

export function useLiveView(selectedStream?: StreamName) {
  const [statuses, setStatuses] = useState<MediaStreamRuntime[]>([]);
  const [playbackUrls, setPlaybackUrls] = useState<MediaPlaybackUrls | null>(null);
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
      void getMediaStreams({
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
    setPlaybackUrls(null);
    if (selectedStream) {
      void getMediaPlaybackUrls(selectedStream, {
        signal: controller.signal,
        timeoutMs: configTimeoutMs,
      })
        .then((nextUrls) => {
          if (mounted) {
            setPlaybackUrls(nextUrls);
          }
        })
        .catch((err: unknown) => {
          if (mounted) {
            setPlaybackUrls(null);
            setError(err instanceof Error ? err.message : '播放地址加载失败');
          }
        });
    }
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
    let eventSource: EventSource | null = null;
    if (typeof EventSource !== 'undefined') {
      eventSource = openMediaEvents((event) => {
        const readyChanged =
          event.target.endsWith('.ready') && event.message === 'changed';
        const firstFrame =
          event.target.endsWith('.frame') && event.message === 'first';
        if (event.type === 'media_status_changed' && (readyChanged || firstFrame)) {
          refreshStatuses();
        }
      });
      eventSource.onerror = () => {
        refreshStatuses();
      };
    }
    return () => {
      mounted = false;
      controller.abort();
      eventSource?.close();
      window.clearInterval(fastTimer);
      window.clearInterval(steadyTimer);
    };
  }, [selectedStream]);

  return { statuses, playbackUrls, error, lastUpdatedAt };
}

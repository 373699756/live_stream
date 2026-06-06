import { useCallback, useEffect, useState } from 'react';
import { mockMediaCapabilities } from '../api/mockVideo';
import { getMediaCapabilities, getStreamStatus } from '../api/video';
import type { MediaCapabilities, StreamStatus } from '../api/types';

const statusTimeoutMs = 1800;
const defaultRefreshIntervalMs = 3000;

export function usePreviewMetadata(refreshIntervalMs = defaultRefreshIntervalMs) {
  const [capabilities, setCapabilities] =
    useState<MediaCapabilities>(mockMediaCapabilities);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);

  const refreshStatuses = useCallback(() =>
    getStreamStatus({ timeoutMs: statusTimeoutMs })
      .then((nextStatuses) => {
        setStatuses(nextStatuses);
      })
      .catch(() => {
        setStatuses([]);
      }), []);

  useEffect(() => {
    let mounted = true;
    const loadStatuses = () => {
      void getStreamStatus({ timeoutMs: statusTimeoutMs })
        .then((nextStatuses) => {
          if (mounted) {
            setStatuses(nextStatuses);
          }
        })
        .catch(() => {
          if (mounted) {
            setStatuses([]);
          }
        });
    };
    void getMediaCapabilities({ timeoutMs: statusTimeoutMs })
      .then((nextCapabilities) => {
        if (mounted) {
          setCapabilities(nextCapabilities);
        }
      })
      .catch(() => {
        if (mounted) {
          setCapabilities(mockMediaCapabilities);
        }
      });
    loadStatuses();
    const timer = refreshIntervalMs > 0
      ? window.setInterval(loadStatuses, refreshIntervalMs)
      : 0;
    return () => {
      mounted = false;
      if (timer !== 0) {
        window.clearInterval(timer);
      }
    };
  }, [refreshIntervalMs]);

  return { capabilities, statuses, refreshStatuses };
}

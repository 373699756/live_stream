import { useCallback, useEffect, useState } from 'react';
import { mockMediaCapabilities } from '../api/mockVideo';
import { getMediaStreams, getMediaPlaybackUrls } from '../api/stream';
import { getMediaCapabilities } from '../api/video';
import type {
  MediaCapabilities,
  MediaPlaybackUrls,
  MediaStreamRuntime,
  StreamName,
} from '../api/types';

const statusTimeoutMs = 1800;
const defaultRefreshIntervalMs = 3000;

export function usePreviewMetadata(
  selectedStream?: StreamName,
  refreshIntervalMs = defaultRefreshIntervalMs,
) {
  const [capabilities, setCapabilities] =
    useState<MediaCapabilities>(mockMediaCapabilities);
  const [statuses, setStatuses] = useState<MediaStreamRuntime[]>([]);
  const [playbackUrls, setPlaybackUrls] = useState<MediaPlaybackUrls | null>(null);

  const refreshStatuses = useCallback(() =>
    getMediaStreams({ timeoutMs: statusTimeoutMs })
      .then((nextStatuses) => {
        setStatuses(nextStatuses);
      })
      .catch(() => {
        setStatuses([]);
      }), []);

  useEffect(() => {
    let mounted = true;
    const loadStatuses = () => {
      void getMediaStreams({ timeoutMs: statusTimeoutMs })
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

  useEffect(() => {
    if (!selectedStream) {
      setPlaybackUrls(null);
      return;
    }
    let mounted = true;
    setPlaybackUrls(null);
    void getMediaPlaybackUrls(selectedStream, { timeoutMs: statusTimeoutMs })
      .then((nextUrls) => {
        if (mounted) {
          setPlaybackUrls(nextUrls);
        }
      })
      .catch(() => {
        if (mounted) {
          setPlaybackUrls(null);
        }
      });
    return () => {
      mounted = false;
    };
  }, [selectedStream]);

  return { capabilities, statuses, playbackUrls, refreshStatuses };
}

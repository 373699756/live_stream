/**
 * useVideoConfig — fetch the editable video config first, then refresh media
 * capabilities and stream status as non-blocking preview metadata.
 */

import { useEffect, useState } from 'react';
import { getVideoConfig, getMediaCapabilities, getStreamStatus } from '../api/video';
import type { VideoConfig, MediaCapabilities, StreamStatus } from '../api/types';
import { mockMediaCapabilities } from '../api/mock';

const configTimeoutMs = 5000;
const statusTimeoutMs = 1800;

export function useVideoConfig() {
  const [config, setConfig] = useState<VideoConfig | null>(null);
  const [capabilities, setCapabilities] = useState<MediaCapabilities>(mockMediaCapabilities);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  useEffect(() => {
    let mounted = true;
    setLoading(true);
    void getVideoConfig({ timeoutMs: configTimeoutMs })
      .then((nextConfig) => {
        if (!mounted) return;
        if (nextConfig !== null) {
          setConfig(nextConfig);
        }
        setError('');
      })
      .catch((err: unknown) => {
        if (mounted) {
          setError(err instanceof Error ? err.message : '加载视频配置失败');
        }
      })
      .finally(() => {
        if (mounted) {
          setLoading(false);
        }
      });
    return () => {
      mounted = false;
    };
  }, []);

  useEffect(() => {
    let mounted = true;
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
    return () => {
      mounted = false;
    };
  }, []);

  return { config, setConfig, capabilities, statuses, loading, error };
}

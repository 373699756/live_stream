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
const statusRefreshIntervalMs = 3000;

export function useVideoConfig() {
  const [config, setConfig] = useState<VideoConfig | null>(null);
  const [capabilities, setCapabilities] = useState<MediaCapabilities>(mockMediaCapabilities);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const reloadConfig = () =>
    getVideoConfig({ timeoutMs: configTimeoutMs })
      .then((nextConfig) => {
        setConfig(nextConfig);
        setError('');
        return nextConfig;
      })
      .catch((err: unknown) => {
        setError(err instanceof Error ? err.message : '加载视频配置失败');
        throw err;
      });

  const refreshStatuses = () =>
    getStreamStatus({ timeoutMs: statusTimeoutMs })
      .then((nextStatuses) => {
        setStatuses(nextStatuses);
      })
      .catch(() => {
        setStatuses([]);
      });

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
    const timer = window.setInterval(() => {
      loadStatuses();
    }, statusRefreshIntervalMs);
    return () => {
      mounted = false;
      window.clearInterval(timer);
    };
  }, []);

  return {
    config,
    setConfig,
    capabilities,
    statuses,
    reloadConfig,
    refreshStatuses,
    loading,
    error,
  };
}

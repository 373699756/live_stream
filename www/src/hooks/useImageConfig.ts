/**
 * useImageConfig — fetch image config first; preview capabilities and stream
 * status are loaded independently so the form stays responsive.
 */

import { useEffect, useState } from 'react';
import {
  getImageConfig,
  getImageStrategyStatus,
  saveImageConfig,
} from '../api/image';
import { getMediaCapabilities, getStreamStatus } from '../api/video';
import type {
  ImageConfig,
  ImageStrategyStatus,
  MediaCapabilities,
  StreamStatus,
} from '../api/types';
import {
  cloneDefaultConfig,
  mockMediaCapabilities,
  mockImageConfig,
  mockImageStrategyStatus,
} from '../api/mock';

const configTimeoutMs = 5000;
const statusTimeoutMs = 1800;

export function useImageConfig() {
  const [config, setConfig] = useState<ImageConfig | null>(null);
  const [capabilities, setCapabilities] = useState<MediaCapabilities>(mockMediaCapabilities);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [strategyStatus, setStrategyStatus] =
    useState<ImageStrategyStatus>(mockImageStrategyStatus);
  const [savedMsg, setSavedMsg] = useState('');
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    let mounted = true;
    setLoading(true);
    void getImageConfig({ timeoutMs: configTimeoutMs })
      .then((nextConfig) => {
        if (!mounted) return;
        if (nextConfig !== null) {
          setConfig(nextConfig);
        }
        setError('');
      })
      .catch((err: unknown) => {
        if (mounted) {
          setError(err instanceof Error ? err.message : '加载图像配置失败');
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
    const refreshStrategy = () => {
      void getImageStrategyStatus({ timeoutMs: statusTimeoutMs })
        .then((nextStatus) => {
          if (mounted) {
            setStrategyStatus(nextStatus);
          }
        })
        .catch(() => {
          if (mounted) {
            setStrategyStatus(mockImageStrategyStatus);
          }
        });
    };
    refreshStrategy();
    const timer = window.setInterval(refreshStrategy, 2000);
    return () => {
      mounted = false;
      window.clearInterval(timer);
    };
  }, []);

  const save = async (nextConfig = config) => {
    if (nextConfig === null) return;
    setSaving(true);
    setError('');
    try {
      await saveImageConfig(nextConfig);
      setConfig(nextConfig);
      setSavedMsg('已提交保存');
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : '保存失败';
      setSavedMsg(`保存失败：${message}`);
      setError(message);
    } finally {
      setSaving(false);
    }
  };

  const reset = () => {
    setConfig(cloneDefaultConfig(mockImageConfig));
    setSavedMsg('已恢复默认值，保存后生效');
  };

  return {
    config,
    setConfig,
    capabilities,
    statuses,
    strategyStatus,
    save,
    reset,
    savedMsg,
    loading,
    saving,
    error,
  };
}

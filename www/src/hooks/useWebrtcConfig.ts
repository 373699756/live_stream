import { useEffect, useState } from 'react';
import {
  getWebrtcConfig,
  saveWebrtcConfig,
} from '../api/stream';
import type { WebrtcConfig } from '../api/types';

const configTimeoutMs = 3000;

function normalizeWebrtcConfig(config: WebrtcConfig): WebrtcConfig {
  return {
    ...config,
    enabled: Boolean(config.enabled),
    ice_servers: Array.isArray(config.ice_servers)
      ? config.ice_servers.map((server) => ({
          url: server.url || '',
          username: server.username || '',
          credential: server.credential || '',
        }))
      : [],
    local_port_base: Number(config.local_port_base) || 16000,
    max_peers: Math.max(Number(config.max_peers) || 1, 1),
    prefer_tcp: Boolean(config.prefer_tcp),
    public_ip: config.public_ip || 'auto',
  };
}

export function useWebrtcConfig() {
  const [config, setConfig] = useState<WebrtcConfig | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');

  const reload = () =>
    getWebrtcConfig({ timeoutMs: configTimeoutMs })
      .then((nextConfig) => {
        const normalized = normalizeWebrtcConfig(nextConfig);
        setConfig(normalized);
        setError('');
        return normalized;
      })
      .catch((err: unknown) => {
        const text = err instanceof Error ? err.message : '加载 WebRTC 配置失败';
        setError(text);
        throw err;
      });

  useEffect(() => {
    let mounted = true;
    setLoading(true);
    void getWebrtcConfig({ timeoutMs: configTimeoutMs })
      .then((nextConfig) => {
        if (!mounted) return;
        setConfig(normalizeWebrtcConfig(nextConfig));
        setError('');
      })
      .catch((err: unknown) => {
        if (mounted) {
          setError(err instanceof Error ? err.message : '加载 WebRTC 配置失败');
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

  const save = async (nextConfig?: WebrtcConfig) => {
    const configToSave = nextConfig ?? config;
    if (!configToSave) return;
    setSaving(true);
    setError('');
    try {
      await saveWebrtcConfig(configToSave, { timeoutMs: configTimeoutMs });
      const normalized = normalizeWebrtcConfig(configToSave);
      setConfig(normalized);
      setMessage('WebRTC 配置已保存');
    } catch (err: unknown) {
      const text = err instanceof Error ? err.message : '保存 WebRTC 配置失败';
      setError(text);
      setMessage(`保存失败：${text}`);
    } finally {
      setSaving(false);
    }
  };

  return {
    config,
    error,
    loading,
    message,
    reload,
    save,
    saving,
    setConfig,
  };
}

import { useEffect, useState } from 'react';
import { getWebrtcConfig } from '../api/stream';
import type { WebrtcConfig } from '../api/types';

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

export function usePreviewWebrtcConfig() {
  const [config, setConfig] = useState<WebrtcConfig | null>(null);
  const [loaded, setLoaded] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    let mounted = true;
    const controller = new AbortController();
    void getWebrtcConfig({ signal: controller.signal })
      .then((nextConfig) => {
        if (mounted) {
          setConfig(nextConfig);
          setLoaded(true);
          setError('');
        }
      })
      .catch((err: unknown) => {
        if (isAbortError(err)) {
          return;
        }
        if (mounted) {
          setConfig(null);
          setLoaded(true);
          setError('WebRTC 配置不可用');
        }
      });
    return () => {
      mounted = false;
      controller.abort();
    };
  }, []);

  return { config, loaded, error };
}

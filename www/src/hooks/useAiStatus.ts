import { useEffect, useState } from 'react';
import { getAiStatus } from '../api/ai';
import type { AiStatus } from '../api/types';

interface AiStatusState {
  status: AiStatus | null;
  error: string;
}

export function useAiStatus(pollIntervalMs = 1000): AiStatusState {
  const [status, setStatus] = useState<AiStatus | null>(null);
  const [error, setError] = useState('');

  useEffect(() => {
    let mounted = true;
    let timer = 0;
    const controller = new AbortController();

    const refresh = () => {
      void getAiStatus({ signal: controller.signal, timeoutMs: 3000 })
        .then((nextStatus) => {
          if (mounted) {
            setStatus(nextStatus);
            setError('');
          }
        })
        .catch((err: unknown) => {
          if (!mounted) {
            return;
          }
          if (err instanceof DOMException && err.name === 'AbortError') {
            return;
          }
          setError(err instanceof Error ? err.message : 'AI 状态刷新失败');
        })
        .finally(() => {
          if (mounted) {
            timer = window.setTimeout(refresh, pollIntervalMs);
          }
        });
    };

    refresh();
    return () => {
      mounted = false;
      controller.abort();
      window.clearTimeout(timer);
    };
  }, [pollIntervalMs]);

  return { status, error };
}

import { useCallback, useEffect, useState } from 'react';
import { getAiAlerts, getAiStatus } from '../api/ai';
import type { AiAlertRecord, AiStatus } from '../api/types';

interface AiAlertsState {
  status: AiStatus | null;
  alerts: AiAlertRecord[];
  loading: boolean;
  error: string;
  refresh: () => Promise<void>;
}

export function useAiAlerts(): AiAlertsState {
  const [status, setStatus] = useState<AiStatus | null>(null);
  const [alerts, setAlerts] = useState<AiAlertRecord[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const refresh = useCallback(async () => {
    setLoading(true);
    setError('');
    try {
      const [nextStatus, nextAlerts] = await Promise.all([
        getAiStatus(),
        getAiAlerts(),
      ]);
      setStatus(nextStatus);
      setAlerts(nextAlerts.items);
    } catch (err) {
      setError(err instanceof Error ? err.message : '加载 AI 告警失败');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  return { status, alerts, loading, error, refresh };
}

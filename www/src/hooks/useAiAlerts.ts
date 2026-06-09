import { useCallback, useEffect, useState } from 'react';
import { getAlarmConfig, getAlarmStatus } from '../api/alarm';
import { getAiAlerts, getAiStatus } from '../api/ai';
import { openMediaEvents, type MediaEvent } from '../api/mediaEvents';
import type {
  AiAlertRecord,
  AiStatus,
  AlarmConfig,
  AlarmStatusResponse,
} from '../api/types';

interface AiAlertsState {
  status: AiStatus | null;
  alarmConfig: AlarmConfig | null;
  alarmStatus: AlarmStatusResponse | null;
  lastAlarmEvent: MediaEvent | null;
  alerts: AiAlertRecord[];
  loading: boolean;
  error: string;
  refresh: () => Promise<void>;
}

export function useAiAlerts(): AiAlertsState {
  const [status, setStatus] = useState<AiStatus | null>(null);
  const [alarmConfig, setAlarmConfig] = useState<AlarmConfig | null>(null);
  const [alarmStatus, setAlarmStatus] = useState<AlarmStatusResponse | null>(
    null,
  );
  const [lastAlarmEvent, setLastAlarmEvent] = useState<MediaEvent | null>(null);
  const [alerts, setAlerts] = useState<AiAlertRecord[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const refresh = useCallback(async () => {
    setLoading(true);
    setError('');
    try {
      const [
        nextStatus,
        nextAlerts,
        nextAlarmConfig,
        nextAlarmStatus,
      ] = await Promise.all([
        getAiStatus(),
        getAiAlerts(),
        getAlarmConfig(),
        getAlarmStatus(),
      ]);
      setStatus(nextStatus);
      setAlerts(nextAlerts.items);
      setAlarmConfig(nextAlarmConfig);
      setAlarmStatus(nextAlarmStatus);
    } catch (err) {
      setError(err instanceof Error ? err.message : '加载 AI 告警失败');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    if (typeof EventSource === 'undefined') {
      return undefined;
    }
    let refreshTimer: number | undefined;
    const eventSource = openMediaEvents((event) => {
      if (event.type !== 'alarm_triggered' || event.target !== 'ai_detection') {
        return;
      }
      setLastAlarmEvent(event);
      void getAlarmStatus()
        .then(setAlarmStatus)
        .catch(() => {
          // The periodic/manual refresh path will surface persistent failures.
        });
      if (refreshTimer !== undefined) {
        window.clearTimeout(refreshTimer);
      }
      refreshTimer = window.setTimeout(() => {
        void getAiAlerts()
          .then((nextAlerts) => setAlerts(nextAlerts.items))
          .catch(() => {
            // Alarm status is the primary event signal; manual refresh covers
            // transient image-list failures after the snapshot is written.
          });
      }, 600);
    });
    return () => {
      if (refreshTimer !== undefined) {
        window.clearTimeout(refreshTimer);
      }
      eventSource.close();
    };
  }, []);

  return {
    status,
    alarmConfig,
    alarmStatus,
    lastAlarmEvent,
    alerts,
    loading,
    error,
    refresh,
  };
}

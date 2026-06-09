import { useCallback, useEffect, useRef, useState } from 'react';
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
  const refreshAbortRef = useRef<AbortController | null>(null);
  const refreshRequestIdRef = useRef(0);

  const abortRefresh = useCallback(() => {
    refreshAbortRef.current?.abort();
    refreshAbortRef.current = null;
    refreshRequestIdRef.current += 1;
  }, []);

  const refresh = useCallback(async () => {
    abortRefresh();
    const requestId = refreshRequestIdRef.current;
    const controller = new AbortController();
    refreshAbortRef.current = controller;
    setLoading(true);
    setError('');
    try {
      const [
        nextStatus,
        nextAlerts,
        nextAlarmConfig,
        nextAlarmStatus,
      ] = await Promise.all([
        getAiStatus({ signal: controller.signal }),
        getAiAlerts({ signal: controller.signal }),
        getAlarmConfig({ signal: controller.signal }),
        getAlarmStatus({ signal: controller.signal }),
      ]);
      if (
        controller.signal.aborted ||
        refreshRequestIdRef.current !== requestId
      ) {
        return;
      }
      setStatus(nextStatus);
      setAlerts(nextAlerts.items);
      setAlarmConfig(nextAlarmConfig);
      setAlarmStatus(nextAlarmStatus);
    } catch (err) {
      if (
        controller.signal.aborted ||
        refreshRequestIdRef.current !== requestId
      ) {
        return;
      }
      setError(err instanceof Error ? err.message : '加载 AI 告警失败');
    } finally {
      if (refreshRequestIdRef.current === requestId) {
        refreshAbortRef.current = null;
        setLoading(false);
      }
    }
  }, [abortRefresh]);

  useEffect(() => {
    void refresh();
    return abortRefresh;
  }, [abortRefresh, refresh]);

  useEffect(() => {
    if (typeof EventSource === 'undefined') {
      return undefined;
    }
    let refreshTimer: number | undefined;
    let alarmStatusController: AbortController | null = null;
    let alertListController: AbortController | null = null;
    const eventSource = openMediaEvents((event) => {
      if (event.type !== 'alarm_triggered' || event.target !== 'ai_detection') {
        return;
      }
      setLastAlarmEvent(event);
      alarmStatusController?.abort();
      alarmStatusController = new AbortController();
      void getAlarmStatus({ signal: alarmStatusController.signal })
        .then(setAlarmStatus)
        .catch(() => {
          // The periodic/manual refresh path will surface persistent failures.
        });
      if (refreshTimer !== undefined) {
        window.clearTimeout(refreshTimer);
      }
      refreshTimer = window.setTimeout(() => {
        alertListController?.abort();
        alertListController = new AbortController();
        void getAiAlerts({ signal: alertListController.signal })
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
      alarmStatusController?.abort();
      alertListController?.abort();
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

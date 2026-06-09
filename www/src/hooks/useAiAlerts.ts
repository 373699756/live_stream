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

const kRealtimePollMs = 3000;
const kMaxStoredAlerts = 80;
const kAlertRetryDelaysMs = [300, 1000, 2500];

function normalizeAlerts(alerts: AiAlertRecord[]): AiAlertRecord[] {
  const alertsById = new Map<string, AiAlertRecord>();
  alerts.forEach((alert) => {
    if (!alertsById.has(alert.id)) {
      alertsById.set(alert.id, alert);
    }
  });
  return Array.from(alertsById.values())
    .sort((left, right) =>
      right.timestamp_ms === left.timestamp_ms
        ? right.id.localeCompare(left.id)
        : right.timestamp_ms - left.timestamp_ms,
    )
    .slice(0, kMaxStoredAlerts);
}

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

  const applyAlerts = useCallback(
    (nextAlerts: AiAlertRecord[], mergeCurrent: boolean) => {
      setAlerts((currentAlerts) =>
        normalizeAlerts(
          mergeCurrent ? [...nextAlerts, ...currentAlerts] : nextAlerts,
        ),
      );
    },
    [],
  );

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
      applyAlerts(nextAlerts.items, false);
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
  }, [abortRefresh, applyAlerts]);

  useEffect(() => {
    void refresh();
    return abortRefresh;
  }, [abortRefresh, refresh]);

  useEffect(() => {
    let alertRetryTimers: number[] = [];
    let pollTimer: number | undefined;
    let aiStatusController: AbortController | null = null;
    let alarmStatusController: AbortController | null = null;
    let alertListController: AbortController | null = null;
    const clearAlertRetryTimers = () => {
      alertRetryTimers.forEach((timer) => window.clearTimeout(timer));
      alertRetryTimers = [];
    };
    const refreshAiStatus = () => {
      aiStatusController?.abort();
      aiStatusController = new AbortController();
      void getAiStatus({ signal: aiStatusController.signal })
        .then(setStatus)
        .catch(() => {
          // The manual refresh path reports persistent status failures.
        });
    };
    const refreshAlarmStatus = () => {
      alarmStatusController?.abort();
      alarmStatusController = new AbortController();
      void getAlarmStatus({ signal: alarmStatusController.signal })
        .then(setAlarmStatus)
        .catch(() => {
          // The manual refresh path reports persistent alarm failures.
        });
    };
    const refreshAlertList = () => {
      alertListController?.abort();
      alertListController = new AbortController();
      void getAiAlerts({ signal: alertListController.signal })
        .then((nextAlerts) => applyAlerts(nextAlerts.items, true))
        .catch(() => {
          // The next SSE retry or visible-page poll will try again.
        });
    };
    const refreshVisibleData = () => {
      if (document.visibilityState !== 'visible') {
        return;
      }
      refreshAiStatus();
      refreshAlarmStatus();
      refreshAlertList();
    };
    const scheduleAlertRetries = () => {
      clearAlertRetryTimers();
      alertRetryTimers = kAlertRetryDelaysMs.map((delayMs) =>
        window.setTimeout(refreshAlertList, delayMs),
      );
    };
    const handleAlarmEvent = (event: MediaEvent) => {
      if (event.type !== 'alarm_triggered' || event.target !== 'ai_detection') {
        return;
      }
      setLastAlarmEvent(event);
      refreshAlarmStatus();
      scheduleAlertRetries();
    };
    const eventSource =
      typeof EventSource === 'undefined' ? null : openMediaEvents(handleAlarmEvent);
    const handleVisibilityChange = () => {
      if (document.visibilityState === 'visible') {
        refreshVisibleData();
      }
    };
    document.addEventListener('visibilitychange', handleVisibilityChange);
    pollTimer = window.setInterval(refreshVisibleData, kRealtimePollMs);
    return () => {
      clearAlertRetryTimers();
      if (pollTimer !== undefined) {
        window.clearInterval(pollTimer);
      }
      document.removeEventListener('visibilitychange', handleVisibilityChange);
      aiStatusController?.abort();
      alarmStatusController?.abort();
      alertListController?.abort();
      eventSource?.close();
    };
  }, [applyAlerts]);

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

import { useCallback, useEffect, useState } from 'react';
import {
  getTimeStatus,
  saveTimeConfig,
  syncBrowserTime,
  syncNtpNow,
} from '../api/time';
import type { NtpConfig, TimeStatus } from '../api/types';

const statusTimeoutMs = 1800;

function errorMessage(error: unknown, fallback: string) {
  return error instanceof Error && error.message ? error.message : fallback;
}

function normalizeServersText(servers: string[]) {
  return servers.join('\n');
}

function parseServers(text: string) {
  return text
    .split(/\r?\n/)
    .map((server) => server.trim())
    .filter((server) => server.length > 0);
}

export function useTimeConfig() {
  const [status, setStatus] = useState<TimeStatus | null>(null);
  const [timezone, setTimezone] = useState('UTC');
  const [ntpEnabled, setNtpEnabled] = useState(false);
  const [ntpServersText, setNtpServersText] = useState('');
  const [ntpIntervalSec, setNtpIntervalSec] = useState(3600);
  const [manualSyncAllowed, setManualSyncAllowed] = useState(true);
  const [browserSyncOnLogin, setBrowserSyncOnLogin] = useState(true);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');

  const applyStatus = useCallback((nextStatus: TimeStatus) => {
    setStatus(nextStatus);
    setTimezone(nextStatus.timezone);
    setNtpEnabled(nextStatus.ntp.enabled);
    setNtpServersText(normalizeServersText(nextStatus.ntp.servers));
    setNtpIntervalSec(nextStatus.ntp.sync_interval_sec);
    setManualSyncAllowed(nextStatus.manual_sync_allowed);
    setBrowserSyncOnLogin(nextStatus.browser_sync_on_login);
  }, []);

  const refresh = useCallback(async () => {
    const nextStatus = await getTimeStatus({ timeoutMs: statusTimeoutMs });
    applyStatus(nextStatus);
    setError('');
  }, [applyStatus]);

  useEffect(() => {
    let mounted = true;
    setLoading(true);
    void getTimeStatus({ timeoutMs: statusTimeoutMs })
      .then((nextStatus) => {
        if (!mounted) {
          return;
        }
        applyStatus(nextStatus);
        setError('');
      })
      .catch((nextError: unknown) => {
        if (mounted) {
          setError(errorMessage(nextError, '时间状态加载失败'));
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
  }, [applyStatus]);

  const saveConfig = useCallback(async () => {
    const ntp: NtpConfig = {
      enabled: ntpEnabled,
      servers: parseServers(ntpServersText),
      sync_interval_sec: Math.max(1, Math.trunc(ntpIntervalSec)),
    };
    const nextManualSyncAllowed = manualSyncAllowed;
    const nextBrowserSyncOnLogin =
      nextManualSyncAllowed && browserSyncOnLogin;
    setBrowserSyncOnLogin(nextBrowserSyncOnLogin);
    setBusy(true);
    setError('');
    setMessage('');
    try {
      await saveTimeConfig({
        timezone: timezone.trim() || 'UTC',
        ntp,
        manual_sync_allowed: nextManualSyncAllowed,
        browser_sync_on_login: nextBrowserSyncOnLogin,
      });
      await refresh();
      setMessage('时间配置已保存');
    } catch (nextError: unknown) {
      setError(errorMessage(nextError, '保存时间配置失败'));
    } finally {
      setBusy(false);
    }
  }, [
    browserSyncOnLogin,
    manualSyncAllowed,
    ntpEnabled,
    ntpIntervalSec,
    ntpServersText,
    refresh,
    timezone,
  ]);

  const syncBrowserNow = useCallback(async () => {
    setBusy(true);
    setError('');
    setMessage('');
    try {
      await syncBrowserTime();
      await refresh();
      setMessage('已用浏览器时间同步设备');
    } catch (nextError: unknown) {
      setError(errorMessage(nextError, '浏览器时间同步失败'));
    } finally {
      setBusy(false);
    }
  }, [refresh]);

  const syncNtp = useCallback(async () => {
    setBusy(true);
    setError('');
    setMessage('');
    try {
      await syncNtpNow({ timeoutMs: 8000 });
      await refresh();
      setMessage('已触发 NTP 同步');
    } catch (nextError: unknown) {
      setError(errorMessage(nextError, 'NTP 同步失败'));
    } finally {
      setBusy(false);
    }
  }, [refresh]);

  return {
    status,
    timezone,
    setTimezone,
    ntpEnabled,
    setNtpEnabled,
    ntpServersText,
    setNtpServersText,
    ntpIntervalSec,
    setNtpIntervalSec,
    manualSyncAllowed,
    setManualSyncAllowed,
    browserSyncOnLogin,
    setBrowserSyncOnLogin,
    loading,
    busy,
    message,
    error,
    saveConfig,
    syncBrowserNow,
    syncNtp,
    refresh,
  };
}

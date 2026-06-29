import { useCallback, useEffect, useState } from 'react';
import {
    getTimeInfo,
    saveTimeConfig,
    syncBrowserTime,
    syncNtpNow,
} from '../api/time';
import type { NtpConfig, TimeInfo } from '../api/types';

const TIME_INFO_REQUEST_TIMEOUT_MS = 1800;
const NTP_SYNC_REQUEST_TIMEOUT_MS = 8000;

function errorMsg(error: unknown, fallback: string) {
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
    const [timeInfo, setTimeInfo] = useState<TimeInfo | null>(null);
    const [timezone, setTimezone] = useState('UTC');
    const [ntpEnabled, setNtpEnabled] = useState(false);
    const [ntpServersText, setNtpServersText] = useState('');
    const [ntpIntervalSec, setNtpIntervalSec] = useState(3600);
    const [manualSyncAllowed, setManualSyncAllowed] = useState(true);
    const [browserSyncOnLogin, setBrowserSyncOnLogin] = useState(true);
    const [loading, setLoading] = useState(true);
    const [busy, setBusy] = useState(false);
    const [msg, setMsg] = useState('');
    const [error, setError] = useState('');

    const applyTimeInfo = useCallback((nextTimeInfo: TimeInfo) => {
        setTimeInfo(nextTimeInfo);
        setTimezone(nextTimeInfo.timezone);
        setNtpEnabled(nextTimeInfo.ntp.enabled);
        setNtpServersText(normalizeServersText(nextTimeInfo.ntp.servers));
        setNtpIntervalSec(nextTimeInfo.ntp.sync_interval_sec);
        setManualSyncAllowed(nextTimeInfo.manual_sync_allowed);
        setBrowserSyncOnLogin(nextTimeInfo.browser_sync_on_login);
    }, []);

    const refresh = useCallback(async () => {
        const nextTimeInfo = await getTimeInfo({
            timeoutMs: TIME_INFO_REQUEST_TIMEOUT_MS,
        });
        applyTimeInfo(nextTimeInfo);
        setError('');
    }, [applyTimeInfo]);

    useEffect(() => {
        let mounted = true;
        setLoading(true);
        void getTimeInfo({ timeoutMs: TIME_INFO_REQUEST_TIMEOUT_MS })
            .then((nextTimeInfo) => {
                if (!mounted) {
                    return;
                }
                applyTimeInfo(nextTimeInfo);
                setError('');
            })
            .catch((nextError: unknown) => {
                if (mounted) {
                    setError(errorMsg(nextError, '时间状态加载失败'));
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
    }, [applyTimeInfo]);

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
        setMsg('');
        try {
            await saveTimeConfig({
                timezone: timezone.trim() || 'UTC',
                ntp,
                manual_sync_allowed: nextManualSyncAllowed,
                browser_sync_on_login: nextBrowserSyncOnLogin,
            });
            await refresh();
            setMsg('时间配置已保存');
        } catch (nextError: unknown) {
            setError(errorMsg(nextError, '保存时间配置失败'));
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
        setMsg('');
        try {
            await syncBrowserTime();
            await refresh();
            setMsg('已用浏览器时间同步设备');
        } catch (nextError: unknown) {
            setError(errorMsg(nextError, '浏览器时间同步失败'));
        } finally {
            setBusy(false);
        }
    }, [refresh]);

    const syncNtp = useCallback(async () => {
        setBusy(true);
        setError('');
        setMsg('');
        try {
            await syncNtpNow({ timeoutMs: NTP_SYNC_REQUEST_TIMEOUT_MS });
            await refresh();
            setMsg('已触发 NTP 同步');
        } catch (nextError: unknown) {
            setError(errorMsg(nextError, 'NTP 同步失败'));
        } finally {
            setBusy(false);
        }
    }, [refresh]);

    return {
        timeInfo,
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
        msg,
        error,
        saveConfig,
        syncBrowserNow,
        syncNtp,
        refresh,
    };
}

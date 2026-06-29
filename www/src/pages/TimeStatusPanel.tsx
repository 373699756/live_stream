import { useEffect, useState } from 'react';
import type { TimeInfo, TimeSyncSource } from '../api/types';

interface TimeStatusPanelProps {
    busy: boolean;
    error: string;
    manualSyncAllowed: boolean;
    msg: string;
    saveConfig: () => void;
    syncBrowserNow: () => void;
    timeInfo: TimeInfo;
}

const syncSourceLabels: Record<TimeSyncSource, string> = {
    manual: '手动校时',
    onvif: 'ONVIF',
    ntp: 'NTP',
    browser: '浏览器',
    unknown: '未知',
};

function formatTime(timestampMs: number) {
    if (timestampMs <= 0) {
        return '--';
    }
    return new Date(timestampMs).toLocaleString();
}

function sourceLabel(source: TimeSyncSource) {
    return syncSourceLabels[source] || source;
}

export function TimeStatusPanel({
    busy,
    error,
    manualSyncAllowed,
    msg,
    saveConfig,
    syncBrowserNow,
    timeInfo,
}: TimeStatusPanelProps) {
    const [browserNow, setBrowserNow] = useState(Date.now());

    useEffect(() => {
        const timer = window.setInterval(() => setBrowserNow(Date.now()), 1000);
        return () => window.clearInterval(timer);
    }, []);

    return (
        <section className="panel wide-panel">
            <div className="page-heading">
                <div>
                    <h2>时间同步</h2>
                    <p>设备系统时间、NTP 和浏览器登录校时配置</p>
                </div>
                <div className="time-action-row">
                    <button
                        type="button"
                        onClick={syncBrowserNow}
                        disabled={busy || !manualSyncAllowed}
                        title={
                            manualSyncAllowed
                                ? '使用当前浏览器时间同步设备'
                                : '手动/浏览器校时已关闭'
                        }
                    >
                        浏览器时间同步
                    </button>
                    <button
                        type="button"
                        className="primary"
                        onClick={saveConfig}
                        disabled={busy}
                    >
                        保存配置
                    </button>
                </div>
            </div>

            {error ? (
                <div className="status-note error-note">{error}</div>
            ) : null}
            {msg ? (
                <div className="status-note success-note">{msg}</div>
            ) : null}

            <div className="time-status-row">
                <div>
                    <span>设备时间</span>
                    <strong>{formatTime(timeInfo.system_time_ms)}</strong>
                </div>
                <div>
                    <span>浏览器时间</span>
                    <strong>{formatTime(browserNow)}</strong>
                </div>
                <div>
                    <span>最近同步</span>
                    <strong>
                        {sourceLabel(timeInfo.last_sync_source)} /{' '}
                        {timeInfo.last_sync_ok ? '成功' : '失败'}
                    </strong>
                </div>
                <div>
                    <span>同步时间</span>
                    <strong>{formatTime(timeInfo.last_sync_time_ms)}</strong>
                </div>
            </div>
        </section>
    );
}

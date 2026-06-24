import { useEffect, useMemo, useState } from 'react';
import type { TimeInfo, TimeSyncSource } from '../api/types';

interface TimeConfigPanelProps {
    browserSyncOnLogin: boolean;
    busy: boolean;
    error: string;
    manualSyncAllowed: boolean;
    message: string;
    ntpEnabled: boolean;
    ntpIntervalSec: number;
    ntpServersText: string;
    saveConfig: () => void;
    setBrowserSyncOnLogin: (value: boolean) => void;
    setManualSyncAllowed: (value: boolean) => void;
    setNtpEnabled: (value: boolean) => void;
    setNtpIntervalSec: (value: number) => void;
    setNtpServersText: (value: string) => void;
    setTimezone: (value: string) => void;
    status: TimeInfo;
    syncBrowserNow: () => void;
    syncNtp: () => void;
    timezone: string;
}

const syncSourceLabels: Record<TimeSyncSource, string> = {
    manual: '手动校时',
    onvif: 'ONVIF',
    ntp: 'NTP',
    browser: '浏览器',
    unknown: '未知',
};

const commonNtpServers = [
    'pool.ntp.org',
    'cn.pool.ntp.org',
    'ntp.aliyun.com',
    'time.cloudflare.com',
];

function formatTime(timestampMs: number) {
    if (timestampMs <= 0) {
        return '--';
    }
    return new Date(timestampMs).toLocaleString();
}

function sourceLabel(source: TimeSyncSource) {
    return syncSourceLabels[source] || source;
}

function ntpServerRows(text: string) {
    const rows = text.split(/\r?\n/);
    if (rows.length === 0 || rows.every((row) => row.trim().length === 0)) {
        return [''];
    }
    return rows;
}

function joinNtpServerRows(rows: string[]) {
    return rows.join('\n');
}

export function TimeConfigPanel({
    browserSyncOnLogin,
    busy,
    error,
    manualSyncAllowed,
    message,
    ntpEnabled,
    ntpIntervalSec,
    ntpServersText,
    saveConfig,
    setBrowserSyncOnLogin,
    setManualSyncAllowed,
    setNtpEnabled,
    setNtpIntervalSec,
    setNtpServersText,
    setTimezone,
    status,
    syncBrowserNow,
    syncNtp,
    timezone,
}: TimeConfigPanelProps) {
    const [browserNow, setBrowserNow] = useState(Date.now());
    const ntpServers = useMemo(
        () => ntpServerRows(ntpServersText),
        [ntpServersText],
    );
    const activeNtpServerCount = ntpServers.filter(
        (server) => server.trim().length > 0,
    ).length;

    useEffect(() => {
        const timer = window.setInterval(() => setBrowserNow(Date.now()), 1000);
        return () => window.clearInterval(timer);
    }, []);

    const updateNtpServer = (index: number, value: string) => {
        const nextServers = [...ntpServers];
        nextServers[index] = value;
        setNtpServersText(joinNtpServerRows(nextServers));
    };

    const addNtpServer = (server = '') => {
        const nextServers =
            ntpServers.length === 1 && ntpServers[0].trim() === ''
                ? [server]
                : [...ntpServers, server];
        setNtpServersText(joinNtpServerRows(nextServers));
    };

    const removeNtpServer = (index: number) => {
        const nextServers = ntpServers.filter(
            (_, rowIndex) => rowIndex !== index,
        );
        setNtpServersText(
            joinNtpServerRows(nextServers.length > 0 ? nextServers : ['']),
        );
    };

    const addCommonNtpServer = (server: string) => {
        const currentServers = ntpServers.map((item) => item.trim());
        if (currentServers.includes(server)) {
            return;
        }
        addNtpServer(server);
    };

    return (
        <div className="page-grid time-config-grid">
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
                {message ? (
                    <div className="status-note success-note">{message}</div>
                ) : null}

                <div className="time-status-row">
                    <div>
                        <span>设备时间</span>
                        <strong>{formatTime(status.system_time_ms)}</strong>
                    </div>
                    <div>
                        <span>浏览器时间</span>
                        <strong>{formatTime(browserNow)}</strong>
                    </div>
                    <div>
                        <span>最近同步</span>
                        <strong>
                            {sourceLabel(status.last_sync_source)} /{' '}
                            {status.last_sync_ok ? '成功' : '失败'}
                        </strong>
                    </div>
                    <div>
                        <span>同步时间</span>
                        <strong>{formatTime(status.last_sync_time_ms)}</strong>
                    </div>
                </div>
            </section>

            <section className="panel">
                <div className="time-panel-heading">
                    <div>
                        <h2>时间设置</h2>
                        <span>时区、手动校时和登录校时</span>
                    </div>
                </div>
                <div className="time-setting-list">
                    <label className="time-setting-row">
                        <span>
                            <strong>时区</strong>
                            <em>设备日志和事件时间显示使用该时区</em>
                        </span>
                        <input
                            value={timezone}
                            onChange={(event) =>
                                setTimezone(event.target.value)
                            }
                            placeholder="Asia/Shanghai"
                        />
                    </label>
                    <label className="time-switch-row">
                        <input
                            type="checkbox"
                            checked={manualSyncAllowed}
                            onChange={(event) => {
                                setManualSyncAllowed(event.target.checked);
                                if (!event.target.checked) {
                                    setBrowserSyncOnLogin(false);
                                }
                            }}
                        />
                        <span>
                            <strong>允许手动/浏览器校时</strong>
                            <em>
                                关闭后，浏览器时间同步和登录后自动校时都会停止
                            </em>
                        </span>
                    </label>
                    <label className="time-switch-row">
                        <input
                            type="checkbox"
                            checked={browserSyncOnLogin}
                            disabled={!manualSyncAllowed}
                            onChange={(event) =>
                                setBrowserSyncOnLogin(event.target.checked)
                            }
                        />
                        <span>
                            <strong>登录后同步浏览器时间</strong>
                            <em>
                                账号密码登录成功后，用浏览器当前时间校准一次设备
                            </em>
                        </span>
                    </label>
                </div>
            </section>

            <section className="panel">
                <div className="time-panel-heading">
                    <div>
                        <h2>NTP 设置</h2>
                        <span>{activeNtpServerCount} 个服务器地址</span>
                    </div>
                    <button
                        type="button"
                        onClick={syncNtp}
                        disabled={busy || !ntpEnabled}
                    >
                        立即同步
                    </button>
                </div>
                <div className="time-setting-list">
                    <label className="time-switch-row">
                        <input
                            type="checkbox"
                            checked={ntpEnabled}
                            onChange={(event) =>
                                setNtpEnabled(event.target.checked)
                            }
                        />
                        <span>
                            <strong>启用 NTP</strong>
                            <em>按同步间隔自动从服务器校准设备时间</em>
                        </span>
                    </label>
                    <label className="time-setting-row">
                        <span>
                            <strong>同步间隔</strong>
                            <em>单位：秒</em>
                        </span>
                        <input
                            type="number"
                            min={1}
                            value={ntpIntervalSec}
                            onChange={(event) =>
                                setNtpIntervalSec(Number(event.target.value))
                            }
                        />
                    </label>
                    <div className="time-server-editor">
                        <div className="time-server-editor-heading">
                            <span>
                                <strong>NTP 服务器地址</strong>
                                <em>按优先级从上到下尝试，支持域名或 IP</em>
                            </span>
                            <button
                                type="button"
                                onClick={() => addNtpServer()}
                            >
                                新增地址
                            </button>
                        </div>
                        <div className="time-server-list">
                            {ntpServers.map((server, index) => (
                                <div className="time-server-row" key={index}>
                                    <strong>{index + 1}</strong>
                                    <input
                                        value={server}
                                        onChange={(event) =>
                                            updateNtpServer(
                                                index,
                                                event.target.value,
                                            )
                                        }
                                        placeholder="pool.ntp.org"
                                    />
                                    <button
                                        type="button"
                                        disabled={
                                            ntpServers.length === 1 &&
                                            server.trim() === ''
                                        }
                                        onClick={() => removeNtpServer(index)}
                                    >
                                        删除
                                    </button>
                                </div>
                            ))}
                        </div>
                        <div className="time-server-presets">
                            {commonNtpServers.map((server) => (
                                <button
                                    type="button"
                                    key={server}
                                    disabled={ntpServers
                                        .map((item) => item.trim())
                                        .includes(server)}
                                    onClick={() => addCommonNtpServer(server)}
                                >
                                    {server}
                                </button>
                            ))}
                        </div>
                    </div>
                </div>
            </section>
        </div>
    );
}

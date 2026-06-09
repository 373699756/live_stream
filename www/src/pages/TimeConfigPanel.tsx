import { useEffect, useState } from 'react';
import type { TimeStatus, TimeSyncSource } from '../api/types';

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
  status: TimeStatus;
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

function formatTime(timestampMs: number) {
  if (timestampMs <= 0) {
    return '--';
  }
  return new Date(timestampMs).toLocaleString();
}

function sourceLabel(source: TimeSyncSource) {
  return syncSourceLabels[source] || source;
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

  useEffect(() => {
    const timer = window.setInterval(() => setBrowserNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);

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
            <button type="button" onClick={syncNtp} disabled={busy || !ntpEnabled}>
              NTP 立即同步
            </button>
            <button type="button" className="primary" onClick={saveConfig} disabled={busy}>
              保存配置
            </button>
          </div>
        </div>

        {error ? <div className="status-note error-note">{error}</div> : null}
        {message ? <div className="status-note success-note">{message}</div> : null}

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
              {sourceLabel(status.last_sync_source)} / {status.last_sync_ok ? '成功' : '失败'}
            </strong>
          </div>
          <div>
            <span>同步时间</span>
            <strong>{formatTime(status.last_sync_time_ms)}</strong>
          </div>
        </div>
      </section>

      <section className="panel">
        <h2>基础设置</h2>
        <div className="form-grid">
          <label>
            <span>时区</span>
            <input
              value={timezone}
              onChange={(event) => setTimezone(event.target.value)}
              placeholder="Asia/Shanghai"
            />
          </label>
          <label className="checkbox-row">
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
            <span>允许手动/浏览器校时</span>
          </label>
          <label className="checkbox-row">
            <input
              type="checkbox"
              checked={browserSyncOnLogin}
              disabled={!manualSyncAllowed}
              onChange={(event) => setBrowserSyncOnLogin(event.target.checked)}
            />
            <span>登录成功后使用浏览器时间同步一次</span>
          </label>
        </div>
      </section>

      <section className="panel">
        <h2>NTP 设置</h2>
        <div className="form-grid">
          <label className="checkbox-row">
            <input
              type="checkbox"
              checked={ntpEnabled}
              onChange={(event) => setNtpEnabled(event.target.checked)}
            />
            <span>启用 NTP</span>
          </label>
          <label>
            <span>同步间隔 sec</span>
            <input
              type="number"
              min={1}
              value={ntpIntervalSec}
              onChange={(event) => setNtpIntervalSec(Number(event.target.value))}
            />
          </label>
          <label className="time-server-field">
            <span>NTP 服务器</span>
            <textarea
              rows={4}
              value={ntpServersText}
              onChange={(event) => setNtpServersText(event.target.value)}
              placeholder={'pool.ntp.org\nntp.aliyun.com'}
            />
          </label>
        </div>
      </section>
    </div>
  );
}

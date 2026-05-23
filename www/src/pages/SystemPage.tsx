import { useUpgrade } from '../hooks/useUpgrade';
import type { UpgradeStatus } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';

function formatTimestamp(timestampMs: number) {
  if (timestampMs <= 0) {
    return '-';
  }
  return new Date(timestampMs).toLocaleString();
}

function formatBytes(sizeBytes: number) {
  if (sizeBytes <= 0) {
    return '-';
  }
  if (sizeBytes >= 1024 * 1024) {
    return `${(sizeBytes / (1024 * 1024)).toFixed(2)} MB`;
  }
  if (sizeBytes >= 1024) {
    return `${(sizeBytes / 1024).toFixed(1)} KB`;
  }
  return `${sizeBytes} B`;
}

function canCancel(status: UpgradeStatus) {
  return (
    status.state === 'validating' ||
    status.state === 'preparing' ||
    status.state === 'writing'
  );
}

function canConfirmReboot(status: UpgradeStatus) {
  return status.state === 'waiting_reboot';
}

function canStartUpgrade(status: UpgradeStatus) {
  return (
    status.state === 'idle' ||
    status.state === 'completed' ||
    status.state === 'failed' ||
    status.state === 'canceled'
  );
}

function statusLabel(status: UpgradeStatus) {
  if (status.current_stage) {
    return status.current_stage;
  }
  return status.state;
}

export function SystemPage() {
  const {
    status,
    upgradeStatus,
    packageInfo,
    selectedFile,
    allowSameVersion,
    setAllowSameVersion,
    allowDowngrade,
    setAllowDowngrade,
    autoReboot,
    setAutoReboot,
    busy,
    message,
    error,
    selectFile,
    uploadPackage,
    startUpgrade,
    cancelUpgrade,
    confirmReboot,
  } = useUpgrade();

  if (!status || !upgradeStatus) {
    return <div className="panel">加载系统状态...</div>;
  }

  return (
    <div className="page-grid system-grid">
      <section className="panel">
        <h2>系统状态</h2>
        <div className="metric-grid">
          <div>
            <span>CPU</span>
            <strong>{status.cpu}%</strong>
          </div>
          <div>
            <span>内存</span>
            <strong>{status.memory}%</strong>
          </div>
          <div>
            <span>温度</span>
            <strong>{status.temperature} C</strong>
          </div>
          <div>
            <span>运行时间</span>
            <strong>{status.uptime}</strong>
          </div>
        </div>
      </section>

      <section className="panel">
        <h2>设备信息</h2>
        <div className="info-table">
          <div>
            <span>设备名</span>
            <strong>{status.deviceName}</strong>
          </div>
          <div>
            <span>型号</span>
            <strong>{status.model}</strong>
          </div>
          <div>
            <span>固件版本</span>
            <strong>{status.firmware}</strong>
          </div>
        </div>
      </section>

      <section className="panel wide-panel">
        <h2>固件升级</h2>
        <div className="upgrade-grid">
          <div className="upgrade-section">
            <div className="form-field form-field-stacked">
              <span className="form-label">升级包</span>
              <div className="form-control file-upload-control">
                <input
                  type="file"
                  accept=".bin,.img,.tar,.tgz,.zip"
                  onChange={(event) => {
                    selectFile(event.target.files?.[0] ?? null);
                  }}
                />
                <button
                  type="button"
                  className="primary"
                  disabled={!selectedFile || busy}
                  onClick={() => {
                    void uploadPackage();
                  }}
                >
                  上传并校验
                </button>
              </div>
            </div>

            <div className="inline-checks">
              <label>
                <input
                  type="checkbox"
                  checked={allowSameVersion}
                  onChange={(event) => setAllowSameVersion(event.target.checked)}
                />
                允许同版本覆盖
              </label>
              <label>
                <input
                  type="checkbox"
                  checked={allowDowngrade}
                  onChange={(event) => setAllowDowngrade(event.target.checked)}
                />
                允许降级
              </label>
              <label>
                <input
                  type="checkbox"
                  checked={autoReboot}
                  onChange={(event) => setAutoReboot(event.target.checked)}
                />
                完成后自动重启
              </label>
            </div>

            <div className="form-actions form-actions-left">
              <button
                type="button"
                className="primary"
                disabled={!packageInfo || busy || !canStartUpgrade(upgradeStatus)}
                onClick={() => {
                  void startUpgrade();
                }}
              >
                开始升级
              </button>
              <button
                type="button"
                disabled={busy || !canCancel(upgradeStatus)}
                onClick={() => {
                  void cancelUpgrade();
                }}
              >
                取消升级
              </button>
              <button
                type="button"
                disabled={busy || !canConfirmReboot(upgradeStatus)}
                onClick={() => {
                  void confirmReboot();
                }}
              >
                确认重启
              </button>
            </div>

            {message ? <div className="status-note success-note">{message}</div> : null}
            {error ? <div className="status-note error-note">{error}</div> : null}
          </div>

          <div className="upgrade-section">
            <div className="upgrade-progress">
              <div className="upgrade-progress-header">
                <strong>{statusLabel(upgradeStatus)}</strong>
                <span>{upgradeStatus.progress_percent}%</span>
              </div>
              <div className="progress-track">
                <div
                  className="progress-fill"
                  style={{ width: `${upgradeStatus.progress_percent}%` }}
                />
              </div>
            </div>

            <div className="upgrade-status-grid">
              <div>
                <span>状态</span>
                <strong>{upgradeStatus.state}</strong>
              </div>
              <div>
                <span>目标版本</span>
                <strong>{upgradeStatus.target_version || '-'}</strong>
              </div>
              <div>
                <span>开始时间</span>
                <strong>{formatTimestamp(upgradeStatus.started_at_ms)}</strong>
              </div>
              <div>
                <span>结束时间</span>
                <strong>{formatTimestamp(upgradeStatus.finished_at_ms)}</strong>
              </div>
              <div>
                <span>错误</span>
                <strong>{upgradeStatus.error_message || '-'}</strong>
              </div>
            </div>
          </div>
        </div>

        <div className="upgrade-meta">
          <div className="panel-title">已校验包信息</div>
          <div className="upgrade-package-grid">
            <div>
              <span>包路径</span>
              <strong>{packageInfo?.package_path || '-'}</strong>
            </div>
            <div>
              <span>版本</span>
              <strong>{packageInfo?.version || '-'}</strong>
            </div>
            <div>
              <span>大小</span>
              <strong>{packageInfo ? formatBytes(packageInfo.size_bytes) : '-'}</strong>
            </div>
            <div>
              <span>摘要</span>
              <code>{packageInfo?.digest || '-'}</code>
            </div>
            <div>
              <span>目标型号</span>
              <strong>{packageInfo?.target_model || '-'}</strong>
            </div>
            <div>
              <span>构建时间</span>
              <strong>
                {packageInfo ? formatTimestamp(packageInfo.build_time_ms) : '-'}
              </strong>
            </div>
          </div>
        </div>
      </section>

      <section className="panel wide-panel">
        <h2>服务状态</h2>
        <div className="service-list">
          {status.services.map((service) => (
            <div key={service.name}>
              <span>{service.name}</span>
              <StatusBadge state={service.state} />
            </div>
          ))}
        </div>
      </section>
    </div>
  );
}

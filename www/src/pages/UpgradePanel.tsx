import type { UpgradePackageInfo, UpgradeStatus } from '../api/types';
import { formatBytes, formatTimestamp } from '../utils/format';

interface UpgradePanelProps {
  actionError: string;
  allowDowngrade: boolean;
  allowSameVersion: boolean;
  autoReboot: boolean;
  busy: boolean;
  cancelUpgrade: () => Promise<void>;
  confirmReboot: () => Promise<void>;
  message: string;
  packageInfo: UpgradePackageInfo | null;
  refreshError: string;
  selectedFile: File | null;
  selectFile: (file: File | null) => void;
  setAllowDowngrade: (value: boolean) => void;
  setAllowSameVersion: (value: boolean) => void;
  setAutoReboot: (value: boolean) => void;
  startUpgrade: () => Promise<void>;
  upgradeStatus: UpgradeStatus;
  uploadPackage: () => Promise<void>;
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

function hasActiveUpgrade(status: UpgradeStatus) {
  return !canStartUpgrade(status);
}

const upgradeStateLabels: Record<UpgradeStatus['state'], string> = {
  idle: '空闲',
  validating: '校验中',
  preparing: '准备写入',
  writing: '写入中',
  committing: '提交中',
  waiting_reboot: '等待重启',
  completed: '已完成',
  failed: '失败',
  canceled: '已取消',
};

function upgradeStageText(status: UpgradeStatus) {
  if (status.current_stage) {
    return status.current_stage;
  }
  return upgradeStateLabels[status.state];
}

function startHint(packageInfo: UpgradePackageInfo | null, status: UpgradeStatus) {
  if (!packageInfo) {
    return '上传并校验通过后才可开始写入。';
  }
  if (!canStartUpgrade(status)) {
    return '当前升级流程未结束，不能重复开始。';
  }
  return '开始升级后会进入准备写入、写入和提交阶段。';
}

function cancelHint(status: UpgradeStatus) {
  if (status.state === 'writing') {
    return '写入阶段可提交取消请求，是否中断由后端按平台能力处理。';
  }
  if (canCancel(status)) {
    return '当前阶段可提交取消请求。';
  }
  if (status.state === 'committing' || status.state === 'waiting_reboot') {
    return '提交和等待重启阶段不可取消。';
  }
  return '仅校验、准备和写入阶段可取消。';
}

export function UpgradePanel({
  actionError,
  allowDowngrade,
  allowSameVersion,
  autoReboot,
  busy,
  cancelUpgrade,
  confirmReboot,
  message,
  packageInfo,
  refreshError,
  selectedFile,
  selectFile,
  setAllowDowngrade,
  setAllowSameVersion,
  setAutoReboot,
  startUpgrade,
  upgradeStatus,
  uploadPackage,
}: UpgradePanelProps) {
  const activeUpgrade = hasActiveUpgrade(upgradeStatus);
  const packageInputsDisabled = busy || activeUpgrade;

  return (
    <section className="panel wide-panel upgrade-panel">
      <div className="page-heading">
        <div>
          <h2>固件升级</h2>
          <p>先上传并校验升级包，校验通过后再开始写入设备。</p>
        </div>
      </div>

      <div className="upgrade-grid">
        <div className="upgrade-section upgrade-status-section">
          <div className="panel-title">当前升级状态</div>
          <div className={`upgrade-state-banner state-${upgradeStatus.state}`}>
            <div>
              <span>当前状态</span>
              <strong>{upgradeStateLabels[upgradeStatus.state]}</strong>
            </div>
            <div>
              <span>当前阶段</span>
              <strong>{upgradeStageText(upgradeStatus)}</strong>
            </div>
          </div>

          <div className="upgrade-progress">
            <div className="upgrade-progress-header">
              <strong>进度</strong>
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
              <span>目标版本</span>
              <strong>{upgradeStatus.target_version || '-'}</strong>
            </div>
            <div>
              <span>状态码</span>
              <strong>{upgradeStatus.state}</strong>
            </div>
            <div>
              <span>开始时间</span>
              <strong>{formatTimestamp(upgradeStatus.started_at_ms)}</strong>
            </div>
            <div>
              <span>结束时间</span>
              <strong>{formatTimestamp(upgradeStatus.finished_at_ms)}</strong>
            </div>
            <div className="wide-status-cell">
              <span>错误</span>
              <strong>{upgradeStatus.error_message || '-'}</strong>
            </div>
          </div>
        </div>

        <div className="upgrade-section">
          <div className="panel-title">1. 选择升级包</div>
          <div className="form-field form-field-stacked">
            <span className="form-label">升级包</span>
            <div className="form-control file-upload-control">
              <input
                type="file"
                accept=".bin,.img,.tar,.tgz,.zip"
                disabled={packageInputsDisabled}
                onChange={(event) => {
                  selectFile(event.target.files?.[0] ?? null);
                }}
              />
              <button
                type="button"
                className="primary"
                disabled={!selectedFile || packageInputsDisabled}
                onClick={() => {
                  void uploadPackage();
                }}
              >
                上传并校验
              </button>
            </div>
          </div>

          <div className="upgrade-file-summary">
            <span>已选择</span>
            <strong>
              {activeUpgrade
                ? '升级流程运行中，不能更换升级包'
                : selectedFile
                ? `${selectedFile.name} / ${formatBytes(selectedFile.size)}`
                : '未选择升级包'}
            </strong>
          </div>
        </div>

        <div className="upgrade-section">
          <div className="panel-title">2. 升级选项</div>
          <div className="inline-checks upgrade-option-list">
            <label className="form-control">
              <input
                type="checkbox"
                checked={allowSameVersion}
                disabled={packageInputsDisabled}
                onChange={(event) => setAllowSameVersion(event.target.checked)}
              />
              允许同版本覆盖
            </label>
            <label className="form-control">
              <input
                type="checkbox"
                checked={allowDowngrade}
                disabled={packageInputsDisabled}
                onChange={(event) => setAllowDowngrade(event.target.checked)}
              />
              允许降级
            </label>
            <label className="form-control">
              <input
                type="checkbox"
                checked={autoReboot}
                disabled={packageInputsDisabled}
                onChange={(event) => setAutoReboot(event.target.checked)}
              />
              完成后自动重启
            </label>
          </div>
        </div>

        <div className="upgrade-section upgrade-action-section">
          <div className="panel-title">3. 执行动作</div>
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
              className="danger-action"
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

          <div className="upgrade-action-notes">
            <span>{startHint(packageInfo, upgradeStatus)}</span>
            <span>{cancelHint(upgradeStatus)}</span>
          </div>

          {message ? <div className="status-note success-note">{message}</div> : null}
          {actionError ? <div className="status-note error-note">{actionError}</div> : null}
          {refreshError ? (
            <div className="status-note warning-note">{refreshError}</div>
          ) : null}
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
          <div>
            <span>需要重启</span>
            <strong>{packageInfo ? (packageInfo.requires_reboot ? '是' : '否') : '-'}</strong>
          </div>
        </div>
      </div>
    </section>
  );
}

import type { UpgradePackageInfo, UpgradeInfo } from '../api/types';
import { formatBytes, formatTimestamp } from '../utils/format';

interface UpgradePanelProps {
    actionError: string;
    allowDowngrade: boolean;
    allowSameVersion: boolean;
    autoReboot: boolean;
    busy: boolean;
    cancelUpgrade: () => Promise<void>;
    confirmReboot: () => Promise<void>;
    msg: string;
    packageInfo: UpgradePackageInfo | null;
    refreshError: string;
    selectedFile: File | null;
    selectFile: (file: File | null) => void;
    setAllowDowngrade: (value: boolean) => void;
    setAllowSameVersion: (value: boolean) => void;
    setAutoReboot: (value: boolean) => void;
    startUpgrade: () => Promise<void>;
    upgradeInfo: UpgradeInfo;
    uploadPackage: () => Promise<void>;
}

function canCancel(status: UpgradeInfo) {
    return (
        status.state === 'validating' ||
        status.state === 'preparing'
    );
}

function canConfirmReboot(status: UpgradeInfo) {
    return status.state === 'waiting_reboot';
}

function canStartUpgrade(status: UpgradeInfo) {
    return (
        status.state === 'idle' ||
        status.state === 'completed' ||
        status.state === 'failed' ||
        status.state === 'canceled'
    );
}

function hasActiveUpgrade(status: UpgradeInfo) {
    return !canStartUpgrade(status);
}

const upgradeStateLabels: Record<UpgradeInfo['state'], string> = {
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

function upgradeStageText(status: UpgradeInfo) {
    if (status.current_stage) {
        return status.current_stage;
    }
    return upgradeStateLabels[status.state];
}

function startHint(
    packageInfo: UpgradePackageInfo | null,
    status: UpgradeInfo,
) {
    if (!packageInfo) {
        return '上传并校验通过后才可开始写入。';
    }
    if (!canStartUpgrade(status)) {
        return '当前升级流程未结束，不能重复开始。';
    }
    return '开始升级后会进入准备写入、写入和提交阶段。';
}

function cancelHint(status: UpgradeInfo) {
    if (status.state === 'writing') {
        return '写入阶段正在擦写 Flash，不可取消，请勿断电。';
    }
    if (canCancel(status)) {
        return '当前阶段可提交取消请求。';
    }
    if (status.state === 'committing' || status.state === 'waiting_reboot') {
        return '提交和等待重启阶段不可取消。';
    }
    return '仅校验和准备阶段可取消。';
}

export function UpgradePanel({
    actionError,
    allowDowngrade,
    allowSameVersion,
    autoReboot,
    busy,
    cancelUpgrade,
    confirmReboot,
    msg,
    packageInfo,
    refreshError,
    selectedFile,
    selectFile,
    setAllowDowngrade,
    setAllowSameVersion,
    setAutoReboot,
    startUpgrade,
    upgradeInfo,
    uploadPackage,
}: UpgradePanelProps) {
    const activeUpgrade = hasActiveUpgrade(upgradeInfo);
    const packageInputsDisabled = busy || activeUpgrade;

    return (
        <section className="panel wide-panel upgrade-panel">
            <div className="page-heading">
                <div>
                    <h2>固件升级</h2>
                    <p>Web 入口只允许升级 Web Console 分区；写入阶段请勿断电。</p>
                </div>
            </div>

            <div className="upgrade-grid">
                <div className="upgrade-flow-column">
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
                                        selectFile(
                                            event.target.files?.[0] ?? null,
                                        );
                                    }}
                                />
                                <button
                                    type="button"
                                    className="primary"
                                    disabled={
                                        !selectedFile || packageInputsDisabled
                                    }
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
                                    onChange={(event) =>
                                        setAllowSameVersion(
                                            event.target.checked,
                                        )
                                    }
                                />
                                允许同版本覆盖
                            </label>
                            <label className="form-control">
                                <input
                                    type="checkbox"
                                    checked={allowDowngrade}
                                    disabled={packageInputsDisabled}
                                    onChange={(event) =>
                                        setAllowDowngrade(event.target.checked)
                                    }
                                />
                                允许降级
                            </label>
                            <label className="form-control">
                                <input
                                    type="checkbox"
                                    checked={autoReboot}
                                    disabled={packageInputsDisabled}
                                    onChange={(event) =>
                                        setAutoReboot(event.target.checked)
                                    }
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
                                disabled={
                                    !packageInfo ||
                                    busy ||
                                    !canStartUpgrade(upgradeInfo)
                                }
                                onClick={() => {
                                    void startUpgrade();
                                }}
                            >
                                开始升级
                            </button>
                            <button
                                type="button"
                                className="danger-action"
                                disabled={busy || !canCancel(upgradeInfo)}
                                onClick={() => {
                                    void cancelUpgrade();
                                }}
                            >
                                取消升级
                            </button>
                            <button
                                type="button"
                                disabled={
                                    busy || !canConfirmReboot(upgradeInfo)
                                }
                                onClick={() => {
                                    void confirmReboot();
                                }}
                            >
                                确认重启
                            </button>
                        </div>

                        <div className="upgrade-action-notes">
                            <span>{startHint(packageInfo, upgradeInfo)}</span>
                            <span>{cancelHint(upgradeInfo)}</span>
                        </div>

                        {msg ? (
                            <div className="status-note success-note">
                                {msg}
                            </div>
                        ) : null}
                        {actionError ? (
                            <div className="status-note error-note">
                                {actionError}
                            </div>
                        ) : null}
                        {refreshError ? (
                            <div className="status-note warning-note">
                                {refreshError}
                            </div>
                        ) : null}
                    </div>

                    <div className="upgrade-meta">
                        <div className="panel-title">已校验包信息</div>
                        <div className="upgrade-package-grid">
                            <div>
                                <span>包路径</span>
                                <strong>
                                    {packageInfo?.package_path || '-'}
                                </strong>
                            </div>
                            <div>
                                <span>版本</span>
                                <strong>{packageInfo?.version || '-'}</strong>
                            </div>
                            <div>
                                <span>大小</span>
                                <strong>
                                    {packageInfo
                                        ? formatBytes(packageInfo.size_bytes)
                                        : '-'}
                                </strong>
                            </div>
                            <div>
                                <span>摘要</span>
                                <code>{packageInfo?.digest || '-'}</code>
                            </div>
                            <div>
                                <span>目标型号</span>
                                <strong>
                                    {packageInfo?.target_model || '-'}
                                </strong>
                            </div>
                            <div>
                                <span>构建时间</span>
                                <strong>
                                    {packageInfo
                                        ? formatTimestamp(
                                              packageInfo.build_time_ms,
                                          )
                                        : '-'}
                                </strong>
                            </div>
                            <div>
                                <span>需要重启</span>
                                <strong>
                                    {packageInfo
                                        ? packageInfo.requires_reboot
                                            ? '是'
                                            : '否'
                                        : '-'}
                                </strong>
                            </div>
                        </div>
                    </div>
                </div>

                <div className="upgrade-section upgrade-status-section">
                    <div className="panel-title">当前升级状态</div>
                    <div
                        className={`upgrade-state-banner state-${upgradeInfo.state}`}
                    >
                        <div>
                            <span>当前状态</span>
                            <strong>
                                {upgradeStateLabels[upgradeInfo.state]}
                            </strong>
                        </div>
                        <div>
                            <span>当前阶段</span>
                            <strong>{upgradeStageText(upgradeInfo)}</strong>
                        </div>
                    </div>

                    <div className="upgrade-progress">
                        <div className="upgrade-progress-header">
                            <strong>进度</strong>
                            <span>{upgradeInfo.progress_percent}%</span>
                        </div>
                        <div className="progress-track">
                            <div
                                className="progress-fill"
                                style={{
                                    width: `${upgradeInfo.progress_percent}%`,
                                }}
                            />
                        </div>
                    </div>

                    <div className="upgrade-status-grid">
                        <div>
                            <span>目标版本</span>
                            <strong>
                                {upgradeInfo.target_version || '-'}
                            </strong>
                        </div>
                        <div>
                            <span>状态码</span>
                            <strong>{upgradeInfo.state}</strong>
                        </div>
                        <div>
                            <span>开始时间</span>
                            <strong>
                                {formatTimestamp(upgradeInfo.started_at_ms)}
                            </strong>
                        </div>
                        <div>
                            <span>结束时间</span>
                            <strong>
                                {formatTimestamp(upgradeInfo.finished_at_ms)}
                            </strong>
                        </div>
                        <div className="wide-status-cell">
                            <span>错误</span>
                            <strong>
                                {upgradeInfo.error_message || '-'}
                            </strong>
                        </div>
                    </div>
                </div>
            </div>
        </section>
    );
}

import type { UpgradePackageInfo, UpgradeInfo } from '../api/types';
import type { UpgradeActionErrorSeverity } from '../hooks/useUpgrade';
import { formatBytes, formatTimestamp } from '../utils/displayText';

interface UpgradePackagePanelProps {
    actionError: string;
    actionErrorSeverity: UpgradeActionErrorSeverity;
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
    return status.state === 'validating' || status.state === 'preparing';
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

function PackageUploadSection({
    activeUpgrade,
    packageInfo,
    packageInputsDisabled,
    selectedFile,
    selectFile,
    uploadPackage,
}: UpgradePackagePanelProps & {
    activeUpgrade: boolean;
    packageInputsDisabled: boolean;
}) {
    return (
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

            {selectedFile && !packageInfo && !activeUpgrade ? (
                <div className="status-note warning-note">
                    已选择本地文件，尚未完成上传校验；此阶段不会写入 Flash。
                </div>
            ) : null}
            {packageInfo ? (
                <div className="status-note warning-note">
                    升级包已通过校验；开始升级后才会进入写入流程。
                </div>
            ) : null}
        </div>
    );
}

function UpgradeOptionsSection({
    allowDowngrade,
    allowSameVersion,
    autoReboot,
    packageInputsDisabled,
    setAllowDowngrade,
    setAllowSameVersion,
    setAutoReboot,
}: UpgradePackagePanelProps & {
    packageInputsDisabled: boolean;
}) {
    return (
        <div className="upgrade-section">
            <div className="panel-title">2. 升级选项</div>
            <div className="inline-checks upgrade-option-list">
                <label className="form-control">
                    <input
                        type="checkbox"
                        checked={allowSameVersion}
                        disabled={packageInputsDisabled}
                        onChange={(event) =>
                            setAllowSameVersion(event.target.checked)
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
                        onChange={(event) => setAutoReboot(event.target.checked)}
                    />
                    完成后自动重启
                </label>
            </div>
        </div>
    );
}

function UpgradeActionsSection({
    actionError,
    actionErrorSeverity,
    busy,
    cancelUpgrade,
    confirmReboot,
    msg,
    packageInfo,
    refreshError,
    startUpgrade,
    upgradeInfo,
}: UpgradePackagePanelProps) {
    return (
        <div className="upgrade-section upgrade-action-section">
            <div className="panel-title">3. 执行动作</div>
            <div className="form-actions form-actions-left">
                <button
                    type="button"
                    className="primary"
                    disabled={!packageInfo || busy || !canStartUpgrade(upgradeInfo)}
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
                    disabled={busy || !canConfirmReboot(upgradeInfo)}
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

            {msg ? <div className="status-note success-note">{msg}</div> : null}
            {actionError ? (
                <div
                    className={`status-note ${
                        actionErrorSeverity === 'warning'
                            ? 'warning-note'
                            : 'error-note'
                    }`}
                >
                    {actionError}
                </div>
            ) : null}
            {refreshError ? (
                <div className="status-note warning-note">{refreshError}</div>
            ) : null}
        </div>
    );
}

function PackageInfoSection({
    packageInfo,
}: UpgradePackagePanelProps) {
    return (
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
                    <strong>
                        {packageInfo ? formatBytes(packageInfo.size_bytes) : '-'}
                    </strong>
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
                        {packageInfo
                            ? formatTimestamp(packageInfo.build_time_ms)
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
    );
}

export function UpgradePackagePanel(props: UpgradePackagePanelProps) {
    const activeUpgrade = hasActiveUpgrade(props.upgradeInfo);
    const packageInputsDisabled = props.busy || activeUpgrade;

    return (
        <div className="upgrade-flow-column">
            <PackageUploadSection
                {...props}
                activeUpgrade={activeUpgrade}
                packageInputsDisabled={packageInputsDisabled}
            />
            <UpgradeOptionsSection
                {...props}
                packageInputsDisabled={packageInputsDisabled}
            />
            <UpgradeActionsSection {...props} />
            <PackageInfoSection {...props} />
        </div>
    );
}

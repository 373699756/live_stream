import { useRef } from 'react';

import type { UpgradePackageInfo, UpgradeInfo } from '../api/types';
import type {
    UpgradeActionErrorScope,
    UpgradeActionErrorSeverity,
    UpgradeActionMessageTone,
} from '../hooks/useUpgrade';
import { formatBytes, formatTimestamp } from '../utils/displayText';
import { buildUpgradeDisplayInfo } from './upgradeDisplay';

interface UpgradePackagePanelProps {
    actionError: string;
    actionErrorScope: UpgradeActionErrorScope;
    actionErrorSeverity: UpgradeActionErrorSeverity;
    allowDowngrade: boolean;
    allowSameVersion: boolean;
    autoReboot: boolean;
    busy: boolean;
    cancelUpgrade: () => Promise<void>;
    confirmReboot: () => Promise<void>;
    msg: string;
    msgTone: UpgradeActionMessageTone;
    packageInfo: UpgradePackageInfo | null;
    refreshError: string;
    selectedFile: File | null;
    selectFile: (file: File | null) => void;
    setAllowDowngrade: (value: boolean) => void;
    setAllowSameVersion: (value: boolean) => void;
    setAutoReboot: (value: boolean) => void;
    startUpgrade: () => Promise<void>;
    upgradeInfo: UpgradeInfo;
    uploadPackage: (file?: File) => Promise<void>;
}

function canCancel(status: UpgradeInfo) {
    return status.state === 'validating' || status.state === 'preparing';
}

function canConfirmReboot(status: UpgradeInfo) {
    return status.state === 'waiting_reboot';
}

function canStartUpgrade(status: UpgradeInfo) {
    return !buildUpgradeDisplayInfo(status).isActive;
}

function hasActiveUpgrade(status: UpgradeInfo) {
    return buildUpgradeDisplayInfo(status).isActive;
}

function startHint(
    packageInfo: UpgradePackageInfo | null,
    status: UpgradeInfo,
) {
    const display = buildUpgradeDisplayInfo(status);
    if (!packageInfo) {
        return '上传并校验通过后才可开始写入。';
    }
    if (display.isActive) {
        return '当前升级流程未结束，不能重复开始。';
    }
    return '开始升级后会进入准备写入、写入和提交阶段。';
}

function cancelHint(status: UpgradeInfo) {
    const display = buildUpgradeDisplayInfo(status);
    if (display.state === 'rebooting') {
        return '设备正在重启恢复，不能提交新动作。';
    }
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
    actionError,
    actionErrorSeverity,
    actionErrorScope,
    packageInfo,
    packageInputsDisabled,
    selectedFile,
    selectFile,
    uploadPackage,
}: UpgradePackagePanelProps & {
    activeUpgrade: boolean;
    packageInputsDisabled: boolean;
}) {
    const fileInputRef = useRef<HTMLInputElement | null>(null);
    const selectedFileLabel = selectedFile
        ? `${selectedFile.name} / ${formatBytes(selectedFile.size)}`
        : '未选择升级包';
    const uploadedPath = packageInfo?.package_path || '';
    const pickerLabel = packageInputsDisabled
        ? activeUpgrade
            ? '升级进行中'
            : '正在上传校验...'
        : packageInfo
          ? '重新选择升级包'
          : '选择并校验升级包';

    return (
        <div className="upgrade-section">
            <div className="panel-title">1. 选择升级包</div>
            <div className="form-field form-field-stacked">
                <span className="form-label">升级包</span>
                <div className="form-control file-upload-control">
                    <input
                        ref={fileInputRef}
                        className="upgrade-file-input"
                        type="file"
                        accept=".bin,.img,.tar,.tgz,.zip"
                        disabled={packageInputsDisabled}
                        onChange={(event) => {
                            const file = event.target.files?.[0] ?? null;
                            event.target.value = '';
                            selectFile(file);
                            if (file) {
                                void uploadPackage(file);
                            }
                        }}
                    />
                    <button
                        type="button"
                        className="upgrade-file-picker primary"
                        disabled={packageInputsDisabled}
                        onClick={() => fileInputRef.current?.click()}
                    >
                        {pickerLabel}
                    </button>
                    <span className="upgrade-file-picker-name">
                        {activeUpgrade
                            ? '升级流程运行中，不能更换升级包'
                            : selectedFileLabel}
                    </span>
                </div>
            </div>

            <div className="upgrade-file-summary">
                <div>
                    <span>本地文件</span>
                    <strong>
                        {activeUpgrade
                            ? '升级流程运行中，不能更换升级包'
                            : selectedFileLabel}
                    </strong>
                </div>
                <div>
                    <span>上传路径</span>
                    <code>{uploadedPath || '上传校验后显示设备临时路径'}</code>
                </div>
            </div>

            {selectedFile &&
            !packageInfo &&
            packageInputsDisabled &&
            !activeUpgrade ? (
                <div className="status-note warning-note">
                    正在上传并校验；此阶段只写入临时内存目录，不会擦写 Flash。
                </div>
            ) : null}
            {actionError && actionErrorScope === 'upload' ? (
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
            {packageInfo ? (
                <div className="status-note success-note">
                    升级包已通过校验；开始升级后才会进入写入流程。
                </div>
            ) : null}
            <PackageInfoSection packageInfo={packageInfo} />
        </div>
    );
}

function UpgradeOptionsSection({
    allowDowngrade,
    allowSameVersion,
    autoReboot,
    optionInputsDisabled,
    setAllowDowngrade,
    setAllowSameVersion,
    setAutoReboot,
}: UpgradePackagePanelProps & {
    optionInputsDisabled: boolean;
}) {
    return (
        <div className="upgrade-section">
            <div className="panel-title">2. 升级选项</div>
            <div className="inline-checks upgrade-option-list">
                <label className="form-control">
                    <input
                        type="checkbox"
                        checked={allowSameVersion}
                        disabled={optionInputsDisabled}
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
                        disabled={optionInputsDisabled}
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
                        disabled={optionInputsDisabled}
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
    actionErrorScope,
    actionErrorSeverity,
    busy,
    cancelUpgrade,
    confirmReboot,
    msg,
    msgTone,
    packageInfo,
    startUpgrade,
    upgradeInfo,
}: UpgradePackagePanelProps) {
    return (
        <div className="upgrade-section upgrade-action-section">
            <div className="panel-title">3. 执行动作</div>
            <div className="form-actions form-actions-left">
                <button
                    type="button"
                    className={
                        canStartUpgrade(upgradeInfo) ? 'primary' : undefined
                    }
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
                    className={
                        canConfirmReboot(upgradeInfo) ? 'primary' : undefined
                    }
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

            {msg ? (
                <div
                    className={`status-note ${
                        msgTone === 'info' ? 'info-note' : 'success-note'
                    }`}
                >
                    {msg}
                </div>
            ) : null}
            {actionError && actionErrorScope === 'action' ? (
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
        </div>
    );
}

function PackageInfoSection({
    packageInfo,
}: {
    packageInfo: UpgradePackageInfo | null;
}) {
    if (!packageInfo) {
        return null;
    }
    return (
        <div className="upgrade-package-compact">
            <div className="panel-title">已校验包信息</div>
            <div className="upgrade-package-line">
                <span>版本</span>
                <strong>{packageInfo.version || '-'}</strong>
                <span>大小</span>
                <strong>{formatBytes(packageInfo.size_bytes)}</strong>
                <span>型号</span>
                <strong>{packageInfo.target_model || '-'}</strong>
                <span>重启</span>
                <strong>{packageInfo.requires_reboot ? '是' : '否'}</strong>
            </div>
            <div className="upgrade-package-line">
                <span>构建</span>
                <strong>{formatTimestamp(packageInfo.build_time_ms)}</strong>
                <span>摘要</span>
                <code>{packageInfo.digest || '-'}</code>
            </div>
        </div>
    );
}

export function UpgradePackagePanel(props: UpgradePackagePanelProps) {
    const activeUpgrade = hasActiveUpgrade(props.upgradeInfo);
    const packageInputsDisabled = props.busy || activeUpgrade;
    const optionInputsDisabled = activeUpgrade;

    return (
        <div className="upgrade-flow-column">
            <PackageUploadSection
                {...props}
                activeUpgrade={activeUpgrade}
                packageInputsDisabled={packageInputsDisabled}
            />
            <UpgradeOptionsSection
                {...props}
                optionInputsDisabled={optionInputsDisabled}
            />
            <UpgradeActionsSection {...props} />
        </div>
    );
}

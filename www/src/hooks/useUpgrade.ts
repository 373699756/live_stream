/**
 * useUpgrade — manages upgrade workflow state:
 * - Polls upgrade status every 2 seconds
 * - Handles file upload, start, cancel, confirm-reboot actions
 */

import { useEffect, useRef, useState } from 'react';
import {
    getUpgradeInfo,
    uploadUpgradePackage,
    startUpgrade as apiStartUpgrade,
    cancelUpgrade as apiCancelUpgrade,
    confirmUpgradeReboot as apiConfirmUpgradeReboot,
} from '../api/upgrade';
import type {
    UpgradePackageInfo,
    UpgradeRequest,
    UpgradeInfo,
} from '../api/types';

const UPGRADE_STATUS_POLL_INTERVAL_MS = 2000;
const UPGRADE_STATUS_REQUEST_TIMEOUT_MS = 5000;

export type UpgradeActionErrorSeverity = 'warning' | 'danger';

function errorMessage(error: unknown, fallback: string) {
    return error instanceof Error ? error.message : fallback;
}

export function useUpgrade() {
    const [upgradeInfo, setUpgradeInfo] = useState<UpgradeInfo | null>(null);
    const [packageInfo, setPackageInfo] = useState<UpgradePackageInfo | null>(
        null,
    );
    const [selectedFile, setSelectedFile] = useState<File | null>(null);
    const [allowSameVersion, setAllowSameVersion] = useState(false);
    const [allowDowngrade, setAllowDowngrade] = useState(false);
    const [autoReboot, setAutoReboot] = useState(false);
    const [busy, setBusy] = useState(false);
    const [msg, setMsg] = useState('');
    const [actionError, setActionError] = useState('');
    const [actionErrorSeverity, setActionErrorSeverity] =
        useState<UpgradeActionErrorSeverity>('warning');
    const [refreshError, setRefreshError] = useState('');
    const isStatusPollingPaused = useRef(false);

    const setUpgradeActionBusy = (value: boolean) => {
        isStatusPollingPaused.current = value;
        setBusy(value);
    };

    useEffect(() => {
        let mounted = true;
        let timer = 0;
        const load = async () => {
            if (isStatusPollingPaused.current) {
                timer = window.setTimeout(
                    load,
                    UPGRADE_STATUS_POLL_INTERVAL_MS,
                );
                return;
            }
            const startedAt = Date.now();
            try {
                const nextUpgradeInfo = await getUpgradeInfo({
                    timeoutMs: UPGRADE_STATUS_REQUEST_TIMEOUT_MS,
                });
                if (mounted) {
                    setUpgradeInfo(nextUpgradeInfo);
                    setRefreshError('');
                }
            } catch (err: unknown) {
                if (mounted && !isStatusPollingPaused.current) {
                    setRefreshError(errorMessage(err, '升级状态刷新失败'));
                }
            } finally {
                if (mounted) {
                    const elapsedMs = Date.now() - startedAt;
                    timer = window.setTimeout(
                        load,
                        Math.max(
                            0,
                            UPGRADE_STATUS_POLL_INTERVAL_MS - elapsedMs,
                        ),
                    );
                }
            }
        };
        void load();
        return () => {
            mounted = false;
            window.clearTimeout(timer);
        };
    }, []);

    const selectFile = (file: File | null) => {
        setSelectedFile(file);
        setPackageInfo(null);
        setActionError('');
        setActionErrorSeverity('warning');
        setMsg('');
    };

    const uploadPackage = async () => {
        if (!selectedFile) return;
        setUpgradeActionBusy(true);
        setActionError('');
        setRefreshError('');
        setMsg('');
        try {
            const uploaded = await uploadUpgradePackage(selectedFile);
            setPackageInfo(uploaded);
            setMsg(`已上传 ${selectedFile.name}`);
        } catch (err) {
            setActionErrorSeverity('warning');
            setActionError(errorMessage(err, '上传失败'));
        } finally {
            setUpgradeActionBusy(false);
        }
    };

    const startUpgrade = async () => {
        if (!packageInfo) return;
        setUpgradeActionBusy(true);
        setActionError('');
        setRefreshError('');
        setMsg('');
        const request: UpgradeRequest = {
            package_path: packageInfo.package_path,
            expected_version: packageInfo.version,
            allow_same_version: allowSameVersion,
            allow_downgrade: allowDowngrade,
            auto_reboot: autoReboot,
        };
        try {
            const next = await apiStartUpgrade(request);
            setUpgradeInfo(next);
            setMsg('升级任务已提交');
        } catch (err) {
            setActionErrorSeverity('danger');
            setActionError(errorMessage(err, '启动升级失败'));
        } finally {
            setUpgradeActionBusy(false);
        }
    };

    const cancelUpgrade = async () => {
        setUpgradeActionBusy(true);
        setActionError('');
        setRefreshError('');
        setMsg('');
        try {
            const next = await apiCancelUpgrade();
            setUpgradeInfo(next);
            setMsg('升级任务已取消');
        } catch (err) {
            setActionErrorSeverity('danger');
            setActionError(errorMessage(err, '取消升级失败'));
        } finally {
            setUpgradeActionBusy(false);
        }
    };

    const confirmReboot = async () => {
        setUpgradeActionBusy(true);
        setActionError('');
        setRefreshError('');
        setMsg('');
        try {
            const next = await apiConfirmUpgradeReboot();
            setUpgradeInfo(next);
            setMsg('已下发重启应用升级');
        } catch (err) {
            setActionErrorSeverity('danger');
            setActionError(errorMessage(err, '确认重启失败'));
        } finally {
            setUpgradeActionBusy(false);
        }
    };

    return {
        upgradeInfo,
        packageInfo,
        selectedFile,
        allowSameVersion,
        setAllowSameVersion,
        allowDowngrade,
        setAllowDowngrade,
        autoReboot,
        setAutoReboot,
        busy,
        msg,
        actionError,
        actionErrorSeverity,
        refreshError,
        selectFile,
        uploadPackage,
        startUpgrade,
        cancelUpgrade,
        confirmReboot,
    };
}

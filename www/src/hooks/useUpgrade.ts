/**
 * useUpgrade — manages upgrade workflow state:
 * - Polls upgrade status every 2 seconds
 * - Handles file upload, start, cancel, confirm-reboot actions
 */

import { useEffect, useRef, useState } from 'react';
import {
    MAX_UPGRADE_UPLOAD_BYTES,
    getUpgradeInfo,
    uploadUpgradePackage,
    startUpgrade as apiStartUpgrade,
    cancelUpgrade as apiCancelUpgrade,
    confirmUpgradeReboot as apiConfirmUpgradeReboot,
} from '../api/upgrade';
import { ApiClientError } from '../api/client';
import type {
    UpgradePackageInfo,
    UpgradeRequest,
    UpgradeInfo,
} from '../api/types';
import { formatBytes } from '../utils/displayText';

const UPGRADE_STATUS_POLL_INTERVAL_MS = 2000;
const UPGRADE_STATUS_REQUEST_TIMEOUT_MS = 5000;
const STATUS_CANCELED_REASON = 'upgrade-action-started';
const UPGRADE_LOG_PREFIX = '[upgrade]';

export type UpgradeActionErrorSeverity = 'warning' | 'danger';
export type UpgradeActionMessageTone = 'info' | 'success';
export type UpgradeActionErrorScope = 'upload' | 'action';

function errorMessage(error: unknown, fallback: string) {
    if (isFetchUnavailable(error)) {
        return '设备连接中断或服务暂时不可达，请稍后重试。';
    }
    return error instanceof Error ? error.message : fallback;
}

function isFetchUnavailable(error: unknown) {
    if (error instanceof ApiClientError) {
        return (
            (error.status === 0 &&
                error.message.toLowerCase() === 'failed to fetch') ||
            error.code === 'request_timeout' ||
            (error.status === 503 && error.code === 'upgrade_in_progress')
        );
    }
    return (
        error instanceof TypeError &&
        error.message.toLowerCase() === 'failed to fetch'
    );
}

function formatRefreshError(error: unknown) {
    if (isFetchUnavailable(error)) {
        return '升级状态刷新失败：设备正处于重启/服务重启窗口，当前会暂时不可达';
    }
    if (error instanceof ApiClientError) {
        return error.message
            ? `升级状态刷新失败：${error.message}`
            : '升级状态刷新失败';
    }
    return errorMessage(error, '升级状态刷新失败');
}

function makeTraceId(action: string) {
    const randomPart = Math.random().toString(36).slice(2, 8);
    return `${action}-${Date.now().toString(36)}-${randomPart}`;
}

function traceHeaders(traceId: string): HeadersInit {
    return { 'X-Client-Trace-Id': traceId };
}

function logUpgradeAction(
    level: 'info' | 'warn',
    action: string,
    traceId: string,
    detail: Record<string, unknown>,
) {
    console[level](UPGRADE_LOG_PREFIX, {
        action,
        trace_id: traceId,
        ...detail,
    });
}

export function useUpgrade() {
    const [upgradeInfo, setUpgradeInfo] = useState<UpgradeInfo | null>(null);
    const [packageInfo, setPackageInfo] = useState<UpgradePackageInfo | null>(
        null,
    );
    const [selectedFile, setSelectedFile] = useState<File | null>(null);
    const [allowSameVersion, setAllowSameVersion] = useState(true);
    const [allowDowngrade, setAllowDowngrade] = useState(false);
    const [autoReboot, setAutoReboot] = useState(true);
    const [busy, setBusy] = useState(false);
    const [msg, setMsg] = useState('');
    const [msgTone, setMsgTone] =
        useState<UpgradeActionMessageTone>('success');
    const [actionError, setActionError] = useState('');
    const [actionErrorScope, setActionErrorScope] =
        useState<UpgradeActionErrorScope>('action');
    const [actionErrorSeverity, setActionErrorSeverity] =
        useState<UpgradeActionErrorSeverity>('warning');
    const [refreshError, setRefreshError] = useState('');
    const isStatusPollingPaused = useRef(false);
    const latestUpgradeInfoRef = useRef<UpgradeInfo | null>(null);
    const statusAbortRef = useRef<AbortController | null>(null);
    const uploadSeqRef = useRef(0);

    const setUpgradeActionBusy = (value: boolean) => {
        isStatusPollingPaused.current = value;
        if (value) {
            statusAbortRef.current?.abort(STATUS_CANCELED_REASON);
            statusAbortRef.current = null;
            setRefreshError('');
        }
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
            const controller = new AbortController();
            statusAbortRef.current = controller;
            try {
                const nextUpgradeInfo = await getUpgradeInfo({
                    signal: controller.signal,
                    timeoutMs: UPGRADE_STATUS_REQUEST_TIMEOUT_MS,
                });
                if (mounted) {
                    const previousState = latestUpgradeInfoRef.current?.state;
                    setUpgradeInfo(nextUpgradeInfo);
                    latestUpgradeInfoRef.current = nextUpgradeInfo;
                    if (
                        previousState !== nextUpgradeInfo.state &&
                        (nextUpgradeInfo.state === 'completed' ||
                            nextUpgradeInfo.state === 'failed' ||
                            nextUpgradeInfo.state === 'canceled')
                    ) {
                        setPackageInfo(null);
                        setSelectedFile(null);
                    }
                    setRefreshError('');
                }
            } catch (err: unknown) {
                if (
                    mounted &&
                    !isStatusPollingPaused.current &&
                    controller.signal.reason !== STATUS_CANCELED_REASON
                ) {
                    const previousState = latestUpgradeInfoRef.current?.state;
                    if (
                        previousState === 'writing' ||
                        previousState === 'committing' ||
                        previousState === 'waiting_reboot' ||
                        previousState === 'completed'
                    ) {
                        setRefreshError(
                            '升级任务结束后接口可能短暂不可达（通常是重启窗口），请稍后重试。',
                        );
                        return;
                    }
                    setRefreshError(formatRefreshError(err));
                }
            } finally {
                if (statusAbortRef.current === controller) {
                    statusAbortRef.current = null;
                }
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
            statusAbortRef.current?.abort(STATUS_CANCELED_REASON);
            statusAbortRef.current = null;
            window.clearTimeout(timer);
        };
    }, []);

    const selectFile = (file: File | null) => {
        setSelectedFile(file);
        setPackageInfo(null);
        setActionError('');
        setActionErrorScope('action');
        setActionErrorSeverity('warning');
        setMsg('');
        setMsgTone('success');
    };

    const uploadPackage = async (file?: File) => {
        const file_to_upload = file ?? selectedFile;
        if (!file_to_upload) return;
        if (file_to_upload.size > MAX_UPGRADE_UPLOAD_BYTES) {
            setPackageInfo(null);
            setActionErrorSeverity('danger');
            setActionErrorScope('upload');
            setActionError(
                `升级包过大：${formatBytes(file_to_upload.size)}，当前设备最多支持 ${formatBytes(MAX_UPGRADE_UPLOAD_BYTES)}。`,
            );
            return;
        }
        uploadSeqRef.current += 1;
        const uploadSeq = uploadSeqRef.current;
        const traceId = makeTraceId('upload');
        const startedAt = Date.now();
        setUpgradeActionBusy(true);
        setActionError('');
        setActionErrorScope('upload');
        setRefreshError('');
        setMsg('');
        setMsgTone('success');
        logUpgradeAction('info', 'upload', traceId, {
            event: 'begin',
            file_name: file_to_upload.name,
            file_size: file_to_upload.size,
        });
        try {
            const uploaded = await uploadUpgradePackage(file_to_upload, {
                headers: traceHeaders(traceId),
            });
            if (uploadSeq !== uploadSeqRef.current) {
                logUpgradeAction('info', 'upload', traceId, {
                    event: 'ignored',
                    elapsed_ms: Date.now() - startedAt,
                    reason: 'stale_upload_result',
                });
                return;
            }
            setPackageInfo(uploaded);
            logUpgradeAction('info', 'upload', traceId, {
                event: 'ok',
                elapsed_ms: Date.now() - startedAt,
                package_path: uploaded.package_path,
                version: uploaded.version,
                requires_reboot: uploaded.requires_reboot,
            });
        } catch (err) {
            if (uploadSeq !== uploadSeqRef.current) {
                logUpgradeAction('warn', 'upload', traceId, {
                    event: 'ignored',
                    elapsed_ms: Date.now() - startedAt,
                    reason: 'stale_upload_error',
                });
                return;
            }
            setActionErrorSeverity('warning');
            setActionErrorScope('upload');
            const msg =
                err instanceof ApiClientError && err.code === 'request_timeout'
                    ? '上传校验请求超时。设备可能仍在处理大包校验；请稍后查看升级包信息，或重新选择升级包后再试。'
                    : errorMessage(err, '上传失败');
            setActionError(msg);
            logUpgradeAction('warn', 'upload', traceId, {
                event: 'failed',
                elapsed_ms: Date.now() - startedAt,
                error: msg,
            });
        } finally {
            if (uploadSeq === uploadSeqRef.current) {
                setUpgradeActionBusy(false);
            }
        }
    };

    const startUpgrade = async () => {
        if (!packageInfo) return;
        const traceId = makeTraceId('start');
        const startedAt = Date.now();
        setUpgradeActionBusy(true);
        setActionError('');
        setActionErrorScope('action');
        setRefreshError('');
        setMsg('');
        setMsgTone('info');
        const request: UpgradeRequest = {
            package_path: packageInfo.package_path,
            expected_version: packageInfo.version,
            allow_same_version: allowSameVersion,
            allow_downgrade: allowDowngrade,
            auto_reboot: autoReboot,
        };
        logUpgradeAction('info', 'start', traceId, {
            event: 'begin',
            package_path: request.package_path,
            expected_version: request.expected_version,
            allow_same_version: request.allow_same_version,
            allow_downgrade: request.allow_downgrade,
            auto_reboot: request.auto_reboot,
        });
        try {
            const next = await apiStartUpgrade(request, {
                headers: traceHeaders(traceId),
            });
            setUpgradeInfo(next);
            setPackageInfo(null);
            setSelectedFile(null);
            setMsg('升级任务已提交');
            setMsgTone('info');
            logUpgradeAction('info', 'start', traceId, {
                event: 'ok',
                elapsed_ms: Date.now() - startedAt,
                state: next.state,
                stage: next.current_stage,
            });
        } catch (err) {
            if (isFetchUnavailable(err)) {
                setPackageInfo(null);
                setSelectedFile(null);
                setMsg('连接已进入升级窗口，正在等待设备状态恢复');
                setMsgTone('info');
                setRefreshError(
                    '连接已中断，设备可能正在升级或重启，等待状态刷新。',
                );
                logUpgradeAction('info', 'start', traceId, {
                    event: 'upgrade-window',
                    elapsed_ms: Date.now() - startedAt,
                });
                return;
            }
            setActionErrorSeverity('danger');
            setActionErrorScope('action');
            const msg = errorMessage(err, '启动升级失败');
            setActionError(msg);
            logUpgradeAction('warn', 'start', traceId, {
                event: 'failed',
                elapsed_ms: Date.now() - startedAt,
                error: msg,
            });
        } finally {
            setUpgradeActionBusy(false);
        }
    };

    const cancelUpgrade = async () => {
        const traceId = makeTraceId('cancel');
        const startedAt = Date.now();
        setUpgradeActionBusy(true);
        setActionError('');
        setActionErrorScope('action');
        setRefreshError('');
        setMsg('');
        setMsgTone('success');
        logUpgradeAction('info', 'cancel', traceId, { event: 'begin' });
        try {
            const next = await apiCancelUpgrade({
                headers: traceHeaders(traceId),
            });
            setUpgradeInfo(next);
            setMsg('升级任务已取消');
            setMsgTone('success');
            logUpgradeAction('info', 'cancel', traceId, {
                event: 'ok',
                elapsed_ms: Date.now() - startedAt,
                state: next.state,
                stage: next.current_stage,
            });
        } catch (err) {
            setActionErrorSeverity('danger');
            setActionErrorScope('action');
            const msg = errorMessage(err, '取消升级失败');
            setActionError(msg);
            logUpgradeAction('warn', 'cancel', traceId, {
                event: 'failed',
                elapsed_ms: Date.now() - startedAt,
                error: msg,
            });
        } finally {
            setUpgradeActionBusy(false);
        }
    };

    const confirmReboot = async () => {
        const traceId = makeTraceId('confirm-reboot');
        const startedAt = Date.now();
        setUpgradeActionBusy(true);
        setActionError('');
        setActionErrorScope('action');
        setRefreshError('');
        setMsg('');
        setMsgTone('info');
        logUpgradeAction('info', 'confirm-reboot', traceId, {
            event: 'begin',
        });
        try {
            const next = await apiConfirmUpgradeReboot({
                headers: traceHeaders(traceId),
            });
            setUpgradeInfo(next);
            setMsg('已下发重启应用升级');
            setMsgTone('info');
            logUpgradeAction('info', 'confirm-reboot', traceId, {
                event: 'ok',
                elapsed_ms: Date.now() - startedAt,
                state: next.state,
                stage: next.current_stage,
            });
        } catch (err) {
            if (isFetchUnavailable(err)) {
                setMsg('重启已触发，等待设备恢复');
                setMsgTone('info');
                setRefreshError(
                    '设备正处于重启/服务重启窗口，当前会暂时不可达。',
                );
                logUpgradeAction('info', 'confirm-reboot', traceId, {
                    event: 'reboot-window',
                    elapsed_ms: Date.now() - startedAt,
                });
                return;
            }
            setActionErrorSeverity('danger');
            setActionErrorScope('action');
            const msg = errorMessage(err, '确认重启失败');
            setActionError(msg);
            logUpgradeAction('warn', 'confirm-reboot', traceId, {
                event: 'failed',
                elapsed_ms: Date.now() - startedAt,
                error: msg,
            });
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
        msgTone,
        actionError,
        actionErrorScope,
        actionErrorSeverity,
        refreshError,
        selectFile,
        uploadPackage,
        startUpgrade,
        cancelUpgrade,
        confirmReboot,
    };
}

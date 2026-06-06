/**
 * useUpgrade — manages upgrade workflow state:
 * - Polls system status and upgrade status every 2 seconds
 * - Handles file upload, start, cancel, confirm-reboot actions
 */

import { useEffect, useState } from 'react';
import {
  getUpgradeStatus,
  uploadUpgradePackage,
  startUpgrade as apiStartUpgrade,
  cancelUpgrade as apiCancelUpgrade,
  confirmUpgradeReboot as apiConfirmUpgradeReboot,
} from '../api/upgrade';
import { getSystemStatus } from '../api/system';
import type { SystemStatus, UpgradePackageInfo, UpgradeRequest, UpgradeStatus } from '../api/types';

const pollIntervalMs = 2000;
const statusTimeoutMs = 1800;

function errorMessage(error: unknown, fallback: string) {
  return error instanceof Error ? error.message : fallback;
}

export function useUpgrade() {
  const [status, setStatus] = useState<SystemStatus | null>(null);
  const [upgradeStatus, setUpgradeStatus] = useState<UpgradeStatus | null>(null);
  const [packageInfo, setPackageInfo] = useState<UpgradePackageInfo | null>(null);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [allowSameVersion, setAllowSameVersion] = useState(false);
  const [allowDowngrade, setAllowDowngrade] = useState(false);
  const [autoReboot, setAutoReboot] = useState(false);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');

  useEffect(() => {
    let mounted = true;
    let timer = 0;
    const load = async () => {
      const startedAt = Date.now();
      try {
        const [nextStatus, nextUpgradeStatus] = await Promise.all([
          getSystemStatus({ timeoutMs: statusTimeoutMs }),
          getUpgradeStatus({ timeoutMs: statusTimeoutMs }),
        ]);
        if (mounted) {
          setStatus(nextStatus);
          setUpgradeStatus(nextUpgradeStatus);
          setError('');
        }
      } catch (err: unknown) {
        if (mounted) {
          setError(errorMessage(err, '状态刷新失败'));
        }
      } finally {
        if (mounted) {
          const elapsedMs = Date.now() - startedAt;
          timer = window.setTimeout(
            load,
            Math.max(0, pollIntervalMs - elapsedMs),
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
    setError('');
    setMessage('');
  };

  const uploadPackage = async () => {
    if (!selectedFile) return;
    setBusy(true);
    setError('');
    setMessage('');
    try {
      const uploaded = await uploadUpgradePackage(selectedFile);
      setPackageInfo(uploaded);
      setMessage(`已上传 ${selectedFile.name}`);
    } catch (err) {
      setError(errorMessage(err, '上传失败'));
    } finally {
      setBusy(false);
    }
  };

  const startUpgrade = async () => {
    if (!packageInfo) return;
    setBusy(true);
    setError('');
    setMessage('');
    const request: UpgradeRequest = {
      package_path: packageInfo.package_path,
      expected_version: packageInfo.version,
      allow_same_version: allowSameVersion,
      allow_downgrade: allowDowngrade,
      auto_reboot: autoReboot,
    };
    try {
      const next = await apiStartUpgrade(request);
      setUpgradeStatus(next);
      setMessage('升级任务已提交');
    } catch (err) {
      setError(errorMessage(err, '启动升级失败'));
    } finally {
      setBusy(false);
    }
  };

  const cancelUpgrade = async () => {
    setBusy(true);
    setError('');
    setMessage('');
    try {
      const next = await apiCancelUpgrade();
      setUpgradeStatus(next);
      setMessage('升级任务已取消');
    } catch (err) {
      setError(errorMessage(err, '取消升级失败'));
    } finally {
      setBusy(false);
    }
  };

  const confirmReboot = async () => {
    setBusy(true);
    setError('');
    setMessage('');
    try {
      const next = await apiConfirmUpgradeReboot();
      setUpgradeStatus(next);
      setMessage('已下发重启应用升级');
    } catch (err) {
      setError(errorMessage(err, '确认重启失败'));
    } finally {
      setBusy(false);
    }
  };

  return {
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
  };
}

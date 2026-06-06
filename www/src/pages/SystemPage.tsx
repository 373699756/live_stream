import { useUpgrade } from '../hooks/useUpgrade';
import {
  DeviceInfoPanel,
  ModuleStatusPanel,
  SystemStatusPanel,
} from './SystemStatusPanels';
import { UpgradePanel } from './UpgradePanel';

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
      <SystemStatusPanel status={status} />
      <DeviceInfoPanel status={status} />
      <UpgradePanel
        allowDowngrade={allowDowngrade}
        allowSameVersion={allowSameVersion}
        autoReboot={autoReboot}
        busy={busy}
        cancelUpgrade={cancelUpgrade}
        confirmReboot={confirmReboot}
        error={error}
        message={message}
        packageInfo={packageInfo}
        selectedFile={selectedFile}
        selectFile={selectFile}
        setAllowDowngrade={setAllowDowngrade}
        setAllowSameVersion={setAllowSameVersion}
        setAutoReboot={setAutoReboot}
        startUpgrade={startUpgrade}
        upgradeStatus={upgradeStatus}
        uploadPackage={uploadPackage}
      />
      <ModuleStatusPanel status={status} />
    </div>
  );
}

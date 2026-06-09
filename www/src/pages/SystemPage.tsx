import { useState } from 'react';
import { useUpgrade } from '../hooks/useUpgrade';
import {
  DeviceInfoPanel,
  ModuleStatusPanel,
  SystemStatusPanel,
} from './SystemStatusPanels';
import { UpgradePanel } from './UpgradePanel';

type SystemMaintenanceTab = 'overview' | 'modules' | 'upgrade';

const systemMaintenanceTabs: Array<{
  key: SystemMaintenanceTab;
  label: string;
}> = [
  { key: 'overview', label: '系统概览' },
  { key: 'modules', label: '模块状态' },
  { key: 'upgrade', label: '固件升级' },
];

export function SystemPage() {
  const [activeTab, setActiveTab] =
    useState<SystemMaintenanceTab>('overview');
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
    return (
      <div className="panel">
        {error ? `系统维护状态加载失败：${error}` : '加载系统维护状态...'}
      </div>
    );
  }

  return (
    <div className="page-grid system-maintenance-page">
      <div className="page-heading system-maintenance-heading">
        <div>
          <h2>系统维护</h2>
          <p>设备资源、运行模块和固件升级维护</p>
        </div>
      </div>

      <div className="tabs system-maintenance-tabs">
        {systemMaintenanceTabs.map((tab) => (
          <button
            key={tab.key}
            type="button"
            className={activeTab === tab.key ? 'active' : ''}
            onClick={() => setActiveTab(tab.key)}
          >
            {tab.label}
          </button>
        ))}
      </div>

      {activeTab === 'overview' ? (
        <div className="page-grid system-overview-grid">
          <SystemStatusPanel status={status} />
          <DeviceInfoPanel status={status} />
        </div>
      ) : null}

      {activeTab === 'modules' ? <ModuleStatusPanel status={status} /> : null}

      {activeTab === 'upgrade' ? (
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
      ) : null}
    </div>
  );
}

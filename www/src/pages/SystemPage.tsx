import { useState } from 'react';
import { useSystemStatus } from '../hooks/useSystemStatus';
import { useTimeConfig } from '../hooks/useTimeConfig';
import { useUpgrade } from '../hooks/useUpgrade';
import {
  DeviceInfoPanel,
  ModuleStatusPanel,
  SystemStatusPanel,
} from './SystemStatusPanels';
import { TimeConfigPanel } from './TimeConfigPanel';
import { UpgradePanel } from './UpgradePanel';

type SystemMaintenanceTab = 'overview' | 'modules' | 'time' | 'upgrade';

const systemMaintenanceTabs: Array<{
  key: SystemMaintenanceTab;
  label: string;
}> = [
  { key: 'overview', label: '系统概览' },
  { key: 'modules', label: '模块状态' },
  { key: 'time', label: '时间同步' },
  { key: 'upgrade', label: '固件升级' },
];

export function SystemPage() {
  const [activeTab, setActiveTab] =
    useState<SystemMaintenanceTab>('overview');
  const {
    status,
    refreshError: systemRefreshError,
  } = useSystemStatus();
  const timeConfig = useTimeConfig();
  const {
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
    actionError,
    refreshError: upgradeRefreshError,
    selectFile,
    uploadPackage,
    startUpgrade,
    cancelUpgrade,
    confirmReboot,
  } = useUpgrade();

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
        status ? (
          <div className="page-grid system-overview-grid">
            <SystemStatusPanel status={status} />
            <DeviceInfoPanel status={status} />
          </div>
        ) : (
          <div className="panel">
            {systemRefreshError
              ? `系统状态加载失败：${systemRefreshError}`
              : '加载系统状态...'}
          </div>
        )
      ) : null}

      {activeTab === 'modules' ? (
        status ? (
          <ModuleStatusPanel status={status} />
        ) : (
          <div className="panel">
            {systemRefreshError
              ? `模块状态加载失败：${systemRefreshError}`
              : '加载模块状态...'}
          </div>
        )
      ) : null}

      {activeTab === 'time' ? (
        timeConfig.status ? (
          <TimeConfigPanel
            browserSyncOnLogin={timeConfig.browserSyncOnLogin}
            busy={timeConfig.busy}
            error={timeConfig.error}
            manualSyncAllowed={timeConfig.manualSyncAllowed}
            message={timeConfig.message}
            ntpEnabled={timeConfig.ntpEnabled}
            ntpIntervalSec={timeConfig.ntpIntervalSec}
            ntpServersText={timeConfig.ntpServersText}
            saveConfig={timeConfig.saveConfig}
            setBrowserSyncOnLogin={timeConfig.setBrowserSyncOnLogin}
            setManualSyncAllowed={timeConfig.setManualSyncAllowed}
            setNtpEnabled={timeConfig.setNtpEnabled}
            setNtpIntervalSec={timeConfig.setNtpIntervalSec}
            setNtpServersText={timeConfig.setNtpServersText}
            setTimezone={timeConfig.setTimezone}
            status={timeConfig.status}
            syncBrowserNow={timeConfig.syncBrowserNow}
            syncNtp={timeConfig.syncNtp}
            timezone={timeConfig.timezone}
          />
        ) : (
          <div className="panel">
            {timeConfig.error
              ? `时间状态加载失败：${timeConfig.error}`
              : '加载时间状态...'}
          </div>
        )
      ) : null}

      {activeTab === 'upgrade' ? (
        upgradeStatus ? (
          <UpgradePanel
            actionError={actionError}
            allowDowngrade={allowDowngrade}
            allowSameVersion={allowSameVersion}
            autoReboot={autoReboot}
            busy={busy}
            cancelUpgrade={cancelUpgrade}
            confirmReboot={confirmReboot}
            message={message}
            packageInfo={packageInfo}
            refreshError={upgradeRefreshError}
            selectedFile={selectedFile}
            selectFile={selectFile}
            setAllowDowngrade={setAllowDowngrade}
            setAllowSameVersion={setAllowSameVersion}
            setAutoReboot={setAutoReboot}
            startUpgrade={startUpgrade}
            upgradeStatus={upgradeStatus}
            uploadPackage={uploadPackage}
          />
        ) : (
          <div className="panel">
            {upgradeRefreshError
              ? `升级状态加载失败：${upgradeRefreshError}`
              : '加载升级状态...'}
          </div>
        )
      ) : null}
    </div>
  );
}

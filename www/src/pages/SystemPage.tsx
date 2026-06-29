import { useState } from 'react';
import { useSystemInfo } from '../hooks/useSystemInfo';
import { useTimeConfig } from '../hooks/useTimeConfig';
import { useUpgrade } from '../hooks/useUpgrade';
import {
    DeviceInfoPanel,
    ModuleStatusPanel,
    SystemInfoPanel,
} from './SystemInfoPanels';
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
    const { systemInfo, refreshError: systemRefreshError } = useSystemInfo();
    const {
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
                systemInfo ? (
                    <div className="page-grid system-overview-grid">
                        <SystemInfoPanel systemInfo={systemInfo} />
                        <DeviceInfoPanel systemInfo={systemInfo} />
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
                systemInfo ? (
                    <ModuleStatusPanel systemInfo={systemInfo} />
                ) : (
                    <div className="panel">
                        {systemRefreshError
                            ? `模块状态加载失败：${systemRefreshError}`
                            : '加载模块状态...'}
                    </div>
                )
            ) : null}

            {activeTab === 'time' ? <TimeMaintenancePanel /> : null}

            {activeTab === 'upgrade' ? (
                upgradeInfo ? (
                    <UpgradePanel
                        actionError={actionError}
                        actionErrorSeverity={actionErrorSeverity}
                        allowDowngrade={allowDowngrade}
                        allowSameVersion={allowSameVersion}
                        autoReboot={autoReboot}
                        busy={busy}
                        cancelUpgrade={cancelUpgrade}
                        confirmReboot={confirmReboot}
                        msg={msg}
                        packageInfo={packageInfo}
                        refreshError={upgradeRefreshError}
                        selectedFile={selectedFile}
                        selectFile={selectFile}
                        setAllowDowngrade={setAllowDowngrade}
                        setAllowSameVersion={setAllowSameVersion}
                        setAutoReboot={setAutoReboot}
                        startUpgrade={startUpgrade}
                        upgradeInfo={upgradeInfo}
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

function TimeMaintenancePanel() {
    const timeConfig = useTimeConfig();

    if (!timeConfig.timeInfo) {
        return (
            <div className="panel">
                {timeConfig.error
                    ? `时间状态加载失败：${timeConfig.error}`
                    : '加载时间状态...'}
            </div>
        );
    }

    return (
        <TimeConfigPanel
            browserSyncOnLogin={timeConfig.browserSyncOnLogin}
            busy={timeConfig.busy}
            error={timeConfig.error}
            manualSyncAllowed={timeConfig.manualSyncAllowed}
            msg={timeConfig.msg}
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
            timeInfo={timeConfig.timeInfo}
            syncBrowserNow={timeConfig.syncBrowserNow}
            syncNtp={timeConfig.syncNtp}
            timezone={timeConfig.timezone}
        />
    );
}

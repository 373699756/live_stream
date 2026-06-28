import type { TimeInfo } from '../api/types';
import { NtpSettingsPanel } from './NtpSettingsPanel';
import { TimeGeneralSettings } from './TimeGeneralSettings';
import { TimeStatusPanel } from './TimeStatusPanel';

interface TimeConfigPanelProps {
    browserSyncOnLogin: boolean;
    busy: boolean;
    error: string;
    manualSyncAllowed: boolean;
    msg: string;
    ntpEnabled: boolean;
    ntpIntervalSec: number;
    ntpServersText: string;
    saveConfig: () => void;
    setBrowserSyncOnLogin: (value: boolean) => void;
    setManualSyncAllowed: (value: boolean) => void;
    setNtpEnabled: (value: boolean) => void;
    setNtpIntervalSec: (value: number) => void;
    setNtpServersText: (value: string) => void;
    setTimezone: (value: string) => void;
    timeInfo: TimeInfo;
    syncBrowserNow: () => void;
    syncNtp: () => void;
    timezone: string;
}

export function TimeConfigPanel({
    browserSyncOnLogin,
    busy,
    error,
    manualSyncAllowed,
    msg,
    ntpEnabled,
    ntpIntervalSec,
    ntpServersText,
    saveConfig,
    setBrowserSyncOnLogin,
    setManualSyncAllowed,
    setNtpEnabled,
    setNtpIntervalSec,
    setNtpServersText,
    setTimezone,
    timeInfo,
    syncBrowserNow,
    syncNtp,
    timezone,
}: TimeConfigPanelProps) {
    return (
        <div className="page-grid time-config-grid">
            <TimeStatusPanel
                busy={busy}
                error={error}
                manualSyncAllowed={manualSyncAllowed}
                msg={msg}
                saveConfig={saveConfig}
                syncBrowserNow={syncBrowserNow}
                timeInfo={timeInfo}
            />

            <TimeGeneralSettings
                browserSyncOnLogin={browserSyncOnLogin}
                manualSyncAllowed={manualSyncAllowed}
                setBrowserSyncOnLogin={setBrowserSyncOnLogin}
                setManualSyncAllowed={setManualSyncAllowed}
                setTimezone={setTimezone}
                timezone={timezone}
            />

            <NtpSettingsPanel
                busy={busy}
                ntpEnabled={ntpEnabled}
                ntpIntervalSec={ntpIntervalSec}
                ntpServersText={ntpServersText}
                setNtpEnabled={setNtpEnabled}
                setNtpIntervalSec={setNtpIntervalSec}
                setNtpServersText={setNtpServersText}
                syncNtp={syncNtp}
            />
        </div>
    );
}

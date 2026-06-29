import type { UpgradePackageInfo, UpgradeInfo } from '../api/types';
import type { UpgradeActionErrorSeverity } from '../hooks/useUpgrade';
import { UpgradePackagePanel } from './UpgradePackagePanel';
import { UpgradeStatusPanel } from './UpgradeStatusPanel';

interface UpgradePanelProps {
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

export function UpgradePanel(props: UpgradePanelProps) {
    return (
        <section className="panel wide-panel upgrade-panel">
            <div className="page-heading">
                <div>
                    <h2>固件升级</h2>
                    <p>
                        Web 入口只允许升级 Web Console 分区；写入阶段请勿断电。
                    </p>
                </div>
            </div>

            <div className="upgrade-grid">
                <UpgradePackagePanel {...props} />
                <UpgradeStatusPanel upgradeInfo={props.upgradeInfo} />
            </div>
        </section>
    );
}

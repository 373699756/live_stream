import type { UpgradeInfo } from '../api/types';
import { formatTimestamp } from '../utils/displayText';
import {
    buildUpgradeDisplayInfo,
    formatUpgradeStageDetail,
    type UpgradeNoteTone,
} from './upgradeDisplay';

interface UpgradeStatusPanelProps {
    upgradeInfo: UpgradeInfo;
    refreshError: string;
}

function noteClassName(tone: UpgradeNoteTone) {
    if (tone === 'success') {
        return 'success-note';
    }
    if (tone === 'error') {
        return 'error-note';
    }
    if (tone === 'warning') {
        return 'warning-note';
    }
    if (tone === 'info') {
        return 'info-note';
    }
    return 'neutral-note';
}

function statusDetailLabel(status: UpgradeInfo) {
    if (status.state === 'failed') {
        return '错误';
    }
    if (status.state === 'canceled') {
        return '结果';
    }
    return '阶段详情';
}

export function UpgradeStatusPanel({
    refreshError,
    upgradeInfo,
}: UpgradeStatusPanelProps) {
    const display = buildUpgradeDisplayInfo(upgradeInfo);
    const statusDetail = upgradeInfo.error_message ||
        formatUpgradeStageDetail(upgradeInfo);

    return (
        <div className="upgrade-section upgrade-status-section">
            <div className="panel-title">当前升级状态</div>
            <div className={`upgrade-state-banner state-${display.state}`}>
                <div>
                    <span>当前状态</span>
                    <strong>{display.stateLabel}</strong>
                </div>
                <div>
                    <span>当前阶段</span>
                    <strong>{display.stageLabel}</strong>
                </div>
            </div>

            {display.note ? (
                <div
                    className={`status-note upgrade-risk-note ${noteClassName(
                        display.tone,
                    )}`}
                >
                    {display.note}
                </div>
            ) : null}
            {refreshError ? (
                <div className="status-note upgrade-risk-note info-note">
                    {refreshError}
                </div>
            ) : null}

            <div className={`upgrade-progress state-${display.state}`}>
                <div className="upgrade-progress-header">
                    <strong>进度</strong>
                    <span>{upgradeInfo.progress_percent}%</span>
                </div>
                <div className="progress-track">
                    <div
                        className={`progress-fill tone-${display.progressTone}`}
                        style={{ width: `${upgradeInfo.progress_percent}%` }}
                    />
                </div>
            </div>

            <div className="upgrade-status-grid">
                <div>
                    <span>目标版本</span>
                    <strong>{upgradeInfo.target_version || '-'}</strong>
                </div>
                <div>
                    <span>状态码</span>
                    <strong>{upgradeInfo.state}</strong>
                </div>
                <div>
                    <span>开始时间</span>
                    <strong>{formatTimestamp(upgradeInfo.started_at_ms)}</strong>
                </div>
                <div>
                    <span>结束时间</span>
                    <strong>
                        {formatTimestamp(upgradeInfo.finished_at_ms)}
                    </strong>
                </div>
                <div className="wide-status-cell">
                    <span>{statusDetailLabel(upgradeInfo)}</span>
                    <strong>{statusDetail}</strong>
                </div>
            </div>
        </div>
    );
}

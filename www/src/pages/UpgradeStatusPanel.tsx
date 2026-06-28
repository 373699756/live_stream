import type { UpgradeInfo } from '../api/types';
import { formatTimestamp } from '../utils/displayText';

interface UpgradeStatusPanelProps {
    upgradeInfo: UpgradeInfo;
}

const upgradeStateLabels: Record<UpgradeInfo['state'], string> = {
    idle: '空闲',
    validating: '校验中',
    preparing: '准备写入',
    writing: '写入中',
    committing: '提交中',
    waiting_reboot: '等待重启',
    completed: '已完成',
    failed: '失败',
    canceled: '已取消',
};

function upgradeStageText(status: UpgradeInfo) {
    if (status.current_stage) {
        return status.current_stage;
    }
    return upgradeStateLabels[status.state];
}

function stageRiskNote(status: UpgradeInfo) {
    if (status.state === 'failed') {
        return (
            status.error_message || '升级失败，请查看 /data/log/upgrade.log。'
        );
    }
    if (status.state === 'writing') {
        return '正在擦写 Flash，请保持供电和网络稳定，不要刷新页面或断电。';
    }
    if (status.state === 'committing' || status.state === 'waiting_reboot') {
        return '升级已进入提交阶段，不可取消；如页面异常，先查看当前状态和升级日志。';
    }
    if (status.state === 'validating' || status.state === 'preparing') {
        return '正在校验或准备升级包，发现包格式、签名或版本策略问题会在这里提示。';
    }
    return '';
}

export function UpgradeStatusPanel({ upgradeInfo }: UpgradeStatusPanelProps) {
    const riskNote = stageRiskNote(upgradeInfo);

    return (
        <div className="upgrade-section upgrade-status-section">
            <div className="panel-title">当前升级状态</div>
            <div className={`upgrade-state-banner state-${upgradeInfo.state}`}>
                <div>
                    <span>当前状态</span>
                    <strong>{upgradeStateLabels[upgradeInfo.state]}</strong>
                </div>
                <div>
                    <span>当前阶段</span>
                    <strong>{upgradeStageText(upgradeInfo)}</strong>
                </div>
            </div>

            {riskNote ? (
                <div
                    className={`status-note upgrade-risk-note ${
                        upgradeInfo.state === 'failed'
                            ? 'error-note'
                            : 'warning-note'
                    }`}
                >
                    {riskNote}
                </div>
            ) : null}

            <div className="upgrade-progress">
                <div className="upgrade-progress-header">
                    <strong>进度</strong>
                    <span>{upgradeInfo.progress_percent}%</span>
                </div>
                <div className="progress-track">
                    <div
                        className="progress-fill"
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
                    <span>错误</span>
                    <strong>{upgradeInfo.error_message || '-'}</strong>
                </div>
            </div>
        </div>
    );
}

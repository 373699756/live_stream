import { aiAlertImageUrl } from '../../api/ai';
import type {
    AiAlertRecord,
    AiCapabilities,
    AiStats,
    AlarmInfoResponse,
} from '../../api/types';
import { formatTimestamp } from '../../utils/displayText';
import { latestAlarmTimeText } from './aiAlertFormat';
import { isAiTaskAvailable, taskLabel } from './aiAlertTasks';
import { maxConfidence, numberText, streamLabel } from './aiConfigDraft';

interface EnabledTaskSummary {
    backendLabel: string;
    enabledTaskTotal: number;
}

interface AiSnapshotRailProps {
    alerts: AiAlertRecord[];
    alarmInfo: AlarmInfoResponse | null;
    capabilities: AiCapabilities | null;
    lastAlarmEvent: Parameters<typeof latestAlarmTimeText>[1];
    summary: AiStats;
    supportedTaskSummary: EnabledTaskSummary;
}

export function AiSnapshotRail({
    alerts,
    alarmInfo,
    capabilities,
    lastAlarmEvent,
    summary,
    supportedTaskSummary,
}: AiSnapshotRailProps) {
    const latestAlerts = alerts
        .filter((alert) => isAiTaskAvailable(alert.task, capabilities))
        .slice()
        .sort((left, right) => right.timestamp_ms - left.timestamp_ms)
        .slice(0, 10);
    const snapshotSlots = Array.from({ length: 10 }, (_, index) => ({
        alert: latestAlerts[index],
        index,
    }));

    return (
        <aside className="ai-snapshot-rail" aria-label="AI 实时抓图">
            <div className="ai-snapshot-rail-header">
                <div>
                    <h3>实时抓图</h3>
                    <span>最新 {latestAlerts.length}/10</span>
                </div>
                <div className="ai-status-kpis">
                    <div>
                        <span>AI 事件</span>
                        <strong>
                            {supportedTaskSummary.enabledTaskTotal} 启用
                        </strong>
                    </div>
                    <div>
                        <span>后端</span>
                        <strong>{supportedTaskSummary.backendLabel}</strong>
                    </div>
                    <div>
                        <span>有效结果</span>
                        <strong>{numberText(summary.active_results)}</strong>
                    </div>
                    <div>
                        <span>最近耗时</span>
                        <strong>
                            {numberText(summary.last_inference_time_ms)} ms
                        </strong>
                    </div>
                    <div>
                        <span>最近报警</span>
                        <strong>
                            {latestAlarmTimeText(alarmInfo, lastAlarmEvent)}
                        </strong>
                    </div>
                </div>
            </div>
            <div className="ai-snapshot-list">
                {snapshotSlots.map(({ alert, index }) => {
                    if (!alert) {
                        return (
                            <div
                                className="ai-snapshot-card ai-snapshot-card-empty"
                                key={`empty-${index}`}
                            >
                                <div className="ai-snapshot-empty-frame" />
                                <span className="ai-snapshot-card-body">
                                    <strong>等待抓图</strong>
                                    <em>Slot {index + 1}</em>
                                    <span>--</span>
                                    <span>--</span>
                                    <span>--</span>
                                </span>
                            </div>
                        );
                    }
                    const imageUrl = aiAlertImageUrl(
                        alert.image_url,
                        alert.timestamp_ms,
                    );
                    return (
                        <button
                            type="button"
                            className="ai-snapshot-card"
                            key={alert.id}
                            onClick={() =>
                                window.open(
                                    imageUrl,
                                    '_blank',
                                    'noopener,noreferrer',
                                )
                            }
                        >
                            <img
                                alt={`${taskLabel(alert.task)} ${alert.id}`}
                                src={imageUrl}
                            />
                            <span className="ai-snapshot-card-body">
                                <strong>{taskLabel(alert.task)}</strong>
                                <em>{formatTimestamp(alert.timestamp_ms)}</em>
                                <span>{streamLabel(alert.stream)}</span>
                                <span>{alert.detected_targets} 个目标</span>
                                <span>{maxConfidence(alert)}</span>
                            </span>
                        </button>
                    );
                })}
            </div>
        </aside>
    );
}

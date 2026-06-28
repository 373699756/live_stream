import { aiAlertImageUrl } from '../../api/ai';
import type { AiAlertRecord, AiCapabilities } from '../../api/types';
import { formatTimestamp } from '../../utils/format';
import { isAiTaskAvailable, taskLabel } from './aiAlertTasks';
import { maxConfidence, streamLabel } from './aiConfigDraft';

interface AiSnapshotRailProps {
    alerts: AiAlertRecord[];
    capabilities: AiCapabilities | null;
}

export function AiSnapshotRail({
    alerts,
    capabilities,
}: AiSnapshotRailProps) {
    const latestAlerts = alerts
        .filter((alert) => isAiTaskAvailable(alert.task, capabilities))
        .slice()
        .sort((left, right) => right.timestamp_ms - left.timestamp_ms)
        .slice(0, 10);

    return (
        <aside className="ai-snapshot-rail" aria-label="AI 实时抓图">
            <div className="ai-snapshot-rail-header">
                <div>
                    <h3>实时抓图</h3>
                    <span>最新 {latestAlerts.length}/10</span>
                </div>
            </div>
            <div className="ai-snapshot-list">
                {latestAlerts.length === 0 ? (
                    <div className="ai-snapshot-empty">暂无抓图</div>
                ) : (
                    latestAlerts.map((alert) => {
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
                                    <em>
                                        {formatTimestamp(alert.timestamp_ms)}
                                    </em>
                                    <span>{streamLabel(alert.stream)}</span>
                                    <span>{alert.detected_targets} 个目标</span>
                                    <span>{maxConfidence(alert)}</span>
                                </span>
                            </button>
                        );
                    })
                )}
            </div>
        </aside>
    );
}

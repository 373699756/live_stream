import type {
    AiStatus,
    AlarmConfig,
    AlarmStatusResponse,
} from '../../api/types';
import type { MediaEvent } from '../../api/mediaEvents';
import { StatusBadge } from '../../components/StatusBadge';
import { AiMetricsPanel } from './AiMetricsPanel';
import {
    alarmSourceLabel,
    formatPercent,
    latestAlarmTimeText,
    streamLabel,
} from './aiAlertFormat';
import {
    alarmBadgeLabel,
    alarmBadgeState,
    backendBadgeState,
    backendLabel,
} from './aiAlertBadges';
import {
    isAiTaskAvailable,
    taskLabel,
    taskUnavailableText,
} from './aiAlertTasks';

interface AiStatusSummaryProps {
    status: AiStatus | null;
    alarmConfig: AlarmConfig | null;
    alarmStatus: AlarmStatusResponse | null;
    lastAlarmEvent: MediaEvent | null;
}

export function AiStatusSummary({
    status,
    alarmConfig,
    alarmStatus,
    lastAlarmEvent,
}: AiStatusSummaryProps) {
    if (!status) {
        return (
            <section className="panel wide-panel">
                <div className="empty-state">加载 AI 状态...</div>
            </section>
        );
    }

    const availableTasks = status.tasks.filter((item) =>
        isAiTaskAvailable(item.config.task),
    );
    const primaryTask = availableTasks[0] ?? status.tasks[0];
    const primaryConfig = primaryTask?.config ?? status.config.tasks[0];
    const unsupportedTaskCount = status.config.tasks.filter(
        (task) => !isAiTaskAvailable(task.task),
    ).length;
    const alarmMessage =
        lastAlarmEvent?.message || alarmStatus?.status.message || '--';
    const alarmSource =
        lastAlarmEvent?.target ||
        (alarmStatus?.status.active ? alarmStatus.status.source : '');

    return (
        <section className="panel wide-panel">
            <div className="ai-status-header">
                <div>
                    <h2>AI 状态</h2>
                    <p>
                        {primaryConfig
                            ? `${primaryConfig.backend} / ${taskLabel(primaryConfig.task)}`
                            : '未配置任务'}
                    </p>
                </div>
                <div className="ai-status-badges">
                    <StatusBadge
                        state={backendBadgeState(status)}
                        label={backendLabel(status)}
                    />
                    <StatusBadge
                        state={alarmBadgeState(status, alarmConfig)}
                        label={alarmBadgeLabel(status, alarmConfig)}
                    />
                </div>
            </div>
            <AiMetricsPanel stats={status.summary} />
            <div className="ai-status-detail-row">
                <span>
                    可用任务 <strong>{availableTasks.length}</strong>
                </span>
                <span>
                    事件源{' '}
                    <strong>
                        {primaryConfig
                            ? streamLabel(primaryConfig.stream)
                            : '--'}
                    </strong>
                </span>
                <span>
                    阈值{' '}
                    <strong>
                        {formatPercent(
                            primaryConfig?.confidence_threshold ?? 0,
                        )}
                    </strong>
                </span>
                <span>
                    间隔{' '}
                    <strong>
                        {primaryConfig?.inference_interval_ms ?? 0} ms
                    </strong>
                </span>
                <span>
                    未内置 <strong>{unsupportedTaskCount}</strong>
                </span>
                <span>
                    最近报警{' '}
                    <strong>
                        {latestAlarmTimeText(alarmStatus, lastAlarmEvent)}
                    </strong>
                </span>
                <span>
                    报警源 <strong>{alarmSourceLabel(alarmSource)}</strong>
                </span>
            </div>
            {unsupportedTaskCount > 0 ? (
                <div className="status-note warning-note ai-status-warning">
                    目标检测和周界检测{taskUnavailableText('object_detection')}，
                    相关配置不会启用。
                </div>
            ) : null}
            {status.summary.alarm_linked &&
            alarmConfig &&
            !alarmConfig.ai_detection.enabled ? (
                <div className="status-note warning-note ai-status-warning">
                    AI 可以生成告警抓拍，但系统报警事件不会触发，因为 AI
                    告警联动规则未启用。
                </div>
            ) : null}
            {alarmStatus?.status.active || lastAlarmEvent ? (
                <div className="status-note success-note ai-status-warning">
                    系统报警事件已触发：{alarmMessage}
                </div>
            ) : null}
        </section>
    );
}

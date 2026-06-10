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
} from './aiAlertStatus';
import { taskLabel } from './aiAlertTasks';

interface AiRuntimeSummaryProps {
  status: AiStatus | null;
  alarmConfig: AlarmConfig | null;
  alarmStatus: AlarmStatusResponse | null;
  lastAlarmEvent: MediaEvent | null;
}

export function AiRuntimeSummary({
  status,
  alarmConfig,
  alarmStatus,
  lastAlarmEvent,
}: AiRuntimeSummaryProps) {
  if (!status) {
    return (
      <section className="panel wide-panel">
        <div className="empty-state">加载 AI 状态...</div>
      </section>
    );
  }

  const primaryTask =
    status.tasks.find((item) => item.config.task === 'perimeter_detection') ??
    status.tasks[0];
  const primaryConfig = primaryTask?.config ?? status.config.tasks[0];
  const regionText =
    (primaryConfig?.perimeter_regions.length ?? 0) > 0
      ? `${primaryConfig?.perimeter_regions.length ?? 0} 个区域`
      : '整幅画面';
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
      <div className="ai-runtime-detail-row">
        <span>
          并行任务 <strong>{status.config.tasks.length}</strong>
        </span>
        <span>
          事件源 <strong>{primaryConfig ? streamLabel(primaryConfig.stream) : '--'}</strong>
        </span>
        <span>
          阈值 <strong>{formatPercent(primaryConfig?.confidence_threshold ?? 0)}</strong>
        </span>
        <span>
          间隔 <strong>{primaryConfig?.inference_interval_ms ?? 0} ms</strong>
        </span>
        <span>
          周界 <strong>{regionText}</strong>
        </span>
        <span>
          最近报警{' '}
          <strong>{latestAlarmTimeText(alarmStatus, lastAlarmEvent)}</strong>
        </span>
        <span>
          报警源 <strong>{alarmSourceLabel(alarmSource)}</strong>
        </span>
      </div>
      {status.summary.alarm_linked && alarmConfig && !alarmConfig.ai_detection.enabled ? (
        <div className="status-note warning-note ai-runtime-warning">
          AI 可以生成告警抓拍，但系统报警事件不会触发，因为 AI 告警联动规则未启用。
        </div>
      ) : null}
      {alarmStatus?.status.active || lastAlarmEvent ? (
        <div className="status-note success-note ai-runtime-warning">
          系统报警事件已触发：{alarmMessage}
        </div>
      ) : null}
    </section>
  );
}

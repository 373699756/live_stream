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

  const regionText =
    status.config.perimeter_regions.length > 0
      ? `${status.config.perimeter_regions.length} 个区域`
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
            {status.config.backend} / {taskLabel(status.config.task)}
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
      <AiMetricsPanel stats={status.stats} />
      <div className="ai-runtime-detail-row">
        <span>
          当前任务 <strong>{taskLabel(status.config.task)}</strong>
        </span>
        <span>
          事件源 <strong>{streamLabel(status.config.stream)}</strong>
        </span>
        <span>
          阈值 <strong>{formatPercent(status.config.confidence_threshold)}</strong>
        </span>
        <span>
          间隔 <strong>{status.config.inference_interval_ms} ms</strong>
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
      {status.stats.alarm_linked && alarmConfig && !alarmConfig.ai_detection.enabled ? (
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

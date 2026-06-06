import type { AiStatus } from '../api/types';

function metricValue(value: number) {
  return Number.isFinite(value) ? String(value) : '-';
}

function timeSince(timestampMs: number) {
  if (timestampMs <= 0) {
    return '-';
  }
  const deltaSeconds = Math.max(0, Math.round((Date.now() - timestampMs) / 1000));
  if (deltaSeconds < 60) {
    return `${deltaSeconds}s`;
  }
  const deltaMinutes = Math.round(deltaSeconds / 60);
  if (deltaMinutes < 60) {
    return `${deltaMinutes}m`;
  }
  return `${Math.round(deltaMinutes / 60)}h`;
}

interface AiMetricsPanelProps {
  stats: AiStatus['stats'];
}

export function AiMetricsPanel({ stats }: AiMetricsPanelProps) {
  return (
    <div className="metric-grid ai-metric-grid">
      <div>
        <span>推理次数</span>
        <strong>{metricValue(stats.inference_count)}</strong>
      </div>
      <div>
        <span>失败次数</span>
        <strong>{metricValue(stats.inference_failed_count)}</strong>
      </div>
      <div>
        <span>有效结果</span>
        <strong>{metricValue(stats.active_results)}</strong>
      </div>
      <div>
        <span>最近耗时</span>
        <strong>{metricValue(stats.last_inference_time_ms)} ms</strong>
      </div>
      <div>
        <span>平均耗时</span>
        <strong>{metricValue(stats.average_inference_time_ms)} ms</strong>
      </div>
      <div>
        <span>最大耗时</span>
        <strong>{metricValue(stats.max_inference_time_ms)} ms</strong>
      </div>
      <div>
        <span>丢弃告警</span>
        <strong>{metricValue(stats.dropped_tasks)}</strong>
      </div>
      <div>
        <span>告警联动</span>
        <strong>{stats.alarm_linked ? '已接入' : '未接入'}</strong>
      </div>
      <div>
        <span>最近成功</span>
        <strong>{timeSince(stats.last_success_time_ms)}</strong>
      </div>
      <div>
        <span>最近失败</span>
        <strong>{timeSince(stats.last_failure_time_ms)}</strong>
      </div>
    </div>
  );
}

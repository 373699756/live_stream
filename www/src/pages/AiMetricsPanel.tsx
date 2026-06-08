import type { AiStatus } from '../api/types';

function metricValue(value: number) {
  return Number.isFinite(value) ? String(value) : '-';
}

function metricTime(value: number) {
  return Number.isFinite(value) ? `${value} ms` : '-';
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
  const summaryItems = [
    { label: '推理', value: metricValue(stats.inference_count) },
    { label: '失败', value: metricValue(stats.inference_failed_count) },
    { label: '有效', value: metricValue(stats.active_results) },
    { label: '丢弃', value: metricValue(stats.dropped_tasks) },
  ];
  const detailItems = [
    { label: '最近耗时', value: metricTime(stats.last_inference_time_ms) },
    { label: '平均耗时', value: metricTime(stats.average_inference_time_ms) },
    { label: '最大耗时', value: metricTime(stats.max_inference_time_ms) },
    { label: '告警联动', value: stats.alarm_linked ? '已接入' : '未接入' },
    { label: '最近成功', value: timeSince(stats.last_success_time_ms) },
    { label: '最近失败', value: timeSince(stats.last_failure_time_ms) },
  ];

  return (
    <div className="ai-metrics-strip">
      <div className="ai-metrics-summary">
        {summaryItems.map((item) => (
          <div className="ai-metric-summary-item" key={item.label}>
            <span>{item.label}</span>
            <strong>{item.value}</strong>
          </div>
        ))}
      </div>
      <div className="ai-metrics-detail">
        {detailItems.map((item) => (
          <span className="ai-metric-detail-item" key={item.label}>
            <span>{item.label}</span>
            <strong>{item.value}</strong>
          </span>
        ))}
      </div>
    </div>
  );
}

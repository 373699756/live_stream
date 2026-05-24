import { aiAlertImageUrl } from '../api/ai';
import type { AiAlertRecord, AiDetection, AiStatus } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { useAiAlerts } from '../hooks/useAiAlerts';

function formatTimestamp(timestampMs: number) {
  if (timestampMs <= 0) {
    return '-';
  }
  return new Date(timestampMs).toLocaleString();
}

function formatPercent(value: number) {
  return `${Math.round(value * 100)}%`;
}

function labelText(detections: AiDetection[]) {
  const labels = detections.map((detection) => detection.label).filter(Boolean);
  if (labels.length === 0) {
    return '-';
  }
  return Array.from(new Set(labels)).join(', ');
}

function taskLabel(task: AiAlertRecord['task']) {
  switch (task) {
    case 'face_detection':
      return '人脸检测';
    case 'motion_classification':
      return '移动侦测';
    case 'object_detection':
      return '目标检测';
  }
}

function backendLabel(status: AiStatus) {
  if (!status.config.enabled) {
    return '未启用';
  }
  return status.stats.backend_available ? '后端可用' : '后端不可用';
}

function metricValue(value: number) {
  return Number.isFinite(value) ? String(value) : '-';
}

function AiStatusPanel({ status }: { status: AiStatus | null }) {
  if (!status) {
    return <section className="panel">加载 AI 状态...</section>;
  }

  const badgeState =
    status.config.enabled && status.stats.backend_available
      ? 'running'
      : status.config.enabled
        ? 'error'
        : 'pending';

  return (
    <section className="panel wide-panel">
      <div className="ai-status-header">
        <div>
          <h2>AI 状态</h2>
          <p>
            {status.config.backend} / {taskLabel(status.config.task)}
          </p>
        </div>
        <StatusBadge state={badgeState} label={backendLabel(status)} />
      </div>
      <div className="metric-grid ai-metric-grid">
        <div>
          <span>推理次数</span>
          <strong>{metricValue(status.stats.inference_count)}</strong>
        </div>
        <div>
          <span>失败次数</span>
          <strong>{metricValue(status.stats.inference_failed_count)}</strong>
        </div>
        <div>
          <span>有效结果</span>
          <strong>{metricValue(status.stats.active_results)}</strong>
        </div>
        <div>
          <span>丢弃告警</span>
          <strong>{metricValue(status.stats.dropped_tasks)}</strong>
        </div>
      </div>
    </section>
  );
}

function AiAlertCard({ alert }: { alert: AiAlertRecord }) {
  return (
    <article className="ai-alert-card">
      <div className="ai-alert-image-wrap">
        <img src={aiAlertImageUrl(alert.image_url)} alt="" loading="lazy" />
      </div>
      <div className="ai-alert-body">
        <div className="ai-alert-title">
          <strong>{labelText(alert.detections)}</strong>
          <span>{formatPercent(alert.confidence_max)}</span>
        </div>
        <div className="ai-alert-meta">
          <span>{formatTimestamp(alert.timestamp_ms)}</span>
          <span>{alert.stream === 'main' ? '主码流' : '子码流'}</span>
          <span>{alert.detection_count} 个目标</span>
        </div>
      </div>
    </article>
  );
}

export function AiAlertsPage() {
  const { status, alerts, loading, error, refresh } = useAiAlerts();

  return (
    <div className="page-grid ai-page-grid">
      <div className="page-heading">
        <div>
          <h2>AI 告警</h2>
          <p>最近智能检测抓拍</p>
        </div>
        <button
          type="button"
          className="primary"
          disabled={loading}
          onClick={() => {
            void refresh();
          }}
        >
          刷新
        </button>
      </div>

      <AiStatusPanel status={status} />

      {error ? <div className="status-note error-note">{error}</div> : null}

      <section className="panel wide-panel">
        <div className="ai-waterfall-header">
          <h2>图片瀑布流</h2>
          <span>{loading ? '加载中' : `${alerts.length} 条`}</span>
        </div>
        {alerts.length === 0 && !loading ? (
          <div className="empty-state">暂无 AI 告警图片</div>
        ) : (
          <div className="ai-waterfall">
            {alerts.map((alert) => (
              <AiAlertCard alert={alert} key={alert.id} />
            ))}
          </div>
        )}
      </section>
    </div>
  );
}

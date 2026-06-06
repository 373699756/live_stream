import { aiAlertImageUrl } from '../api/ai';
import type {
  AiAlertRecord,
  AiDetection,
} from '../api/types';
import { useAiAlerts } from '../hooks/useAiAlerts';
import { AiStatusPanel } from './AiStatusPanel';

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

      <AiStatusPanel status={status} onSaved={refresh} />

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

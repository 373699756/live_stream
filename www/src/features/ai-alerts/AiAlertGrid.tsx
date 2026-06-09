import { aiAlertImageUrl } from '../../api/ai';
import type { AiAlertRecord } from '../../api/types';
import { formatTimestamp } from '../../utils/format';
import {
  cardTitleText,
  formatPercent,
  streamLabel,
} from './aiAlertFormat';

function AiAlertCard({ alert }: { alert: AiAlertRecord }) {
  return (
    <article className="ai-alert-card">
      <div className="ai-alert-image-wrap">
        <img
          src={aiAlertImageUrl(alert.image_url, alert.timestamp_ms)}
          alt=""
          loading="lazy"
        />
      </div>
      <div className="ai-alert-body">
        <div className="ai-alert-title">
          <strong>{cardTitleText(alert)}</strong>
          <span>{formatPercent(alert.confidence_max)}</span>
        </div>
        <div className="ai-alert-meta">
          <span>{formatTimestamp(alert.timestamp_ms)}</span>
          <span>{streamLabel(alert.stream)}</span>
          <span>{alert.detection_count} 个目标</span>
        </div>
      </div>
    </article>
  );
}

interface AiAlertGridProps {
  alerts: AiAlertRecord[];
}

export function AiAlertGrid({ alerts }: AiAlertGridProps) {
  return (
    <div className="ai-alert-grid">
      {alerts.map((alert) => (
        <AiAlertCard alert={alert} key={alert.id} />
      ))}
    </div>
  );
}

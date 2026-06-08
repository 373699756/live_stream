import { useMemo } from 'react';
import { aiAlertImageUrl } from '../api/ai';
import type {
  AiAlertRecord,
  AiDetection,
} from '../api/types';
import { useAiAlerts } from '../hooks/useAiAlerts';
import { formatTimestamp } from '../utils/format';
import { AiStatusPanel } from './AiStatusPanel';

const kMaxVisibleAlertImages = 10;

const kAiAlertColumns: Array<{
  task: AiAlertRecord['task'];
  condition: string;
}> = [
  {
    task: 'object_detection',
    condition: '检测到人员、车辆等目标，且最高置信度达到当前阈值。',
  },
  {
    task: 'motion_classification',
    condition: '检测到有效画面移动，且最高置信度达到当前阈值。',
  },
  {
    task: 'occlusion_detection',
    condition: '检测到画面大面积变暗或镜头被遮挡，且置信度达到当前阈值。',
  },
];

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
    case 'occlusion_detection':
      return '遮挡检测';
    case 'object_detection':
      return '目标检测';
  }
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

function alertGroupsByTask(alerts: AiAlertRecord[]) {
  const groups: Record<AiAlertRecord['task'], AiAlertRecord[]> = {
    object_detection: [],
    face_detection: [],
    motion_classification: [],
    occlusion_detection: [],
  };
  alerts.forEach((alert) => {
    groups[alert.task].push(alert);
  });
  return groups;
}

export function AiAlertsPage() {
  const { status, alerts, loading, error, refresh } = useAiAlerts();
  const visibleAlerts = useMemo(
    () =>
      [...alerts]
        .sort((left, right) => right.timestamp_ms - left.timestamp_ms)
        .slice(0, kMaxVisibleAlertImages),
    [alerts],
  );
  const alertGroups = useMemo(
    () => alertGroupsByTask(visibleAlerts),
    [visibleAlerts],
  );

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
          <span>
            {loading
              ? '加载中'
              : `${visibleAlerts.length}/${kMaxVisibleAlertImages} 张`}
          </span>
        </div>
        <div className="ai-waterfall">
          {kAiAlertColumns.map((column) => {
            const columnAlerts = alertGroups[column.task];
            return (
              <section className="ai-waterfall-column" key={column.task}>
                <div className="ai-waterfall-condition">
                  <div>
                    <h3>{taskLabel(column.task)}</h3>
                    <span>{columnAlerts.length} 张</span>
                  </div>
                  <p>{column.condition}</p>
                </div>
                {columnAlerts.length === 0 ? (
                  <div className="ai-column-empty">
                    {loading ? '加载中' : '暂无告警图片'}
                  </div>
                ) : (
                  columnAlerts.map((alert) => (
                    <AiAlertCard alert={alert} key={alert.id} />
                  ))
                )}
              </section>
            );
          })}
        </div>
      </section>
    </div>
  );
}

import { useEffect, useState } from 'react';
import { aiAlertImageUrl, saveAiConfig } from '../api/ai';
import type {
  AiAlertRecord,
  AiDetection,
  AiModelConfig,
  AiStatus,
} from '../api/types';
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

function AiStatusPanel({
  status,
  onSaved,
}: {
  status: AiStatus | null;
  onSaved: () => Promise<void>;
}) {
  const [draft, setDraft] = useState<AiModelConfig | null>(null);
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState('');

  useEffect(() => {
    setDraft(status?.config ?? null);
  }, [status]);

  if (!status) {
    return <section className="panel">加载 AI 状态...</section>;
  }

  const config = draft ?? status.config;
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
          <span>最近耗时</span>
          <strong>{metricValue(status.stats.last_inference_time_ms)} ms</strong>
        </div>
        <div>
          <span>平均耗时</span>
          <strong>{metricValue(status.stats.average_inference_time_ms)} ms</strong>
        </div>
        <div>
          <span>最大耗时</span>
          <strong>{metricValue(status.stats.max_inference_time_ms)} ms</strong>
        </div>
        <div>
          <span>丢弃告警</span>
          <strong>{metricValue(status.stats.dropped_tasks)}</strong>
        </div>
        <div>
          <span>告警联动</span>
          <strong>{status.stats.alarm_linked ? '已接入' : '未接入'}</strong>
        </div>
        <div>
          <span>最近成功</span>
          <strong>{timeSince(status.stats.last_success_time_ms)}</strong>
        </div>
        <div>
          <span>最近失败</span>
          <strong>{timeSince(status.stats.last_failure_time_ms)}</strong>
        </div>
      </div>
      <div className="ai-config-grid">
        <label className="form-field">
          <span className="form-label">启用</span>
          <span className="form-control">
            <input
              checked={config.enabled}
              type="checkbox"
              onChange={(event) =>
                setDraft({ ...config, enabled: event.target.checked })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">后端</span>
          <span className="form-control">
            <select
              value={config.backend}
              onChange={(event) =>
                setDraft({
                  ...config,
                  backend: event.target.value as AiModelConfig['backend'],
                })
              }
            >
              <option value="hisi3516dv300_nnie">HiSilicon NNIE/IVS</option>
              <option value="host_stub">Host stub</option>
            </select>
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">任务</span>
          <span className="form-control">
            <select
              value={config.task}
              onChange={(event) =>
                setDraft({
                  ...config,
                  task: event.target.value as AiModelConfig['task'],
                })
              }
            >
              <option value="object_detection">目标检测</option>
              <option value="face_detection">人脸检测</option>
              <option value="motion_classification">移动侦测</option>
            </select>
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">码流</span>
          <span className="form-control">
            <select
              value={config.stream}
              onChange={(event) =>
                setDraft({
                  ...config,
                  stream: event.target.value as AiModelConfig['stream'],
                })
              }
            >
              <option value="sub">子码流</option>
              <option value="main">主码流</option>
            </select>
          </span>
        </label>
        <label className="form-field ai-config-path">
          <span className="form-label">模型</span>
          <span className="form-control">
            <input
              value={config.model_path}
              onChange={(event) =>
                setDraft({ ...config, model_path: event.target.value })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">间隔 ms</span>
          <span className="form-control">
            <input
              min={100}
              step={100}
              type="number"
              value={config.inference_interval_ms}
              onChange={(event) =>
                setDraft({
                  ...config,
                  inference_interval_ms: Number(event.target.value),
                })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">阈值</span>
          <span className="form-control">
            <input
              max={1}
              min={0}
              step={0.05}
              type="number"
              value={config.confidence_threshold}
              onChange={(event) =>
                setDraft({
                  ...config,
                  confidence_threshold: Number(event.target.value),
                })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">结果数</span>
          <span className="form-control">
            <input
              min={1}
              step={1}
              type="number"
              value={config.max_results}
              onChange={(event) =>
                setDraft({ ...config, max_results: Number(event.target.value) })
              }
            />
          </span>
        </label>
        <div className="ai-config-actions">
          <button
            type="button"
            disabled={saving}
            onClick={() => {
              setDraft(status.config);
              setSaveMessage('');
            }}
          >
            恢复
          </button>
          <button
            type="button"
            className="primary"
            disabled={saving}
            onClick={() => {
              setSaving(true);
              setSaveMessage('');
              void saveAiConfig(config)
                .then(onSaved)
                .then(() => {
                  setSaveMessage('已保存并应用');
                })
                .catch((err: unknown) => {
                  setSaveMessage(
                    err instanceof Error ? err.message : '保存失败',
                  );
                })
                .finally(() => setSaving(false));
            }}
          >
            {saving ? '保存中' : '保存配置'}
          </button>
          {saveMessage ? <span>{saveMessage}</span> : null}
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

import { useEffect, useMemo, useState } from 'react';
import { saveAiConfig } from '../api/ai';
import type {
  AiAlertRecord,
  AiConfig,
  AiModelConfig,
  AiStats,
  AiTaskName,
  AiTaskStatus,
  StreamName,
} from '../api/types';
import { AiDetectionOverlay } from '../components/AiDetectionOverlay';
import { StatusBadge } from '../components/StatusBadge';
import { VideoPreview } from '../components/VideoPreview';
import { useAiAlerts } from '../hooks/useAiAlerts';
import { useLiveView } from '../hooks/useLiveView';
import {
  latestAlarmTimeText,
  normalizeAiRootConfigForSave,
} from '../features/ai-alerts/aiAlertFormat';
import {
  taskLabel,
  taskRequiresModelPath,
} from '../features/ai-alerts/aiAlertTasks';
import { aiAlertImageUrl } from '../api/ai';
import '../styles/ai-alerts.css';

const kTaskOrder: AiTaskName[] = [
  'object_detection',
  'perimeter_detection',
  'motion_classification',
  'occlusion_detection',
];

const taskShortLabel = (task: AiTaskName) => {
  switch (task) {
    case 'object_detection':
      return '目标';
    case 'perimeter_detection':
      return '周界';
    case 'motion_classification':
      return '移动';
    case 'occlusion_detection':
      return '遮挡';
  }
};

const streamLabel = (stream: StreamName) =>
  stream === 'main' ? '主码流' : '子码流';

const numberText = (value: number) =>
  Number.isFinite(value) ? String(Math.round(value)) : '--';

function defaultTaskConfig(task: AiTaskName): AiModelConfig {
  return {
    enabled: false,
    backend: 'hisi3516dv300_nnie',
    task,
    stream: 'sub',
    model_path:
      task === 'object_detection' || task === 'perimeter_detection'
        ? 'models/inst_ssd_cycle.wk'
        : '',
    input_width: 300,
    input_height: 300,
    inference_interval_ms: 500,
    confidence_threshold: 0.5,
    max_results: 16,
    perimeter_regions: [],
  };
}

function cloneTaskConfig(task: AiModelConfig): AiModelConfig {
  return {
    ...task,
    perimeter_regions: task.perimeter_regions.map((region) => ({ ...region })),
  };
}

function completeAiConfig(config: AiConfig): AiConfig {
  const tasks = kTaskOrder.map((taskName) =>
    cloneTaskConfig(
      config.tasks.find((task) => task.task === taskName) ??
        defaultTaskConfig(taskName),
    ),
  );
  return {
    ...config,
    enabled: tasks.some((task) => task.enabled),
    tasks,
  };
}

function emptyStats(): AiStats {
  return {
    enabled: false,
    backend_available: false,
    alarm_linked: false,
    last_success_time_ms: 0,
    last_failure_time_ms: 0,
    received_frames: 0,
    skipped_frames: 0,
    inference_count: 0,
    inference_failed_count: 0,
    dropped_tasks: 0,
    last_inference_time_ms: 0,
    max_inference_time_ms: 0,
    average_inference_time_ms: 0,
    active_results: 0,
  };
}

function taskStatusText(task: AiTaskStatus | undefined) {
  if (!task) {
    return '未配置';
  }
  if (!task.config.enabled) {
    return '关闭';
  }
  if (!task.stats.enabled) {
    return '未运行';
  }
  if (!task.stats.backend_available) {
    return '后端异常';
  }
  return '运行';
}

function taskBadgeState(task: AiTaskStatus | undefined) {
  if (!task || !task.config.enabled) {
    return 'pending' as const;
  }
  if (!task.stats.enabled) {
    return 'pending' as const;
  }
  return task.stats.backend_available ? ('running' as const) : ('error' as const);
}

function maxConfidence(alert: AiAlertRecord) {
  return `${Math.round(alert.confidence_max * 100)}%`;
}

function taskByName(tasks: AiTaskStatus[], name: AiTaskName) {
  return tasks.find((task) => task.config.task === name);
}

function draftTaskByName(config: AiConfig | null, name: AiTaskName) {
  return config?.tasks.find((task) => task.task === name);
}

function updateTaskConfig(
  config: AiConfig,
  taskName: AiTaskName,
  patch: Partial<AiModelConfig>,
): AiConfig {
  return {
    ...config,
    tasks: config.tasks.map((task) =>
      task.task === taskName ? { ...task, ...patch } : task,
    ),
  };
}

function TaskRuntimeStrip({
  status,
}: {
  status: AiTaskStatus | undefined;
}) {
  return (
    <div className="ai-task-strip-item">
      <div>
        <strong>
          {status ? taskShortLabel(status.config.task) : '未配置'}
        </strong>
        <span>{status ? streamLabel(status.config.stream) : '--'}</span>
      </div>
      <StatusBadge
        state={taskBadgeState(status)}
        label={taskStatusText(status)}
      />
      <small>
        {numberText(status?.stats.inference_count ?? 0)} 次 /{' '}
        {numberText(status?.stats.active_results ?? 0)} 个结果
      </small>
    </div>
  );
}

function SnapshotWaterfall({ alerts }: { alerts: AiAlertRecord[] }) {
  const latestAlerts = alerts
    .slice()
    .sort((left, right) => right.timestamp_ms - left.timestamp_ms)
    .slice(0, 10);

  return (
    <aside className="ai-snapshot-rail" aria-label="AI 实时抓图">
      <div className="ai-snapshot-rail-header">
        <div>
          <h3>实时抓图</h3>
          <span>最新 {latestAlerts.length}/10</span>
        </div>
      </div>
      <div className="ai-snapshot-waterfall">
        {latestAlerts.length === 0 ? (
          <div className="ai-snapshot-empty">暂无 AI 抓图</div>
        ) : (
          latestAlerts.map((alert, index) => (
            <article
              className={`ai-snapshot-card ${index % 3 === 1 ? 'is-tall' : ''}`}
              key={alert.id}
            >
              <img
                alt={`${taskLabel(alert.task)} ${alert.id}`}
                src={aiAlertImageUrl(alert.image_url, alert.timestamp_ms)}
              />
              <div className="ai-snapshot-card-meta">
                <strong>{taskShortLabel(alert.task)}</strong>
                <span>{alert.detection_count} 个</span>
                <span>{maxConfidence(alert)}</span>
              </div>
            </article>
          ))
        )}
      </div>
    </aside>
  );
}

function TaskConfigRow({
  draft,
  status,
  taskName,
  onChange,
}: {
  draft: AiConfig;
  status: AiTaskStatus | undefined;
  taskName: AiTaskName;
  onChange: (config: AiConfig) => void;
}) {
  const task = draftTaskByName(draft, taskName);
  if (!task) {
    return (
      <div className="ai-task-config-row">
        <div className="empty-state">任务配置缺失</div>
      </div>
    );
  }
  const modelMissing =
    task.enabled &&
    taskRequiresModelPath(task.task, task.backend) &&
    task.model_path.trim() === '';

  return (
    <div className="ai-task-config-row">
      <div className="ai-task-config-main">
        <label className="ai-task-enable">
          <input
            checked={task.enabled}
            type="checkbox"
            onChange={(event) =>
              onChange(updateTaskConfig(draft, taskName, {
                enabled: event.target.checked,
              }))
            }
          />
          <span>{taskLabel(task.task)}</span>
        </label>
        <StatusBadge
          state={taskBadgeState(status)}
          label={taskStatusText(status)}
        />
      </div>
      <div className="ai-task-config-fields">
        <label>
          <span>码流</span>
          <select
            value={task.stream}
            onChange={(event) =>
              onChange(updateTaskConfig(draft, taskName, {
                stream: event.target.value as StreamName,
              }))
            }
          >
            <option value="sub">子码流</option>
            <option value="main">主码流</option>
          </select>
        </label>
        <label>
          <span>阈值</span>
          <input
            max={1}
            min={0}
            step={0.05}
            type="number"
            value={task.confidence_threshold}
            onChange={(event) =>
              onChange(updateTaskConfig(draft, taskName, {
                confidence_threshold: Number(event.target.value),
              }))
            }
          />
        </label>
        <label>
          <span>间隔</span>
          <input
            min={1}
            step={100}
            type="number"
            value={task.inference_interval_ms}
            onChange={(event) =>
              onChange(updateTaskConfig(draft, taskName, {
                inference_interval_ms: Number(event.target.value),
              }))
            }
          />
        </label>
        <label>
          <span>结果数</span>
          <input
            min={1}
            step={1}
            type="number"
            value={task.max_results}
            onChange={(event) =>
              onChange(updateTaskConfig(draft, taskName, {
                max_results: Number(event.target.value),
              }))
            }
          />
        </label>
        <label className="ai-task-model-field">
          <span>模型</span>
          <input
            aria-invalid={modelMissing}
            value={task.model_path}
            onChange={(event) =>
              onChange(updateTaskConfig(draft, taskName, {
                model_path: event.target.value,
              }))
            }
          />
        </label>
      </div>
      {modelMissing ? (
        <span className="form-error">模型路径不能为空</span>
      ) : null}
    </div>
  );
}

export function AiAlertsPage() {
  const [stream, setStream] = useState<StreamName>('sub');
  const { statuses, playbackUrls } = useLiveView(stream);
  const {
    status,
    alarmStatus,
    lastAlarmEvent,
    alerts,
    loading,
    error,
    refresh,
  } = useAiAlerts();
  const [draft, setDraft] = useState<AiConfig | null>(null);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState('');
  const activeStatus = statuses.find((item) => item.stream === stream);
  const summary = status?.summary ?? emptyStats();

  useEffect(() => {
    if (!status || dirty) {
      return;
    }
    setDraft({
      ...completeAiConfig(status.config),
    });
    setSaveMessage('');
  }, [dirty, status]);

  const orderedTaskStatuses = useMemo(
    () => kTaskOrder.map((task) => taskByName(status?.tasks ?? [], task)),
    [status],
  );
  const hasModelError = useMemo(() => {
    if (!draft) {
      return false;
    }
    return draft.tasks.some(
      (task) =>
        task.enabled &&
        taskRequiresModelPath(task.task, task.backend) &&
        task.model_path.trim() === '',
    );
  }, [draft]);

  const captureSnapshot = (nextStream: StreamName) => {
    const snapshot = playbackUrls?.stream === nextStream ? playbackUrls.snapshot : '';
    if (!snapshot) {
      return;
    }
    const separator = snapshot.includes('?') ? '&' : '?';
    window.open(
      `${snapshot}${separator}t=${Date.now()}`,
      '_blank',
      'noopener,noreferrer',
    );
  };

  const updateDraft = (nextDraft: AiConfig) => {
    setDraft(nextDraft);
    setDirty(true);
    setSaveMessage('');
  };

  const saveDraft = () => {
    if (!draft || hasModelError) {
      return;
    }
    setSaving(true);
    setSaveMessage('');
    void saveAiConfig(normalizeAiRootConfigForSave(draft))
      .then(refresh)
      .then(() => {
        setDirty(false);
        setSaveMessage('已保存并应用');
      })
      .catch((err: unknown) => {
        setSaveMessage(err instanceof Error ? err.message : '保存失败');
      })
      .finally(() => setSaving(false));
  };

  return (
    <div className="page-grid ai-console-page">
      <div className="page-heading ai-console-heading">
        <div>
          <h2>AI 智能事件</h2>
          <p>四类事件可并行运行，抓图列表实时保留最新 10 张</p>
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

      {error ? <div className="status-note error-note">{error}</div> : null}

      <div className="ai-console-layout">
        <main className="ai-console-main">
          <VideoPreview
            stream={stream}
            statuses={statuses}
            playbackUrls={playbackUrls}
            onStreamChange={setStream}
            onSnapshot={captureSnapshot}
            surfaceOverlay={
              <AiDetectionOverlay
                frameResolution={activeStatus?.resolution}
                status={status}
                stream={stream}
                error={error}
              />
            }
          />

          <section className="ai-runtime-compact">
            <div className="ai-runtime-kpis">
              <div>
                <span>AI</span>
                <strong>{status?.enabled ? '启用' : '关闭'}</strong>
              </div>
              <div>
                <span>后端</span>
                <strong>{summary.backend_available ? '可用' : '异常'}</strong>
              </div>
              <div>
                <span>有效结果</span>
                <strong>{numberText(summary.active_results)}</strong>
              </div>
              <div>
                <span>最近耗时</span>
                <strong>{numberText(summary.last_inference_time_ms)} ms</strong>
              </div>
              <div>
                <span>最近报警</span>
                <strong>{latestAlarmTimeText(alarmStatus, lastAlarmEvent)}</strong>
              </div>
            </div>
            <div className="ai-task-strip">
              {orderedTaskStatuses.map((taskStatus, index) => (
                <TaskRuntimeStrip
                  key={taskStatus?.config.task ?? kTaskOrder[index]}
                  status={taskStatus}
                />
              ))}
            </div>
          </section>

          <section className="ai-config-panel">
            <div className="ai-config-panel-header">
              <div>
                <h3>事件配置</h3>
                <span>
                  推理 {numberText(summary.inference_count)} 次 / 失败{' '}
                  {numberText(summary.inference_failed_count)} 次 / 平均{' '}
                  {numberText(summary.average_inference_time_ms)} ms
                </span>
              </div>
            </div>

            {!draft ? (
              <div className="empty-state">加载智能配置...</div>
            ) : (
              <div className="ai-task-config-list">
                {kTaskOrder.map((taskName) => (
                  <TaskConfigRow
                    draft={draft}
                    key={taskName}
                    status={taskByName(status?.tasks ?? [], taskName)}
                    taskName={taskName}
                    onChange={updateDraft}
                  />
                ))}
              </div>
            )}

            <div className="ai-config-actions">
              <span>四类任务独立保存阈值、间隔和模型路径</span>
              {dirty ? <span>有未保存修改</span> : null}
              {saveMessage ? <span>{saveMessage}</span> : null}
              <button
                type="button"
                disabled={!draft || saving}
                onClick={() => {
                  if (!status) {
                    return;
                  }
                  setDraft({
                    ...completeAiConfig(status.config),
                  });
                  setDirty(false);
                  setSaveMessage('');
                }}
              >
                恢复
              </button>
              <button
                type="button"
                className="primary"
                disabled={!draft || saving || hasModelError}
                onClick={saveDraft}
              >
                {saving ? '保存中' : '保存配置'}
              </button>
            </div>
          </section>
        </main>

        <SnapshotWaterfall alerts={alerts} />
      </div>
    </div>
  );
}

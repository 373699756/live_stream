import { useEffect, useMemo, useRef, useState } from 'react';
import { saveAiAlarmRule } from '../api/alarm';
import { aiAlertImageUrl, saveAiConfig } from '../api/ai';
import type {
  AiAlertRecord,
  AiDetection,
  AiModelConfig,
  AiStatus,
  AiTaskName,
  AlarmConfig,
  AlarmStatusResponse,
  AlarmRuleConfig,
  StreamName,
} from '../api/types';
import type { MediaEvent } from '../api/mediaEvents';
import { StatusBadge } from '../components/StatusBadge';
import { useAiAlerts } from '../hooks/useAiAlerts';
import { formatTimestamp } from '../utils/format';
import { AiMetricsPanel } from './AiMetricsPanel';
import { AiPerimeterEditor } from './AiPerimeterEditor';
import '../styles/ai-alerts.css';

interface AiEventTab {
  task: AiTaskName;
  label: string;
  title: string;
  emptyTitle: string;
  emptyText: string;
}

const kAiEventTabs: AiEventTab[] = [
  {
    task: 'occlusion_detection',
    label: '遮挡',
    title: '遮挡抓拍',
    emptyTitle: '暂无遮挡抓拍',
    emptyText: '当前没有镜头遮挡抓拍记录。',
  },
  {
    task: 'perimeter_detection',
    label: '周界',
    title: '周界抓拍',
    emptyTitle: '暂无周界抓拍',
    emptyText: '当前没有周界入侵抓拍记录。',
  },
  {
    task: 'motion_classification',
    label: '移动',
    title: '移动抓拍',
    emptyTitle: '暂无移动抓拍',
    emptyText: '当前没有画面移动抓拍记录。',
  },
  {
    task: 'object_detection',
    label: '目标',
    title: '目标抓拍',
    emptyTitle: '暂无目标抓拍',
    emptyText: '当前没有目标检测抓拍记录。',
  },
];

function clampPercent(value: number) {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(1, Math.max(0, value));
}

function positiveInteger(value: number, fallback: number) {
  if (!Number.isFinite(value)) {
    return fallback;
  }
  return Math.max(1, Math.round(value));
}

function nonNegativeInteger(value: number, fallback: number) {
  if (!Number.isFinite(value)) {
    return fallback;
  }
  return Math.max(0, Math.round(value));
}

function formatPercent(value: number) {
  return `${Math.round(clampPercent(value) * 100)}%`;
}

function labelText(detections: AiDetection[]) {
  const labels = detections.map((detection) => detection.label).filter(Boolean);
  if (labels.length === 0) {
    return '-';
  }
  return Array.from(new Set(labels)).join(', ');
}

function cardTitleText(alert: AiAlertRecord) {
  const labels = labelText(alert.detections);
  if (alert.task === 'perimeter_detection' && labels !== '-') {
    return `周界: ${labels}`;
  }
  return labels;
}

function taskLabel(task: AiTaskName) {
  switch (task) {
    case 'perimeter_detection':
      return '周界检测';
    case 'motion_classification':
      return '移动侦测';
    case 'occlusion_detection':
      return '遮挡检测';
    case 'object_detection':
      return '目标检测';
  }
}

function taskDescription(task: AiTaskName) {
  switch (task) {
    case 'perimeter_detection':
      return '目标进入周界区域后生成抓拍，并注入 AI 告警输入。';
    case 'motion_classification':
      return '画面出现有效移动后生成抓拍，并注入 AI 告警输入。';
    case 'occlusion_detection':
      return '镜头被遮挡或画面异常后生成抓拍，并注入 AI 告警输入。';
    case 'object_detection':
      return '检测到人员、车辆等目标后生成抓拍，并注入 AI 告警输入。';
  }
}

function taskCaptureScope(task: AiTaskName) {
  switch (task) {
    case 'perimeter_detection':
      return '周界抓拍保存的是进入区域的目标，卡片上的 person/vehicle 是模型识别出的目标类别。';
    case 'motion_classification':
      return '移动抓拍保存的是画面移动触发的快照。';
    case 'occlusion_detection':
      return '遮挡抓拍保存的是镜头遮挡或画面异常触发的快照。';
    case 'object_detection':
      return '目标抓拍保存的是人员、车辆等目标检测快照。';
  }
}

function taskUsesModel(task: AiTaskName) {
  return task === 'object_detection' || task === 'perimeter_detection';
}

function streamLabel(stream: StreamName) {
  return stream === 'main' ? '主码流' : '子码流';
}

function backendLabel(status: AiStatus) {
  if (!status.config.enabled) {
    return 'AI 未启用';
  }
  return status.stats.backend_available ? '后端可用' : '后端不可用';
}

function backendBadgeState(status: AiStatus): 'running' | 'pending' | 'error' {
  if (!status.config.enabled) {
    return 'pending';
  }
  return status.stats.backend_available ? 'running' : 'error';
}

function alarmBadgeLabel(status: AiStatus, alarmConfig: AlarmConfig | null) {
  if (!status.stats.alarm_linked) {
    return 'alarm 未接入';
  }
  if (!alarmConfig) {
    return '读取报警规则';
  }
  return alarmConfig.ai_detection.enabled ? '报警已启用' : '报警未启用';
}

function alarmSourceLabel(source: string) {
  switch (source) {
    case 'ai_detection':
      return 'AI 告警';
    case 'motion':
    case 'motion_detection':
      return '移动侦测';
    case 'io_input':
      return 'IO 输入';
    case 'tamper':
      return '防拆';
    case 'network':
      return '网络';
    case 'unknown':
      return '--';
    default:
      return source || '--';
  }
}

function alarmBadgeState(
  status: AiStatus,
  alarmConfig: AlarmConfig | null,
): 'running' | 'pending' | 'error' {
  if (!status.stats.alarm_linked) {
    return 'error';
  }
  if (!alarmConfig) {
    return 'pending';
  }
  return alarmConfig.ai_detection.enabled ? 'running' : 'pending';
}

function latestTimeText(alerts: AiAlertRecord[]) {
  if (alerts.length === 0) {
    return '--';
  }
  return formatTimestamp(alerts[0].timestamp_ms);
}

function latestAlarmTimeText(
  alarmStatus: AlarmStatusResponse | null,
  lastAlarmEvent: MediaEvent | null,
) {
  if (lastAlarmEvent?.timestamp_ms) {
    return formatTimestamp(lastAlarmEvent.timestamp_ms);
  }
  const lastTriggerTime = alarmStatus?.status.last_trigger_time_ms ?? 0;
  return lastTriggerTime > 0 ? formatTimestamp(lastTriggerTime) : '--';
}

function tabStateLabel(status: AiStatus | null, task: AiTaskName) {
  if (!status) {
    return '读取中';
  }
  if (status.config.task !== task) {
    return '未运行';
  }
  if (!status.config.enabled) {
    return '当前未启用';
  }
  return status.stats.backend_available ? '当前运行' : '后端异常';
}

function emptyTextForTask(
  status: AiStatus | null,
  activeTab: AiEventTab,
  activeTask: AiTaskName,
) {
  if (status && status.config.task !== activeTask) {
    return `当前运行任务是 ${taskLabel(status.config.task)}，切换为当前任务后才会生成新的 ${taskLabel(activeTask)} 抓拍。`;
  }
  if (status && status.config.enabled && !status.stats.backend_available) {
    return 'AI 已启用，但推理后端当前不可用。';
  }
  return activeTab.emptyText;
}

function normalizeAiConfigForSave(config: AiModelConfig): AiModelConfig {
  return {
    ...config,
    input_width: positiveInteger(config.input_width, 300),
    input_height: positiveInteger(config.input_height, 300),
    inference_interval_ms: positiveInteger(config.inference_interval_ms, 500),
    confidence_threshold: clampPercent(config.confidence_threshold),
    max_results: positiveInteger(config.max_results, 16),
    perimeter_regions: config.perimeter_regions ?? [],
  };
}

function alertsForTask(
  alertGroups: Record<AiTaskName, AiAlertRecord[]>,
  task: AiTaskName,
) {
  return alertGroups[task].slice(0, 10);
}

function AiAlertCard({ alert }: { alert: AiAlertRecord }) {
  return (
    <article className="ai-alert-card">
      <div className="ai-alert-image-wrap">
        <img src={aiAlertImageUrl(alert.image_url)} alt="" loading="lazy" />
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

function alertGroupsByTask(alerts: AiAlertRecord[]) {
  const groups: Record<AiTaskName, AiAlertRecord[]> = {
    object_detection: [],
    perimeter_detection: [],
    motion_classification: [],
    occlusion_detection: [],
  };
  alerts.forEach((alert) => {
    groups[alert.task].push(alert);
  });
  return groups;
}

function AiRuntimeSummary({
  status,
  alarmConfig,
  alarmStatus,
  lastAlarmEvent,
}: {
  status: AiStatus | null;
  alarmConfig: AlarmConfig | null;
  alarmStatus: AlarmStatusResponse | null;
  lastAlarmEvent: MediaEvent | null;
}) {
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

function AiCommonConfigPanel({
  status,
  alarmConfig,
  onSaved,
}: {
  status: AiStatus | null;
  alarmConfig: AlarmConfig | null;
  onSaved: () => Promise<void>;
}) {
  const [draft, setDraft] = useState<AiModelConfig | null>(null);
  const [alarmRule, setAlarmRule] = useState<AlarmRuleConfig | null>(null);
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState('');

  useEffect(() => {
    setDraft(status ? { ...status.config } : null);
    setSaveMessage('');
  }, [status]);

  useEffect(() => {
    setAlarmRule(
      alarmConfig
        ? {
            ...alarmConfig.ai_detection,
            regions: [...alarmConfig.ai_detection.regions],
          }
        : null,
    );
  }, [alarmConfig]);

  if (!draft) {
    return (
      <section className="ai-event-config">
        <div className="empty-state">加载智能配置...</div>
      </section>
    );
  }

  const modelRequired = draft.enabled && taskUsesModel(draft.task);
  const modelMissing = modelRequired && draft.model_path.trim() === '';
  const updateDraft = (nextConfig: AiModelConfig) => {
    setDraft(nextConfig);
    setSaveMessage('');
  };
  const updateAlarmRule = (nextRule: AlarmRuleConfig | null) => {
    setAlarmRule(nextRule);
    setSaveMessage('');
  };
  const saveEventConfig = () => {
    const nextConfig = normalizeAiConfigForSave(draft);
    if (nextConfig.enabled && taskUsesModel(nextConfig.task) && !nextConfig.model_path.trim()) {
      setSaveMessage('保存失败：模型路径不能为空');
      return;
    }
    setSaving(true);
    setSaveMessage('');
    const requests: Promise<void>[] = [saveAiConfig(nextConfig)];
    if (alarmConfig && alarmRule) {
      requests.push(
        saveAiAlarmRule(alarmConfig, {
          ...alarmRule,
          min_duration_ms: nonNegativeInteger(alarmRule.min_duration_ms, 0),
        }),
      );
    }
    void Promise.all(requests)
      .then(onSaved)
      .then(() => {
        setSaveMessage('已保存并应用');
      })
      .catch((err: unknown) => {
        setSaveMessage(err instanceof Error ? err.message : '保存失败');
      })
      .finally(() => setSaving(false));
  };

  return (
    <section className="ai-event-config">
      <div className="ai-event-config-header">
        <div>
          <h3>智能配置</h3>
          <p>当前设备一次只运行一个 AI 任务；这里保存公共参数和报警联动。</p>
        </div>
        <StatusBadge
          state={status ? backendBadgeState(status) : 'pending'}
          label={`当前任务: ${taskLabel(draft.task)}`}
        />
      </div>

      <div className="ai-event-config-grid">
        <label className="form-field">
          <span className="form-label">AI 使能</span>
          <span className="form-control">
            <input
              checked={draft.enabled}
              type="checkbox"
              onChange={(event) =>
                updateDraft({ ...draft, enabled: event.target.checked })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">报警联动</span>
          <span className="form-control">
            <input
              checked={alarmRule?.enabled ?? false}
              disabled={!alarmRule}
              type="checkbox"
              onChange={(event) =>
                updateAlarmRule(
                  alarmRule
                    ? { ...alarmRule, enabled: event.target.checked }
                    : alarmRule,
                )
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">码流</span>
          <span className="form-control">
            <select
              value={draft.stream}
              onChange={(event) =>
                updateDraft({
                  ...draft,
                  stream: event.target.value as StreamName,
                })
              }
            >
              <option value="sub">子码流</option>
              <option value="main">主码流</option>
            </select>
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
              value={draft.confidence_threshold}
              onChange={(event) =>
                updateDraft({
                  ...draft,
                  confidence_threshold: Number(event.target.value),
                })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">间隔 ms</span>
          <span className="form-control">
            <input
              min={1}
              step={100}
              type="number"
              value={draft.inference_interval_ms}
              onChange={(event) =>
                updateDraft({
                  ...draft,
                  inference_interval_ms: Number(event.target.value),
                })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">持续 ms</span>
          <span className="form-control">
            <input
              disabled={!alarmRule}
              min={0}
              step={100}
              type="number"
              value={alarmRule?.min_duration_ms ?? 0}
              onChange={(event) =>
                updateAlarmRule(
                  alarmRule
                    ? {
                        ...alarmRule,
                        min_duration_ms: Number(event.target.value),
                      }
                    : alarmRule,
                )
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
              value={draft.max_results}
              onChange={(event) =>
                updateDraft({ ...draft, max_results: Number(event.target.value) })
              }
            />
          </span>
        </label>

        <label className="form-field ai-model-path-field">
          <span className="form-label">模型</span>
          <span className="form-control">
            <input
              aria-invalid={modelMissing}
              value={draft.model_path}
              onChange={(event) =>
                updateDraft({ ...draft, model_path: event.target.value })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">输入宽</span>
          <span className="form-control">
            <input
              min={1}
              step={1}
              type="number"
              value={draft.input_width}
              onChange={(event) =>
                updateDraft({ ...draft, input_width: Number(event.target.value) })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">输入高</span>
          <span className="form-control">
            <input
              min={1}
              step={1}
              type="number"
              value={draft.input_height}
              onChange={(event) =>
                updateDraft({ ...draft, input_height: Number(event.target.value) })
              }
            />
          </span>
        </label>
      </div>

      <div className="ai-config-actions">
        <button
          type="button"
          disabled={saving}
          onClick={() => {
            if (status) {
              setDraft({ ...status.config });
            }
            if (alarmConfig) {
              setAlarmRule({
                ...alarmConfig.ai_detection,
                regions: [...alarmConfig.ai_detection.regions],
              });
            }
            setSaveMessage('');
          }}
        >
          恢复
        </button>
        <button
          type="button"
          className="primary"
          disabled={saving || modelMissing}
          onClick={saveEventConfig}
        >
          {saving ? '保存中' : '保存智能配置'}
        </button>
        {saveMessage ? <span>{saveMessage}</span> : null}
        {modelMissing ? <span className="form-error">模型路径不能为空</span> : null}
      </div>
    </section>
  );
}

function AiEventTaskPanel({
  status,
  activeTask,
  onSaved,
}: {
  status: AiStatus | null;
  activeTask: AiTaskName;
  onSaved: () => Promise<void>;
}) {
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState('');

  if (!status) {
    return (
      <section className="ai-task-panel">
        <div className="empty-state">加载事件任务...</div>
      </section>
    );
  }

  const isCurrentTask = status.config.task === activeTask;
  const switchTask = () => {
    if (
      status.config.enabled &&
      taskUsesModel(activeTask) &&
      !status.config.model_path.trim()
    ) {
      setSaveMessage('切换失败：模型路径不能为空');
      return;
    }
    setSaving(true);
    setSaveMessage('');
    void saveAiConfig(
      normalizeAiConfigForSave({
        ...status.config,
        task: activeTask,
      }),
    )
      .then(onSaved)
      .then(() => {
        setSaveMessage('已切换当前任务');
      })
      .catch((err: unknown) => {
        setSaveMessage(err instanceof Error ? err.message : '切换失败');
      })
      .finally(() => setSaving(false));
  };

  return (
    <section className="ai-task-panel">
      <div>
        <h3>{taskLabel(activeTask)}</h3>
        <p>{taskDescription(activeTask)}</p>
        <p>{taskCaptureScope(activeTask)}</p>
      </div>
      <div className="ai-task-panel-actions">
        <StatusBadge
          state={isCurrentTask ? backendBadgeState(status) : 'pending'}
          label={isCurrentTask ? '当前运行任务' : '未运行'}
        />
        <button
          type="button"
          className={isCurrentTask ? '' : 'primary'}
          disabled={isCurrentTask || saving}
          onClick={switchTask}
        >
          {saving ? '切换中' : isCurrentTask ? '已是当前任务' : '切换为当前任务'}
        </button>
        {saveMessage ? <span>{saveMessage}</span> : null}
      </div>
    </section>
  );
}

export function AiAlertsPage() {
  const {
    status,
    alarmConfig,
    alarmStatus,
    lastAlarmEvent,
    alerts,
    loading,
    error,
    refresh,
  } = useAiAlerts();
  const [activeTask, setActiveTask] =
    useState<AiTaskName>('perimeter_detection');
  const initialTaskSynced = useRef(false);

  useEffect(() => {
    if (!status || initialTaskSynced.current) {
      return;
    }
    setActiveTask(status.config.task);
    initialTaskSynced.current = true;
  }, [status]);

  const sortedAlerts = useMemo(
    () => [...alerts].sort((left, right) => right.timestamp_ms - left.timestamp_ms),
    [alerts],
  );
  const alertGroups = useMemo(
    () => alertGroupsByTask(sortedAlerts),
    [sortedAlerts],
  );
  const activeTab =
    kAiEventTabs.find((tab) => tab.task === activeTask) ?? kAiEventTabs[0];
  const activeAlerts = alertsForTask(alertGroups, activeTask);

  return (
    <div className="page-grid ai-page-grid">
      <div className="page-heading">
        <div>
          <h2>AI 告警</h2>
          <p>按任务查看最近抓拍；设备当前只运行一个 AI 任务</p>
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

      <AiRuntimeSummary
        status={status}
        alarmConfig={alarmConfig}
        alarmStatus={alarmStatus}
        lastAlarmEvent={lastAlarmEvent}
      />

      {error ? <div className="status-note error-note">{error}</div> : null}

      <AiCommonConfigPanel
        status={status}
        alarmConfig={alarmConfig}
        onSaved={refresh}
      />

      <section className="panel wide-panel ai-events-panel">
        <div className="ai-event-tabs" role="tablist" aria-label="AI 抓拍分类">
          {kAiEventTabs.map((tab) => {
            const selected = tab.task === activeTask;
            return (
              <button
                type="button"
                role="tab"
                aria-controls={`ai-event-${tab.task}`}
                aria-selected={selected}
                className={selected ? 'ai-event-tab active' : 'ai-event-tab'}
                key={tab.task}
                onClick={() => setActiveTask(tab.task)}
              >
                <span className="ai-event-tab-main">
                  <strong>{tab.label}</strong>
                  <em>{tabStateLabel(status, tab.task)}</em>
                </span>
                <span className="ai-event-tab-count">
                  {loading ? '...' : alertsForTask(alertGroups, tab.task).length}
                </span>
              </button>
            );
          })}
        </div>

        <div
          id={`ai-event-${activeTask}`}
          className="ai-event-page"
          role="tabpanel"
        >
          <div className="ai-event-toolbar">
            <div>
              <h2>{activeTab.title}</h2>
              <p>{taskLabel(activeTask)}</p>
            </div>
            <div className="ai-event-stats">
              <span>
                运行{' '}
                <strong>
                  {status?.config.task === activeTask ? '当前任务' : '未运行'}
                </strong>
              </span>
              <span>
                抓拍 <strong>{activeAlerts.length}</strong>
              </span>
              <span>
                上限 <strong>10 张</strong>
              </span>
              <span>
                最近抓拍 <strong>{latestTimeText(activeAlerts)}</strong>
              </span>
              <span>
                系统报警{' '}
                <strong>{latestAlarmTimeText(alarmStatus, lastAlarmEvent)}</strong>
              </span>
            </div>
          </div>

          <AiEventTaskPanel
            status={status}
            activeTask={activeTask}
            onSaved={refresh}
          />

          {activeTask === 'perimeter_detection' ? (
            <AiPerimeterEditor status={status} onSaved={refresh} />
          ) : null}

          {activeAlerts.length === 0 ? (
            <div className="ai-event-empty">
              <strong>{loading ? '正在加载' : activeTab.emptyTitle}</strong>
              <span>
                {loading
                  ? '读取 AI 抓拍记录...'
                  : emptyTextForTask(status, activeTab, activeTask)}
              </span>
            </div>
          ) : (
            <div className="ai-alert-grid">
              {activeAlerts.map((alert) => (
                <AiAlertCard alert={alert} key={alert.id} />
              ))}
            </div>
          )}
        </div>
      </section>
    </div>
  );
}

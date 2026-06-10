import { useEffect, useState } from 'react';
import { saveAiConfig } from '../api/ai';
import type {
  AiAlertRecord,
  AiConfig,
  AiModelConfig,
  AiStatus,
} from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { AiMetricsPanel } from '../features/ai-alerts/AiMetricsPanel';
import { AiConfigForm } from './AiConfigForm';

function taskLabel(task: AiAlertRecord['task']) {
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

function backendLabel(status: AiStatus) {
  if (!status.enabled) {
    return '未启用';
  }
  return status.summary.backend_available ? '后端可用' : '后端不可用';
}

interface AiStatusPanelProps {
  status: AiStatus | null;
  onSaved: () => Promise<void>;
}

export function AiStatusPanel({ status, onSaved }: AiStatusPanelProps) {
  const [draft, setDraft] = useState<AiConfig | null>(null);
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState('');

  useEffect(() => {
    setDraft(status ? {
      ...status.config,
      tasks: status.config.tasks.map((task) => ({ ...task })),
    } : null);
  }, [status]);

  if (!status) {
    return <section className="panel">加载 AI 状态...</section>;
  }

  const config = draft ?? status.config;
  const visibleTask =
    config.tasks.find((task) => task.task === 'perimeter_detection') ??
    config.tasks[0];
  const badgeState =
    status.enabled && status.summary.backend_available
      ? 'running'
      : status.enabled
        ? 'error'
        : 'pending';
  const restoreConfig = () => {
    setDraft({
      ...status.config,
      tasks: status.config.tasks.map((task) => ({ ...task })),
    });
    setSaveMessage('');
  };
  const saveConfig = () => {
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
  };

  return (
    <section className="panel wide-panel">
      <div className="ai-status-header">
        <div>
          <h2>AI 状态</h2>
          <p>
            {visibleTask
              ? `${visibleTask.backend} / ${taskLabel(visibleTask.task)}`
              : '未配置任务'}
          </p>
        </div>
        <StatusBadge state={badgeState} label={backendLabel(status)} />
      </div>
      <AiMetricsPanel stats={status.summary} />
      {visibleTask ? (
        <AiConfigForm
          config={visibleTask}
          saving={saving}
          saveMessage={saveMessage}
          onChange={(nextTask) =>
            setDraft({
              ...config,
              tasks: config.tasks.map((task) =>
                task.task === nextTask.task ? nextTask : task,
              ),
            })
          }
          onRestore={restoreConfig}
          onSave={saveConfig}
        />
      ) : null}
    </section>
  );
}

import { useEffect, useState } from 'react';
import { saveAiConfig } from '../api/ai';
import type {
  AiAlertRecord,
  AiModelConfig,
  AiStatus,
} from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { AiConfigForm } from './AiConfigForm';
import { AiMetricsPanel } from './AiMetricsPanel';

function taskLabel(task: AiAlertRecord['task']) {
  switch (task) {
    case 'face_detection':
      return '人脸检测';
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
  if (!status.config.enabled) {
    return '未启用';
  }
  return status.stats.backend_available ? '后端可用' : '后端不可用';
}

interface AiStatusPanelProps {
  status: AiStatus | null;
  onSaved: () => Promise<void>;
}

export function AiStatusPanel({ status, onSaved }: AiStatusPanelProps) {
  const [draft, setDraft] = useState<AiModelConfig | null>(null);
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState('');

  useEffect(() => {
    setDraft(status ? { ...status.config } : null);
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
  const restoreConfig = () => {
    setDraft({ ...status.config });
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
            {config.backend} / {taskLabel(config.task)}
          </p>
        </div>
        <StatusBadge state={badgeState} label={backendLabel(status)} />
      </div>
      <AiMetricsPanel stats={status.stats} />
      <AiConfigForm
        config={config}
        saving={saving}
        saveMessage={saveMessage}
        onChange={setDraft}
        onRestore={restoreConfig}
        onSave={saveConfig}
      />
    </section>
  );
}

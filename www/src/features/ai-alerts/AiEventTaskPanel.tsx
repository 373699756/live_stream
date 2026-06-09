import { useState } from 'react';
import { saveAiConfig } from '../../api/ai';
import type { AiStatus, AiTaskName } from '../../api/types';
import { StatusBadge } from '../../components/StatusBadge';
import { normalizeAiConfigForSave } from './aiAlertFormat';
import { backendBadgeState } from './aiAlertStatus';
import {
  taskCaptureScope,
  taskDescription,
  taskLabel,
  taskUsesModel,
} from './aiAlertTasks';

interface AiEventTaskPanelProps {
  status: AiStatus | null;
  activeTask: AiTaskName;
  onSaved: () => Promise<void>;
}

export function AiEventTaskPanel({
  status,
  activeTask,
  onSaved,
}: AiEventTaskPanelProps) {
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

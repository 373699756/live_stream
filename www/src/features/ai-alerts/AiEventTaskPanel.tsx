import { useState } from 'react';
import { getAiStatus, saveAiConfig } from '../../api/ai';
import type { AiStatus, AiTaskName } from '../../api/types';
import { StatusBadge } from '../../components/StatusBadge';
import { normalizeAiConfigForSave } from './aiAlertFormat';
import { backendBadgeState } from './aiAlertStatus';
import {
  taskCaptureScope,
  taskDescription,
  taskLabel,
  taskRequiresModelPath,
} from './aiAlertTasks';

const kSwitchConfirmDelaysMs = [300, 900, 1800];

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

  const confirmSwitch = async () => {
    for (const delayMs of kSwitchConfirmDelaysMs) {
      await new Promise<void>((resolve) => window.setTimeout(resolve, delayMs));
      try {
        const nextStatus = await getAiStatus();
        if (nextStatus.config.task === activeTask) {
          return true;
        }
      } catch {
        // The page-level realtime refresh continues syncing after save.
      }
    }
    return false;
  };

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
      taskRequiresModelPath(activeTask, status.config.backend) &&
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
      .then(() => {
        setSaveMessage('已提交切换，正在确认');
        return confirmSwitch();
      })
      .then((confirmed) => {
        setSaveMessage(confirmed ? '已切换当前任务' : '已提交，等待设备同步');
        return onSaved().catch(() => {
          // The page-level realtime refresh keeps syncing status after save.
        });
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

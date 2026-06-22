import { useState } from 'react';
import { saveAiConfig } from '../../api/ai';
import type { AiStatus, AiTaskName } from '../../api/types';
import { StatusBadge } from '../../components/StatusBadge';
import { normalizeAiRootConfigForSave } from './aiAlertFormat';
import {
    isAiTaskAvailable,
    taskCaptureScope,
    taskDescription,
    taskLabel,
    taskRequiresModelPath,
    taskUnavailableText,
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

    const taskStatus = status.tasks.find(
        (item) => item.config.task === activeTask,
    );
    const taskConfig =
        status.config.tasks.find((item) => item.task === activeTask) ??
        taskStatus?.config;
    const taskAvailable = isAiTaskAvailable(activeTask);
    const taskEnabled = Boolean(taskConfig?.enabled && taskAvailable);
    const taskRunning = Boolean(taskStatus?.stats.enabled);
    const backendOk = Boolean(taskStatus?.stats.backend_available);
    const taskState: 'running' | 'pending' | 'error' = !taskAvailable
        ? 'pending'
        : !taskEnabled
          ? 'pending'
          : taskRunning
          ? backendOk
              ? 'running'
              : 'error'
          : 'pending';
    const taskStateLabel = !taskAvailable
        ? taskUnavailableText(activeTask)
        : !taskEnabled
        ? '未启用'
        : taskRunning
          ? backendOk
              ? '运行中'
              : '后端异常'
          : '未运行';

    const toggleTask = () => {
        if (!taskConfig) {
            return;
        }
        if (!taskAvailable) {
            setSaveMessage(taskUnavailableText(activeTask));
            return;
        }
        const nextEnabled = !taskEnabled;
        if (
            nextEnabled &&
            taskRequiresModelPath(taskConfig.task, taskConfig.backend) &&
            !taskConfig.model_path.trim()
        ) {
            setSaveMessage('启用失败：模型路径不能为空');
            return;
        }
        const nextConfig = {
            ...status.config,
            tasks: status.config.tasks.map((task) =>
                task.task === activeTask
                    ? { ...task, enabled: nextEnabled }
                    : task,
            ),
        };
        setSaving(true);
        setSaveMessage('');
        void saveAiConfig(normalizeAiRootConfigForSave(nextConfig))
            .then(onSaved)
            .then(() => {
                setSaveMessage(nextEnabled ? '已启用任务' : '已关闭任务');
            })
            .catch((err: unknown) => {
                setSaveMessage(err instanceof Error ? err.message : '保存失败');
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
                <StatusBadge state={taskState} label={taskStateLabel} />
                <button
                    type="button"
                    className={taskEnabled ? '' : 'primary'}
                    disabled={!taskConfig || saving || !taskAvailable}
                    onClick={toggleTask}
                >
                    {saving ? '保存中' : taskEnabled ? '关闭任务' : '启用任务'}
                </button>
                {saveMessage ? <span>{saveMessage}</span> : null}
            </div>
        </section>
    );
}

import { useEffect, useState } from 'react';
import { saveAiConfig } from '../api/ai';
import type { AiConfig, AiModelConfig, AiStatus } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { AiMetricsPanel } from '../features/ai-alerts/AiMetricsPanel';
import {
    isAiTaskAvailable,
    taskLabel,
    taskUnavailableText,
} from '../features/ai-alerts/aiAlertTasks';
import { normalizeAiRootConfigForSave } from '../features/ai-alerts/aiAlertFormat';
import { AiConfigForm } from './AiConfigForm';

function backendLabel(status: AiStatus) {
    if (!status.enabled) {
        return '未启用';
    }
    const enabledTasks = status.tasks.filter(
        (task) => task.config.enabled && isAiTaskAvailable(task.config.task),
    );
    if (enabledTasks.length === 0) {
        return '任务未启用';
    }
    return enabledTasks.every((task) => task.stats.backend_available)
        ? '后端可用'
        : '后端不可用';
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
        setDraft(
            status
                ? {
                      enabled: status.config.tasks.some(
                          (task) => task.enabled && isAiTaskAvailable(task.task),
                      ),
                      tasks: status.config.tasks.map((task) => ({
                          ...task,
                          enabled:
                              isAiTaskAvailable(task.task) && task.enabled,
                      })),
                  }
                : null,
        );
    }, [status]);

    if (!status) {
        return <section className="panel">加载 AI 状态...</section>;
    }

    const config = draft ?? status.config;
    const visibleTask =
        config.tasks.find((task) => isAiTaskAvailable(task.task)) ??
        config.tasks[0];
    const supportedEnabledTasks = status.tasks.filter(
        (task) =>
            task.config.enabled && isAiTaskAvailable(task.config.task),
    );
    const badgeState =
        supportedEnabledTasks.length === 0 || !status.enabled
            ? 'pending'
            : supportedEnabledTasks.every((task) => task.stats.backend_available)
              ? 'running'
              : 'error';
    const restoreConfig = () => {
        const tasks = status.config.tasks.map((task) => ({
            ...task,
            enabled: isAiTaskAvailable(task.task) && task.enabled,
        }));
        setDraft({
            ...status.config,
            enabled: tasks.some((task) => task.enabled),
            tasks,
        });
        setSaveMessage('');
    };
    const saveConfig = () => {
        setSaving(true);
        setSaveMessage('');
        void saveAiConfig(normalizeAiRootConfigForSave(config))
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
        <section className="panel wide-panel">
            <div className="ai-status-header">
                <div>
                    <h2>AI 状态</h2>
                    <p>
                        {visibleTask
                            ? `${visibleTask.backend} / ${taskLabel(visibleTask.task)}`
                            : '未配置任务'}
                        {visibleTask && !isAiTaskAvailable(visibleTask.task)
                            ? ` / ${taskUnavailableText(visibleTask.task)}`
                            : ''}
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
                                task.task === nextTask.task
                                    ? {
                                          ...nextTask,
                                          enabled:
                                              isAiTaskAvailable(task.task) &&
                                              nextTask.enabled,
                                      }
                                    : task,
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

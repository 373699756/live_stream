import type {
    AiCapabilities,
    AiConfig,
    AiTaskInfo,
    AiTaskName,
} from '../../api/types';
import { StatusBadge } from '../../components/StatusBadge';
import {
    isAiTaskAvailable,
    taskLabel,
    taskUnavailableText,
} from './aiAlertTasks';
import { kTaskOrder } from './aiAlertOptions';
import {
    draftTaskByName,
    numberText,
    streamLabel,
    taskBadgeState,
    taskStatusText,
} from './aiConfigDraft';

interface AiTaskSwitchListProps {
    aiCapabilities: AiCapabilities | null;
    alertSizes: Record<AiTaskName, number>;
    draft: AiConfig | null;
    editingPerimeter: boolean;
    orderedTaskStatuses: Array<AiTaskInfo | undefined>;
    togglePerimeterEdit: () => void;
    updateTaskEnabled: (taskName: AiTaskName, enabled: boolean) => void;
}

export function AiTaskSwitchList({
    aiCapabilities,
    alertSizes,
    draft,
    editingPerimeter,
    orderedTaskStatuses,
    togglePerimeterEdit,
    updateTaskEnabled,
}: AiTaskSwitchListProps) {
    if (!draft) {
        return <div className="empty-state">加载智能配置...</div>;
    }

    return (
        <div className="ai-event-switch-list">
            {kTaskOrder.map((taskName, index) => {
                const task = draftTaskByName(draft, taskName);
                const taskStatus = orderedTaskStatuses[index];
                const enabled = Boolean(task?.enabled);
                const available = isAiTaskAvailable(taskName, aiCapabilities);
                return (
                    <article className="ai-event-switch-row" key={taskName}>
                        <label className="ai-event-switch-main">
                            <input
                                checked={enabled}
                                disabled={!available}
                                type="checkbox"
                                onChange={(event) =>
                                    updateTaskEnabled(
                                        taskName,
                                        event.target.checked,
                                    )
                                }
                            />
                            <span
                                className="ai-event-toggle"
                                aria-hidden="true"
                            />
                            <span>
                                <strong>{taskLabel(taskName)}</strong>
                                <em>
                                    {available
                                        ? `${alertSizes[taskName]} 张抓图`
                                        : taskUnavailableText(
                                              taskName,
                                              aiCapabilities,
                                          )}
                                </em>
                            </span>
                        </label>
                        <StatusBadge
                            state={taskBadgeState(
                                taskStatus,
                                aiCapabilities,
                            )}
                            label={taskStatusText(taskStatus, aiCapabilities)}
                        />
                        <div className="ai-event-switch-stats">
                            <span>
                                {taskStatus
                                    ? streamLabel(taskStatus.config.stream)
                                    : '--'}
                            </span>
                            <span>
                                {numberText(
                                    taskStatus?.stats.inferences ?? 0,
                                )}{' '}
                                次
                            </span>
                            <span>
                                {numberText(
                                    taskStatus?.stats.active_results ?? 0,
                                )}{' '}
                                个结果
                            </span>
                        </div>
                        {taskName === 'perimeter_detection' ? (
                            <button
                                type="button"
                                className={editingPerimeter ? 'active' : ''}
                                disabled={!available}
                                onClick={togglePerimeterEdit}
                            >
                                {editingPerimeter ? '完成画框' : '编辑区域'}
                            </button>
                        ) : null}
                    </article>
                );
            })}
        </div>
    );
}

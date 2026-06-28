import type {
    AiCapabilities,
    AiConfig,
    AiModelConfig,
    AiPerimeterRegion,
    AiTaskInfo,
    AiTaskName,
    AlarmRuleConfig,
    StreamName,
} from '../../api/types';
import { StatusBadge } from '../../components/StatusBadge';
import {
    isAiTaskAvailable,
    taskLabel,
    taskUnavailableText,
} from './aiAlertTasks';
import {
    draftTaskByName,
    formatPercent,
    numberText,
    optionValue,
    streamLabel,
    taskBadgeState,
    taskStatusText,
} from './aiConfigDraft';
import {
    kAlarmDurationOptions,
    kMaxResultsOptions,
    kSensitivityOptions,
    kStreamOptions,
    kTaskOrder,
    type SensitivityLevel,
} from './aiAlertOptions';

interface NumericOption {
    label: string;
    value: number;
}

interface SharedTaskConfig {
    backend: AiModelConfig['backend'];
    stream: StreamName;
    model_path: string;
    input_width: number;
    input_height: number;
    inference_interval_ms: number;
    max_results: number;
}

interface AiEventWorkbenchProps {
    activeRegion: AiPerimeterRegion | null;
    activeRegionIndex: number;
    aiCapabilities: AiCapabilities | null;
    alarmDirty: boolean;
    alarmDurationOptions: NumericOption[];
    alarmRule: AlarmRuleConfig | null;
    alertSizes: Record<AiTaskName, number>;
    clearRegions: () => void;
    deleteRegion: () => void;
    dirty: boolean;
    draft: AiConfig | null;
    editingPerimeter: boolean;
    intervalOptions: NumericOption[];
    maxResultsOptions: NumericOption[];
    perimeterRegions: AiPerimeterRegion[];
    restoreDraft: () => void;
    saveDraft: () => void;
    saveMsg: string;
    saving: boolean;
    selectRegion: (index: number) => void;
    sensitivity: SensitivityLevel;
    sharedConfig: SharedTaskConfig | null;
    togglePerimeterEdit: () => void;
    updateAlarmRuleWith: (patch: Partial<AlarmRuleConfig>) => void;
    updateAllTasks: (patch: Partial<AiModelConfig>) => void;
    updateSensitivity: (nextSensitivity: SensitivityLevel) => void;
    updateTaskEnabled: (taskName: AiTaskName, enabled: boolean) => void;
    addRegion: () => void;
    orderedTaskStatuses: Array<AiTaskInfo | undefined>;
}

export function AiEventWorkbench({
    activeRegion,
    activeRegionIndex,
    aiCapabilities,
    alarmDirty,
    alarmDurationOptions,
    alarmRule,
    alertSizes,
    clearRegions,
    deleteRegion,
    dirty,
    draft,
    editingPerimeter,
    intervalOptions,
    maxResultsOptions,
    perimeterRegions,
    restoreDraft,
    saveDraft,
    saveMsg,
    saving,
    selectRegion,
    sensitivity,
    sharedConfig,
    togglePerimeterEdit,
    updateAlarmRuleWith,
    updateAllTasks,
    updateSensitivity,
    updateTaskEnabled,
    addRegion,
    orderedTaskStatuses,
}: AiEventWorkbenchProps) {
    return (
        <section className="ai-event-workbench">
            <div className="ai-event-workbench-header">
                <div>
                    <h3>事件配置</h3>
                    <span>开关事件，调整检测参数</span>
                </div>
            </div>

            <div className="ai-event-option-grid">
                <label>
                    <span>检测码流</span>
                    <select
                        disabled={!draft}
                        value={sharedConfig?.stream ?? 'sub'}
                        onChange={(event) =>
                            updateAllTasks({
                                stream: event.target.value as StreamName,
                            })
                        }
                    >
                        {kStreamOptions.map((option) => (
                            <option key={option.value} value={option.value}>
                                {option.label}
                            </option>
                        ))}
                    </select>
                </label>
                <label>
                    <span>灵敏度</span>
                    <select
                        disabled={!draft}
                        value={sensitivity}
                        onChange={(event) =>
                            updateSensitivity(
                                event.target.value as SensitivityLevel,
                            )
                        }
                    >
                        {kSensitivityOptions.map((option) => (
                            <option key={option.value} value={option.value}>
                                {option.label}
                            </option>
                        ))}
                    </select>
                </label>
                <label>
                    <span>推理频率</span>
                    <select
                        disabled={!draft}
                        value={optionValue(
                            sharedConfig?.inference_interval_ms ?? 500,
                        )}
                        onChange={(event) =>
                            updateAllTasks({
                                inference_interval_ms: Number(
                                    event.target.value,
                                ),
                            })
                        }
                    >
                        {intervalOptions.map((option) => (
                            <option
                                key={option.value}
                                value={optionValue(option.value)}
                            >
                                {option.label}
                            </option>
                        ))}
                    </select>
                </label>
                <label>
                    <span>结果上限</span>
                    <select
                        disabled={!draft}
                        value={optionValue(sharedConfig?.max_results ?? 16)}
                        onChange={(event) =>
                            updateAllTasks({
                                max_results: Number(event.target.value),
                            })
                        }
                    >
                        {maxResultsOptions.map((option) => (
                            <option
                                key={option.value}
                                value={optionValue(option.value)}
                            >
                                {option.label}
                            </option>
                        ))}
                    </select>
                </label>
                <label>
                    <span>报警持续</span>
                    <select
                        disabled={!alarmRule}
                        value={optionValue(alarmRule?.min_duration_ms ?? 0)}
                        onChange={(event) =>
                            updateAlarmRuleWith({
                                min_duration_ms: Number(event.target.value),
                            })
                        }
                    >
                        {alarmDurationOptions.map((option) => (
                            <option
                                key={option.value}
                                value={optionValue(option.value)}
                            >
                                {option.label}
                            </option>
                        ))}
                    </select>
                </label>
            </div>

            {!draft ? (
                <div className="empty-state">加载智能配置...</div>
            ) : (
                <div className="ai-event-switch-list">
                    {kTaskOrder.map((taskName, index) => {
                        const task = draftTaskByName(draft, taskName);
                        const taskStatus = orderedTaskStatuses[index];
                        const enabled = Boolean(task?.enabled);
                        const available = isAiTaskAvailable(
                            taskName,
                            aiCapabilities,
                        );
                        return (
                            <article
                                className="ai-event-switch-row"
                                key={taskName}
                            >
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
                                    label={taskStatusText(
                                        taskStatus,
                                        aiCapabilities,
                                    )}
                                />
                                <div className="ai-event-switch-stats">
                                    <span>
                                        {taskStatus
                                            ? streamLabel(
                                                  taskStatus.config.stream,
                                              )
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
                                            taskStatus?.stats.active_results ??
                                                0,
                                        )}{' '}
                                        个结果
                                    </span>
                                </div>
                                {taskName === 'perimeter_detection' ? (
                                    <button
                                        type="button"
                                        className={
                                            editingPerimeter ? 'active' : ''
                                        }
                                        disabled={!available}
                                        onClick={togglePerimeterEdit}
                                    >
                                        {editingPerimeter
                                            ? '完成画框'
                                            : '编辑区域'}
                                    </button>
                                ) : null}
                            </article>
                        );
                    })}
                </div>
            )}

            {editingPerimeter ? (
                <div className="ai-perimeter-toolbar">
                    <div>
                        <strong>周界区域</strong>
                        <span>
                            {perimeterRegions.length > 0
                                ? `${perimeterRegions.length} 个区域`
                                : '整幅画面'}
                        </span>
                        {activeRegion ? (
                            <em>
                                当前 {activeRegionIndex + 1}: x{' '}
                                {formatPercent(activeRegion.x)} / y{' '}
                                {formatPercent(activeRegion.y)} / w{' '}
                                {formatPercent(activeRegion.width)} / h{' '}
                                {formatPercent(activeRegion.height)}
                            </em>
                        ) : null}
                        {perimeterRegions.length > 0 ? (
                            <div
                                className="ai-perimeter-region-list"
                                aria-label="周界区域"
                            >
                                {perimeterRegions.map((region, index) => (
                                    <button
                                        type="button"
                                        className={
                                            activeRegionIndex === index
                                                ? 'active'
                                                : ''
                                        }
                                        key={`${region.name}-${index}`}
                                        onClick={() => selectRegion(index)}
                                    >
                                        {index + 1}
                                    </button>
                                ))}
                            </div>
                        ) : null}
                    </div>
                    <div className="ai-perimeter-actions">
                        <button type="button" onClick={addRegion}>
                            新增区域
                        </button>
                        <button
                            type="button"
                            disabled={!activeRegion}
                            onClick={deleteRegion}
                        >
                            删除当前
                        </button>
                        <button
                            type="button"
                            disabled={perimeterRegions.length === 0}
                            onClick={clearRegions}
                        >
                            清空
                        </button>
                    </div>
                </div>
            ) : null}

            <div className="ai-config-actions">
                {dirty || alarmDirty ? <span>有未保存修改</span> : null}
                {saveMsg ? <span>{saveMsg}</span> : null}
                <button
                    type="button"
                    disabled={!draft || saving}
                    onClick={restoreDraft}
                >
                    恢复
                </button>
                <button
                    type="button"
                    className="primary"
                    disabled={!draft || saving}
                    onClick={saveDraft}
                >
                    {saving ? '保存中' : '保存配置'}
                </button>
            </div>
        </section>
    );
}

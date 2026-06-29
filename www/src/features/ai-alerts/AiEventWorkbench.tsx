import type {
    AiCapabilities,
    AiConfig,
    AiModelConfig,
    AiPerimeterRegion,
    AiTaskInfo,
    AiTaskName,
    AlarmRuleConfig,
} from '../../api/types';
import { AiConfigActions } from './AiConfigActions';
import { AiEventOptions } from './AiEventOptions';
import { AiPerimeterToolbar } from './AiPerimeterToolbar';
import { AiTaskSwitchList } from './AiTaskSwitchList';
import type {
    NumericOption,
    SharedTaskConfig,
} from './aiEventConfigTypes';
import type { SensitivityLevel } from './aiAlertOptions';

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

            <AiEventOptions
                alarmDurationOptions={alarmDurationOptions}
                alarmRule={alarmRule}
                draft={draft}
                intervalOptions={intervalOptions}
                maxResultsOptions={maxResultsOptions}
                sensitivity={sensitivity}
                sharedConfig={sharedConfig}
                updateAlarmRuleWith={updateAlarmRuleWith}
                updateAllTasks={updateAllTasks}
                updateSensitivity={updateSensitivity}
            />

            <AiTaskSwitchList
                aiCapabilities={aiCapabilities}
                alertSizes={alertSizes}
                draft={draft}
                editingPerimeter={editingPerimeter}
                orderedTaskStatuses={orderedTaskStatuses}
                togglePerimeterEdit={togglePerimeterEdit}
                updateTaskEnabled={updateTaskEnabled}
            />

            {editingPerimeter ? (
                <AiPerimeterToolbar
                    activeRegion={activeRegion}
                    activeRegionIndex={activeRegionIndex}
                    addRegion={addRegion}
                    clearRegions={clearRegions}
                    deleteRegion={deleteRegion}
                    perimeterRegions={perimeterRegions}
                    selectRegion={selectRegion}
                />
            ) : null}

            <AiConfigActions
                alarmDirty={alarmDirty}
                dirty={dirty}
                draft={draft}
                restoreDraft={restoreDraft}
                saveDraft={saveDraft}
                saveMsg={saveMsg}
                saving={saving}
            />
        </section>
    );
}

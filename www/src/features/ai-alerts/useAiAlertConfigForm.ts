import { useCallback, useEffect, useState } from 'react';
import { saveAiAlarmRule } from '../../api/alarm';
import { saveAiConfig } from '../../api/ai';
import type {
    AiConfig,
    AiModelConfig,
    AiStatus,
    AiTaskName,
    AlarmConfig,
    AlarmRuleConfig,
} from '../../api/types';
import {
    nonNegativeInteger,
    normalizeAiRootConfigForSave,
} from './aiAlertFormat';
import {
    isAiTaskAvailable,
    taskLabel,
    taskUnavailableText,
} from './aiAlertTasks';
import {
    kAlarmDurationOptions,
    kInferenceIntervalOptions,
    kMaxResultsOptions,
    type SensitivityLevel,
} from './aiAlertOptions';
import {
    completeAiConfig,
    numericOptionsWithCurrent,
    sharedHiddenTaskConfig,
    sensitivityForConfig,
    thresholdForSensitivity,
    updateTaskConfig,
    withSharedHiddenDefaults,
} from './aiConfigDraft';

interface AiAlertConfigFormOptions {
    aiStatus: AiStatus | null;
    alarmConfig: AlarmConfig | null;
    refresh: () => Promise<void>;
}

export function useAiAlertConfigForm({
    aiStatus,
    alarmConfig,
    refresh,
}: AiAlertConfigFormOptions) {
    const [draft, setDraft] = useState<AiConfig | null>(null);
    const [alarmRule, setAlarmRule] = useState<AlarmRuleConfig | null>(null);
    const [dirty, setDirty] = useState(false);
    const [alarmDirty, setAlarmDirty] = useState(false);
    const [saving, setSaving] = useState(false);
    const [saveMsg, setSaveMsg] = useState('');

    const aiCapabilities = aiStatus?.capabilities ?? null;
    const sensitivity = sensitivityForConfig(draft, aiCapabilities);
    const sharedConfig = draft
        ? sharedHiddenTaskConfig(draft, aiCapabilities)
        : null;
    const intervalOptions = numericOptionsWithCurrent(
        kInferenceIntervalOptions,
        sharedConfig?.inference_interval_ms ?? 500,
        (value) => `${value} ms`,
    );
    const maxResultsOptions = numericOptionsWithCurrent(
        kMaxResultsOptions,
        sharedConfig?.max_results ?? 16,
        (value) => `${value} 个`,
    );
    const alarmDurationOptions = numericOptionsWithCurrent(
        kAlarmDurationOptions,
        alarmRule?.min_duration_ms ?? 0,
        (value) => `${value} ms`,
    );

    useEffect(() => {
        if (!aiStatus || dirty) {
            return;
        }
        setDraft(completeAiConfig(aiStatus.config, aiStatus.capabilities));
        setSaveMsg('');
    }, [dirty, aiStatus]);

    useEffect(() => {
        if (!alarmConfig || alarmDirty) {
            return;
        }
        setAlarmRule({
            ...alarmConfig.ai_detection,
            regions: [...alarmConfig.ai_detection.regions],
        });
    }, [alarmConfig, alarmDirty]);

    const clearSaveMessage = useCallback(() => {
        setSaveMsg('');
    }, []);

    const updateDraftWith = useCallback(
        (updater: (config: AiConfig) => AiConfig) => {
            setDraft((current) => (current ? updater(current) : current));
            setDirty(true);
            setSaveMsg('');
        },
        [],
    );

    const updateTaskEnabled = (taskName: AiTaskName, enabled: boolean) => {
        if (!isAiTaskAvailable(taskName, aiCapabilities)) {
            setSaveMsg(
                `${taskLabel(taskName)}${taskUnavailableText(
                    taskName,
                    aiCapabilities,
                )}`,
            );
            return;
        }
        updateDraftWith((current) =>
            updateTaskConfig(current, taskName, { enabled }),
        );
    };

    const updateAllTasks = (patch: Partial<AiModelConfig>) => {
        updateDraftWith((current) => ({
            ...current,
            tasks: current.tasks.map((task) => ({
                ...task,
                ...patch,
            })),
        }));
    };

    const updateAlarmRuleWith = (patch: Partial<AlarmRuleConfig>) => {
        setAlarmRule((current) =>
            current ? { ...current, ...patch } : current,
        );
        setAlarmDirty(true);
        setSaveMsg('');
    };

    const updateSensitivity = (nextSensitivity: SensitivityLevel) => {
        updateAllTasks({
            confidence_threshold: thresholdForSensitivity(nextSensitivity),
        });
    };

    const restoreDraft = () => {
        if (!aiStatus) {
            return;
        }
        setDraft(completeAiConfig(aiStatus.config, aiStatus.capabilities));
        setDirty(false);
        if (alarmConfig) {
            setAlarmRule({
                ...alarmConfig.ai_detection,
                regions: [...alarmConfig.ai_detection.regions],
            });
            setAlarmDirty(false);
        }
        setSaveMsg('');
    };

    const saveDraft = () => {
        if (!draft) {
            return;
        }
        setSaving(true);
        setSaveMsg('');
        const nextConfig = normalizeAiRootConfigForSave(
            withSharedHiddenDefaults(draft, aiCapabilities),
            aiCapabilities,
        );
        const requests: Promise<void>[] = [saveAiConfig(nextConfig)];
        const nextAlarmRule = alarmRule
            ? {
                  ...alarmRule,
                  min_duration_ms: nonNegativeInteger(
                      alarmRule.min_duration_ms,
                      0,
                  ),
              }
            : null;
        if (alarmConfig && nextAlarmRule) {
            requests.push(saveAiAlarmRule(alarmConfig, nextAlarmRule));
        }
        void Promise.all(requests)
            .then(refresh)
            .then(() => {
                setDirty(false);
                setAlarmDirty(false);
                setDraft(completeAiConfig(nextConfig, aiCapabilities));
                if (nextAlarmRule) {
                    setAlarmRule(nextAlarmRule);
                }
                setSaveMsg('已保存并应用');
            })
            .catch((err: unknown) => {
                setSaveMsg(err instanceof Error ? err.message : '保存失败');
            })
            .finally(() => setSaving(false));
    };

    return {
        alarmDirty,
        alarmDurationOptions,
        alarmRule,
        clearSaveMessage,
        dirty,
        draft,
        intervalOptions,
        maxResultsOptions,
        restoreDraft,
        saveDraft,
        saveMsg,
        saving,
        sensitivity,
        sharedConfig,
        updateAlarmRuleWith,
        updateAllTasks,
        updateDraftWith,
        updateSensitivity,
        updateTaskEnabled,
    };
}

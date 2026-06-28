import type {
    AiConfig,
    AiModelConfig,
    AlarmRuleConfig,
    StreamName,
} from '../../api/types';
import {
    kAlarmDurationOptions,
    kMaxResultsOptions,
    kSensitivityOptions,
    kStreamOptions,
    type SensitivityLevel,
} from './aiAlertOptions';
import { optionValue } from './aiConfigDraft';
import type {
    NumericOption,
    SharedTaskConfig,
} from './aiEventConfigTypes';

interface AiEventOptionsProps {
    alarmDurationOptions: NumericOption[];
    alarmRule: AlarmRuleConfig | null;
    draft: AiConfig | null;
    intervalOptions: NumericOption[];
    maxResultsOptions: NumericOption[];
    sensitivity: SensitivityLevel;
    sharedConfig: SharedTaskConfig | null;
    updateAlarmRuleWith: (patch: Partial<AlarmRuleConfig>) => void;
    updateAllTasks: (patch: Partial<AiModelConfig>) => void;
    updateSensitivity: (nextSensitivity: SensitivityLevel) => void;
}

export function AiEventOptions({
    alarmDurationOptions,
    alarmRule,
    draft,
    intervalOptions,
    maxResultsOptions,
    sensitivity,
    sharedConfig,
    updateAlarmRuleWith,
    updateAllTasks,
    updateSensitivity,
}: AiEventOptionsProps) {
    return (
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
                            inference_interval_ms: Number(event.target.value),
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
    );
}

import type { AiTaskName, StreamName } from '../../api/types';

export type SensitivityLevel = 'low' | 'medium' | 'high';

export const kTaskOrder: AiTaskName[] = [
    'object_detection',
    'perimeter_detection',
    'motion_classification',
    'occlusion_detection',
];

export const kSensitivityOptions: Array<{
    label: string;
    threshold: number;
    value: SensitivityLevel;
}> = [
    { label: '低', threshold: 0.7, value: 'low' },
    { label: '中', threshold: 0.5, value: 'medium' },
    { label: '高', threshold: 0.35, value: 'high' },
];

export const kStreamOptions: Array<{ label: string; value: StreamName }> = [
    { label: '子码流', value: 'sub' },
    { label: '主码流', value: 'main' },
];

export const kInferenceIntervalOptions = [
    { label: '高频 250 ms', value: 250 },
    { label: '标准 500 ms', value: 500 },
    { label: '低负载 1 s', value: 1000 },
    { label: '巡检 2 s', value: 2000 },
];

export const kMaxResultsOptions = [
    { label: '少量 8 个', value: 8 },
    { label: '标准 16 个', value: 16 },
    { label: '更多 32 个', value: 32 },
];

export const kAlarmDurationOptions = [
    { label: '立即触发', value: 0 },
    { label: '持续 0.5 s', value: 500 },
    { label: '持续 1 s', value: 1000 },
    { label: '持续 3 s', value: 3000 },
    { label: '持续 5 s', value: 5000 },
];

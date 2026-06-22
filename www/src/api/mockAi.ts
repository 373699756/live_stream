import type {
    AiAlertList,
    AiDetection,
    AiModelConfig,
    AiStats,
    AiStatus,
    AiTaskName,
} from './types';

const now = Date.now();

const baseTaskConfig: Omit<AiModelConfig, 'enabled' | 'task' | 'model_path'> = {
    backend: 'hisi3516dv300_nnie',
    stream: 'sub',
    input_width: 300,
    input_height: 300,
    inference_interval_ms: 500,
    confidence_threshold: 0.5,
    max_results: 16,
    perimeter_regions: [],
};

const taskConfig = (
    task: AiTaskName,
    enabled: boolean,
    modelPath = '',
    perimeter_regions: AiModelConfig['perimeter_regions'] = [],
): AiModelConfig => ({
    ...baseTaskConfig,
    enabled,
    task,
    model_path: modelPath,
    perimeter_regions,
});

const personDetections: AiDetection[] = [
    {
        label: 'person',
        confidence: 0.91,
        x: 0.18,
        y: 0.24,
        width: 0.16,
        height: 0.42,
    },
    {
        label: 'vehicle',
        confidence: 0.78,
        x: 0.58,
        y: 0.44,
        width: 0.22,
        height: 0.18,
    },
];

const motionDetections: AiDetection[] = [
    {
        label: 'motion',
        confidence: 0.69,
        x: 0.08,
        y: 0.16,
        width: 0.3,
        height: 0.22,
    },
];

const occlusionDetections: AiDetection[] = [
    {
        label: 'occlusion',
        confidence: 0.88,
        x: 0,
        y: 0,
        width: 1,
        height: 1,
    },
];

const emptyStats = (): AiStats => ({
    enabled: true,
    backend_available: true,
    alarm_linked: true,
    last_success_time_ms: now - 1200,
    last_failure_time_ms: 0,
    received_frames: 2841,
    skipped_frames: 3,
    inference_count: 942,
    inference_failed_count: 1,
    dropped_tasks: 0,
    last_inference_time_ms: 34,
    max_inference_time_ms: 71,
    average_inference_time_ms: 38,
    active_results: 0,
});

const disabledStats = (): AiStats => ({
    ...emptyStats(),
    enabled: false,
    backend_available: false,
    last_success_time_ms: 0,
    received_frames: 0,
    inference_count: 0,
    inference_failed_count: 0,
    active_results: 0,
});

const perimeterConfig = taskConfig(
    'perimeter_detection',
    false,
    'models/inst_ssd_cycle.wk',
    [
        {
            name: 'gate',
            x: 0.5,
            y: 0.25,
            width: 0.42,
            height: 0.55,
        },
    ],
);

const objectConfig = taskConfig(
    'object_detection',
    false,
    'models/inst_ssd_cycle.wk',
);
const motionConfig = taskConfig('motion_classification', true);
const occlusionConfig = taskConfig('occlusion_detection', true);

export const mockAiStatus: AiStatus = {
    enabled: true,
    config: {
        enabled: true,
        tasks: [objectConfig, perimeterConfig, motionConfig, occlusionConfig],
    },
    summary: {
        ...emptyStats(),
        active_results: 1,
        received_frames: 5682,
        inference_count: 1884,
    },
    tasks: [
        {
            config: objectConfig,
            stats: disabledStats(),
            last_result: {
                success: true,
                stream: 'sub',
                sequence: 942,
                pts_us: 190214000,
                detections: personDetections,
            },
        },
        {
            config: perimeterConfig,
            stats: disabledStats(),
            last_result: {
                success: true,
                stream: 'sub',
                sequence: 943,
                pts_us: 190215000,
                detections: personDetections,
            },
        },
        {
            config: motionConfig,
            stats: { ...emptyStats(), active_results: 1 },
            last_result: {
                success: true,
                stream: 'sub',
                sequence: 944,
                pts_us: 190216000,
                detections: motionDetections,
            },
        },
        {
            config: occlusionConfig,
            stats: { ...emptyStats(), active_results: 0 },
            last_result: {
                success: true,
                stream: 'sub',
                sequence: 945,
                pts_us: 190217000,
                detections: [],
            },
        },
    ],
    last_result: {
        success: true,
        stream: 'sub',
        sequence: 944,
        pts_us: 190216000,
        detections: motionDetections,
    },
};

const alertDetections = (task: AiTaskName) => {
    if (task === 'motion_classification') {
        return motionDetections;
    }
    if (task === 'occlusion_detection') {
        return occlusionDetections;
    }
    return personDetections;
};

const alertTaskSequence: AiTaskName[] = [
    'perimeter_detection',
    'object_detection',
    'motion_classification',
    'perimeter_detection',
    'occlusion_detection',
    'object_detection',
    'perimeter_detection',
    'motion_classification',
    'object_detection',
    'perimeter_detection',
    'occlusion_detection',
    'perimeter_detection',
];

export const mockAiAlerts: AiAlertList = {
    items: alertTaskSequence.map((task, index) => {
        const detections = alertDetections(task);
        return {
            id: `mock-${alertTaskSequence.length - index}`,
            timestamp_ms: now - index * 24_000,
            stream: 'sub',
            task,
            image_url: '/snapshot/sub.jpg',
            detection_count: detections.length,
            confidence_max: detections.reduce(
                (max, detection) => Math.max(max, detection.confidence),
                0,
            ),
            detections,
        };
    }),
};

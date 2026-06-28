import type { StreamName } from './core';

export type AiBackendId = 'hisi3516dv300_nnie';

export type AiTaskName =
    | 'object_detection'
    | 'perimeter_detection'
    | 'motion_classification'
    | 'occlusion_detection';

export interface AiTaskCapability {
    task: AiTaskName;
    available: boolean;
    requires_model: boolean;
    unavailable_reason: string;
    default_model_path: string;
    default_input_width: number;
    default_input_height: number;
    min_inference_interval_ms: number;
    max_inference_interval_ms: number;
    default_inference_interval_ms: number;
    min_results: number;
    max_results: number;
    default_max_results: number;
    min_confidence_threshold: number;
    max_confidence_threshold: number;
    default_confidence_threshold: number;
    max_perimeter_regions: number;
    supported_backends: AiBackendId[];
    supported_streams: StreamName[];
}

export interface AiCapabilities {
    available: boolean;
    model_runtime_available: boolean;
    model_runtime_reason: string;
    tasks: AiTaskCapability[];
}

export interface AiPerimeterRegion {
    name: string;
    x: number;
    y: number;
    width: number;
    height: number;
}

export interface AiModelConfig {
    enabled: boolean;
    backend: AiBackendId;
    task: AiTaskName;
    stream: StreamName;
    model_path: string;
    input_width: number;
    input_height: number;
    inference_interval_ms: number;
    confidence_threshold: number;
    max_results: number;
    perimeter_regions: AiPerimeterRegion[];
}

export interface AiConfig {
    enabled: boolean;
    tasks: AiModelConfig[];
}

export interface AiDetection {
    label: string;
    confidence: number;
    x: number;
    y: number;
    width: number;
    height: number;
}

export interface AiStats {
    enabled: boolean;
    backend_available: boolean;
    alarm_linked: boolean;
    last_success_time_ms: number;
    last_failure_time_ms: number;
    received_frames: number;
    skipped_frames: number;
    inferences: number;
    failed_inferences: number;
    dropped_tasks: number;
    last_inference_time_ms: number;
    max_inference_time_ms: number;
    average_inference_time_ms: number;
    active_results: number;
}

export interface AiInferenceResult {
    success: boolean;
    stream: StreamName;
    sequence: number;
    pts_us: number;
    detections: AiDetection[];
}

export interface AiTaskInfo {
    config: AiModelConfig;
    stats: AiStats;
    last_result: AiInferenceResult;
}

export interface AiStatus {
    enabled: boolean;
    config: AiConfig;
    summary: AiStats;
    tasks: AiTaskInfo[];
    last_result: AiInferenceResult;
    capabilities: AiCapabilities;
}

export interface AiAlertRecord {
    id: string;
    timestamp_ms: number;
    stream: StreamName;
    task: AiTaskName;
    image_url: string;
    detected_targets: number;
    confidence_max: number;
    detections: AiDetection[];
}

export interface AiAlertList {
    items: AiAlertRecord[];
}

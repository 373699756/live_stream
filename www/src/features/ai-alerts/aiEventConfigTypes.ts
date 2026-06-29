import type { AiModelConfig, StreamName } from '../../api/types';

export interface NumericOption {
    label: string;
    value: number;
}

export interface SharedTaskConfig {
    backend: AiModelConfig['backend'];
    stream: StreamName;
    model_path: string;
    input_width: number;
    input_height: number;
    inference_interval_ms: number;
    max_results: number;
}

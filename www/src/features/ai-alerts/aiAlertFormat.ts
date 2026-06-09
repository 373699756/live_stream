import type {
  AiAlertRecord,
  AiDetection,
  AiModelConfig,
  AlarmStatusResponse,
  StreamName,
} from '../../api/types';
import type { MediaEvent } from '../../api/mediaEvents';
import { formatTimestamp } from '../../utils/format';

export function clampPercent(value: number) {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(1, Math.max(0, value));
}

export function positiveInteger(value: number, fallback: number) {
  if (!Number.isFinite(value)) {
    return fallback;
  }
  return Math.max(1, Math.round(value));
}

export function nonNegativeInteger(value: number, fallback: number) {
  if (!Number.isFinite(value)) {
    return fallback;
  }
  return Math.max(0, Math.round(value));
}

export function formatPercent(value: number) {
  return `${Math.round(clampPercent(value) * 100)}%`;
}

export function streamLabel(stream: StreamName) {
  return stream === 'main' ? '主码流' : '子码流';
}

export function labelText(detections: AiDetection[]) {
  const labels = detections.map((detection) => detection.label).filter(Boolean);
  if (labels.length === 0) {
    return '-';
  }
  return Array.from(new Set(labels)).join(', ');
}

export function cardTitleText(alert: AiAlertRecord) {
  const labels = labelText(alert.detections);
  if (alert.task === 'perimeter_detection' && labels !== '-') {
    return `周界: ${labels}`;
  }
  return labels;
}

export function alarmSourceLabel(source: string) {
  switch (source) {
    case 'ai_detection':
      return 'AI 告警';
    case 'motion':
    case 'motion_detection':
      return '移动侦测';
    case 'io_input':
      return 'IO 输入';
    case 'tamper':
      return '防拆';
    case 'network':
      return '网络';
    case 'unknown':
      return '--';
    default:
      return source || '--';
  }
}

export function latestTimeText(alerts: AiAlertRecord[]) {
  if (alerts.length === 0) {
    return '--';
  }
  return formatTimestamp(alerts[0].timestamp_ms);
}

export function latestAlarmTimeText(
  alarmStatus: AlarmStatusResponse | null,
  lastAlarmEvent: MediaEvent | null,
) {
  if (lastAlarmEvent?.timestamp_ms) {
    return formatTimestamp(lastAlarmEvent.timestamp_ms);
  }
  const lastTriggerTime = alarmStatus?.status.last_trigger_time_ms ?? 0;
  return lastTriggerTime > 0 ? formatTimestamp(lastTriggerTime) : '--';
}

export function normalizeAiConfigForSave(config: AiModelConfig): AiModelConfig {
  return {
    ...config,
    input_width: positiveInteger(config.input_width, 300),
    input_height: positiveInteger(config.input_height, 300),
    inference_interval_ms: positiveInteger(config.inference_interval_ms, 500),
    confidence_threshold: clampPercent(config.confidence_threshold),
    max_results: positiveInteger(config.max_results, 16),
    perimeter_regions: config.perimeter_regions ?? [],
  };
}

import { requestJson, putJson, type ApiRequestOptions } from './client';
import { mockAlarmConfig, mockAlarmStatus } from './mockAlarm';
import type {
  AlarmConfig,
  AlarmRuntimeStatus,
  AlarmSourceName,
  AlarmSourceState,
  AlarmStatusResponse,
  AlarmRuleConfig,
} from './types';

export function getAlarmConfig(init?: ApiRequestOptions): Promise<AlarmConfig> {
  return requestJson<AlarmConfig>(
    '/api/config/alarm',
    mockAlarmConfig,
    init,
  ).then(normalizeAlarmConfig);
}

export function getAlarmStatus(
  init?: ApiRequestOptions,
): Promise<AlarmStatusResponse> {
  return requestJson<AlarmStatusResponse>(
    '/api/alarm/status',
    mockAlarmStatus,
    init,
  ).then(normalizeAlarmStatusResponse);
}

export function saveAlarmConfig(value: AlarmConfig): Promise<void> {
  return putJson('/api/config/alarm', normalizeAlarmConfig(value));
}

export function saveAiAlarmRule(
  currentConfig: AlarmConfig,
  rule: AlarmRuleConfig,
): Promise<void> {
  return saveAlarmConfig({
    ...currentConfig,
    ai_detection: normalizeAlarmRule(rule, mockAlarmConfig.ai_detection),
  });
}

function normalizeAlarmConfig(config: AlarmConfig): AlarmConfig {
  return {
    ...config,
    motion_detection: normalizeAlarmRule(
      config.motion_detection,
      mockAlarmConfig.motion_detection,
    ),
    ai_detection: normalizeAlarmRule(
      config.ai_detection,
      mockAlarmConfig.ai_detection,
    ),
    actions: {
      ...mockAlarmConfig.actions,
      ...(config.actions ?? {}),
    },
    schedule: {
      ...mockAlarmConfig.schedule,
      ...(config.schedule ?? {}),
      weekly: Array.isArray(config.schedule?.weekly)
        ? config.schedule.weekly
        : [],
    },
  };
}

function normalizeAlarmStatusResponse(
  response: AlarmStatusResponse,
): AlarmStatusResponse {
  return {
    available: response.available === true,
    status: normalizeAlarmRuntimeStatus(response.status),
  };
}

function normalizeAlarmRuntimeStatus(
  status: AlarmRuntimeStatus | undefined,
): AlarmRuntimeStatus {
  return {
    active: status?.active === true,
    source: normalizeAlarmSource(status?.source),
    active_since_ms: nonNegativeNumber(status?.active_since_ms, 0),
    last_trigger_time_ms: nonNegativeNumber(status?.last_trigger_time_ms, 0),
    level: clampNumber(Math.round(finiteNumber(status?.level, 0)), 0, 5),
    message: typeof status?.message === 'string' ? status.message : '',
    sources: Array.isArray(status?.sources)
      ? status.sources.map(normalizeAlarmSourceState)
      : [],
  };
}

function normalizeAlarmSourceState(
  state: AlarmSourceState | undefined,
): AlarmSourceState {
  return {
    source: normalizeAlarmSource(state?.source),
    enabled: state?.enabled === true,
    waiting: state?.waiting === true,
    active: state?.active === true,
    waiting_since_ms: nonNegativeNumber(state?.waiting_since_ms, 0),
    active_since_ms: nonNegativeNumber(state?.active_since_ms, 0),
    last_alarm_time_ms: nonNegativeNumber(state?.last_alarm_time_ms, 0),
    level: clampNumber(Math.round(finiteNumber(state?.level, 0)), 0, 5),
    message: typeof state?.message === 'string' ? state.message : '',
  };
}

function normalizeAlarmRule(
  rule: AlarmRuleConfig | undefined,
  fallback: AlarmRuleConfig,
): AlarmRuleConfig {
  return {
    ...(rule ?? {}),
    enabled: rule?.enabled ?? fallback.enabled,
    sensitivity: clampNumber(
      finiteNumber(rule?.sensitivity, fallback.sensitivity),
      0,
      100,
    ),
    min_duration_ms: Math.max(
      0,
      Math.round(finiteNumber(rule?.min_duration_ms, fallback.min_duration_ms)),
    ),
    repeat_interval_ms: Math.max(
      0,
      Math.round(
        finiteNumber(rule?.repeat_interval_ms, fallback.repeat_interval_ms ?? 0),
      ),
    ),
    manual_clear: rule?.manual_clear ?? fallback.manual_clear ?? false,
    level: clampNumber(
      Math.round(finiteNumber(rule?.level, fallback.level ?? 1)),
      0,
      5,
    ),
    regions: Array.isArray(rule?.regions) ? rule.regions : [],
  };
}

function finiteNumber(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value)
    ? value
    : fallback;
}

function nonNegativeNumber(value: unknown, fallback: number): number {
  return Math.max(0, finiteNumber(value, fallback));
}

function clampNumber(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function normalizeAlarmSource(value: unknown): AlarmSourceName {
  switch (value) {
    case 'motion':
    case 'motion_detection':
      return 'motion';
    case 'ai_detection':
    case 'io_input':
    case 'tamper':
    case 'network':
      return value;
    default:
      return 'unknown';
  }
}

import type { AiStatus, AlarmConfig } from '../../api/types';

export function backendLabel(status: AiStatus) {
  if (!status.config.enabled) {
    return 'AI 未启用';
  }
  return status.stats.backend_available ? '后端可用' : '后端不可用';
}

export function backendBadgeState(status: AiStatus): 'running' | 'pending' | 'error' {
  if (!status.config.enabled) {
    return 'pending';
  }
  return status.stats.backend_available ? 'running' : 'error';
}

export function alarmBadgeLabel(status: AiStatus, alarmConfig: AlarmConfig | null) {
  if (!status.stats.alarm_linked) {
    return 'alarm 未接入';
  }
  if (!alarmConfig) {
    return '读取报警规则';
  }
  return alarmConfig.ai_detection.enabled ? '报警已启用' : '报警未启用';
}

export function alarmBadgeState(
  status: AiStatus,
  alarmConfig: AlarmConfig | null,
): 'running' | 'pending' | 'error' {
  if (!status.stats.alarm_linked) {
    return 'error';
  }
  if (!alarmConfig) {
    return 'pending';
  }
  return alarmConfig.ai_detection.enabled ? 'running' : 'pending';
}

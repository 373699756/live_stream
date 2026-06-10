import type { AiStatus, AlarmConfig } from '../../api/types';

export function backendLabel(status: AiStatus) {
  const hasEnabledTask = status.config.tasks.some((task) => task.enabled);
  if (!hasEnabledTask) {
    return '任务未启用';
  }
  if (!status.summary.enabled) {
    return '未运行';
  }
  return status.summary.backend_available ? '后端可用' : '后端不可用';
}

export function backendBadgeState(status: AiStatus): 'running' | 'pending' | 'error' {
  const hasEnabledTask = status.config.tasks.some((task) => task.enabled);
  if (!hasEnabledTask || !status.summary.enabled) {
    return 'pending';
  }
  return status.summary.backend_available ? 'running' : 'error';
}

export function alarmBadgeLabel(status: AiStatus, alarmConfig: AlarmConfig | null) {
  if (!status.summary.alarm_linked) {
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
  if (!status.summary.alarm_linked) {
    return 'error';
  }
  if (!alarmConfig) {
    return 'pending';
  }
  return alarmConfig.ai_detection.enabled ? 'running' : 'pending';
}

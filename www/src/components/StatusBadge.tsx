interface StatusBadgeProps {
  state: 'running' | 'pending' | 'error' | 'online' | 'offline';
  label?: string;
}

const labels: Record<StatusBadgeProps['state'], string> = {
  running: '运行中',
  pending: '待接入',
  error: '异常',
  online: '在线',
  offline: '离线',
};

export function StatusBadge({ state, label }: StatusBadgeProps) {
  return <span className={`status-badge ${state}`}>{label || labels[state]}</span>;
}

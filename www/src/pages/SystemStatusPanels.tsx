import type { SystemStatus } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';

interface SystemStatusPanelsProps {
  status: SystemStatus;
}

export function SystemStatusPanel({ status }: SystemStatusPanelsProps) {
  return (
    <section className="panel">
      <h2>系统状态</h2>
      <div className="metric-grid">
        <div>
          <span>CPU</span>
          <strong>{status.cpu}%</strong>
        </div>
        <div>
          <span>内存</span>
          <strong>{status.memory}%</strong>
        </div>
        <div>
          <span>温度</span>
          <strong>{status.temperature} C</strong>
        </div>
        <div>
          <span>运行时间</span>
          <strong>{status.uptime}</strong>
        </div>
      </div>
    </section>
  );
}

export function DeviceInfoPanel({ status }: SystemStatusPanelsProps) {
  return (
    <section className="panel">
      <h2>设备信息</h2>
      <div className="info-table">
        <div>
          <span>设备名</span>
          <strong>{status.deviceName}</strong>
        </div>
        <div>
          <span>型号</span>
          <strong>{status.model}</strong>
        </div>
        <div>
          <span>固件版本</span>
          <strong>{status.firmware}</strong>
        </div>
      </div>
    </section>
  );
}

export function ServiceStatusPanel({ status }: SystemStatusPanelsProps) {
  return (
    <section className="panel wide-panel">
      <h2>服务状态</h2>
      <div className="service-list">
        {status.services.map((service) => (
          <div key={service.name}>
            <span>{service.name}</span>
            <StatusBadge state={service.state} />
          </div>
        ))}
      </div>
    </section>
  );
}

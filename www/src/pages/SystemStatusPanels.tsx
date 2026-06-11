import type { SystemStatus } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';

interface SystemStatusPanelsProps {
    status: SystemStatus;
}

const moduleLabels: Record<string, string> = {
    logger: '日志服务',
    config: '配置服务',
    auth: '认证服务',
    system: '系统服务',
    time: '时间同步',
    network_config: '网络配置',
    alarm: '报警服务',
    upgrade: '升级服务',
    rtsp: 'RTSP 服务',
    onvif: 'ONVIF 服务',
    http: 'HTTP 服务',
    device: '视频采集',
    ai: 'AI 分析',
    snapshot: '抓图服务',
    webrtc: 'WebRTC 服务',
    media: '媒体核心',
};

const moduleGroups = [
    {
        title: '基础服务',
        names: ['logger', 'config', 'auth', 'system', 'time', 'network_config'],
    },
    {
        title: '媒体与智能',
        names: ['device', 'media', 'snapshot', 'alarm', 'ai'],
    },
    {
        title: '协议与运维',
        names: ['http', 'rtsp', 'webrtc', 'onvif', 'upgrade'],
    },
];

function moduleLabel(name: string) {
    return moduleLabels[name] || name;
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

export function ModuleStatusPanel({ status }: SystemStatusPanelsProps) {
    const runningCount = status.modules.filter(
        (module) => module.state === 'running',
    ).length;
    const pendingCount = status.modules.filter(
        (module) => module.state === 'pending',
    ).length;
    const errorCount = status.modules.filter(
        (module) => module.state === 'error',
    ).length;
    const groupedNames = new Set(moduleGroups.flatMap((group) => group.names));
    const extraModules = status.modules.filter(
        (module) => !groupedNames.has(module.name),
    );

    return (
        <section className="panel wide-panel">
            <div className="page-heading">
                <div>
                    <h2>模块状态</h2>
                    <p>展示后端模块注册和启动状态，独立于码流运行诊断</p>
                </div>
            </div>

            <div className="module-summary-row">
                <div>
                    <span>总模块</span>
                    <strong>{status.modules.length}</strong>
                </div>
                <div>
                    <span>运行中</span>
                    <strong>{runningCount}</strong>
                </div>
                <div>
                    <span>待接入</span>
                    <strong>{pendingCount}</strong>
                </div>
                <div>
                    <span>异常</span>
                    <strong>{errorCount}</strong>
                </div>
            </div>

            <div className="module-group-grid">
                {moduleGroups.map((group) => {
                    const modules = group.names
                        .map((name) =>
                            status.modules.find(
                                (module) => module.name === name,
                            ),
                        )
                        .filter(
                            (
                                module,
                            ): module is SystemStatus['modules'][number] =>
                                Boolean(module),
                        );
                    if (modules.length === 0) {
                        return null;
                    }
                    return (
                        <div className="module-group" key={group.title}>
                            <div className="panel-title">{group.title}</div>
                            <div className="module-list">
                                {modules.map((module) => (
                                    <div key={module.name}>
                                        <span>
                                            <strong>
                                                {moduleLabel(module.name)}
                                            </strong>
                                            <small>{module.name}</small>
                                        </span>
                                        <StatusBadge state={module.state} />
                                    </div>
                                ))}
                            </div>
                        </div>
                    );
                })}

                {extraModules.length > 0 ? (
                    <div className="module-group">
                        <div className="panel-title">其他模块</div>
                        <div className="module-list">
                            {extraModules.map((module) => (
                                <div key={module.name}>
                                    <span>
                                        <strong>
                                            {moduleLabel(module.name)}
                                        </strong>
                                        <small>{module.name}</small>
                                    </span>
                                    <StatusBadge state={module.state} />
                                </div>
                            ))}
                        </div>
                    </div>
                ) : null}
            </div>
        </section>
    );
}

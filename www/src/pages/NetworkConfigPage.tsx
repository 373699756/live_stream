import { useNetworkConfig } from '../hooks/useNetworkConfig';
import { useWebrtcConfig } from '../hooks/useWebrtcConfig';
import { ConfigActionBar } from '../components/ConfigActionBar';
import { FormField } from '../components/FormField';

export function NetworkConfigPage() {
    const { config, setConfig, save, reset, savedMsg, loading, saving, error } =
        useNetworkConfig();
    const {
        config: webrtcConfig,
        error: webrtcError,
        loading: webrtcLoading,
        msg: webrtcMsg,
        save: saveWebrtc,
        saving: webrtcSaving,
        setConfig: setWebrtcConfig,
    } = useWebrtcConfig();

    if (loading || webrtcLoading) {
        return <div className="panel">加载网络配置...</div>;
    }
    if (!config || !webrtcConfig) {
        return (
            <div className="panel">
                网络配置加载失败：{error || webrtcError || '无可用配置'}
            </div>
        );
    }

    const iceServersText = webrtcConfig.ice_servers
        .map((server) =>
            [server.url, server.username || '', server.credential || ''].join(
                '|',
            ),
        )
        .join('\n');
    const updateIceServers = (value: string) => {
        const iceServers = value
            .split('\n')
            .map((line) => line.trim())
            .filter(Boolean)
            .map((line) => {
                const [url, username = '', credential = ''] = line.split('|');
                return {
                    credential: credential.trim(),
                    url: (url || '').trim(),
                    username: username.trim(),
                };
            });
        setWebrtcConfig({ ...webrtcConfig, ice_servers: iceServers });
    };

    return (
        <div className="network-config-layout">
            <section className="panel settings-column network-settings-panel">
                <div className="page-heading">
                    <div>
                        <h2>网络设置</h2>
                        <p>
                            配置主机名和服务端口，IP 配置由后端 system.network
                            应用。
                        </p>
                    </div>
                </div>
                <div className="form-grid form-grid-single network-basic-form">
                    <FormField label="主机名">
                        <input
                            value={config.hostname}
                            onChange={(e) =>
                                setConfig({
                                    ...config,
                                    hostname: e.target.value,
                                })
                            }
                        />
                    </FormField>
                    {Object.entries(config.ports).map(([key, value]) => (
                        <FormField
                            label={`${key.toUpperCase()} 端口`}
                            key={key}
                        >
                            <input
                                type="number"
                                value={value}
                                onChange={(e) =>
                                    setConfig({
                                        ...config,
                                        ports: {
                                            ...config.ports,
                                            [key]: Number(e.target.value),
                                        },
                                    })
                                }
                            />
                        </FormField>
                    ))}
                </div>
                <ConfigActionBar
                    error={error}
                    message={savedMsg}
                    onReset={reset}
                    onSave={() => void save()}
                    saving={saving}
                />

                <div className="network-section-heading">
                    <h3>WebRTC 外网直连</h3>
                    <p>
                        公网 IP、端口映射或 VPN 场景使用，CGNAT 仍需要 TURN
                        或中继。
                    </p>
                </div>
                <div className="form-grid network-webrtc-form">
                    <FormField label="启用 WebRTC">
                        <input
                            checked={webrtcConfig.enabled}
                            type="checkbox"
                            onChange={(event) =>
                                setWebrtcConfig({
                                    ...webrtcConfig,
                                    enabled: event.target.checked,
                                })
                            }
                        />
                    </FormField>
                    <FormField label="对外 IP">
                        <input
                            value={webrtcConfig.public_ip}
                            placeholder="auto 或公网 IPv4"
                            onChange={(event) =>
                                setWebrtcConfig({
                                    ...webrtcConfig,
                                    public_ip: event.target.value.trim(),
                                })
                            }
                        />
                    </FormField>
                    <FormField label="UDP 起始端口">
                        <input
                            min={1}
                            max={65535}
                            type="number"
                            value={webrtcConfig.local_port_base}
                            onChange={(event) =>
                                setWebrtcConfig({
                                    ...webrtcConfig,
                                    local_port_base: Number(event.target.value),
                                })
                            }
                        />
                    </FormField>
                    <FormField label="最大 Peer">
                        <input
                            min={1}
                            type="number"
                            value={webrtcConfig.max_peers}
                            onChange={(event) =>
                                setWebrtcConfig({
                                    ...webrtcConfig,
                                    max_peers: Number(event.target.value),
                                })
                            }
                        />
                    </FormField>
                    <FormField label="优先 TCP">
                        <input
                            checked={webrtcConfig.prefer_tcp}
                            type="checkbox"
                            onChange={(event) =>
                                setWebrtcConfig({
                                    ...webrtcConfig,
                                    prefer_tcp: event.target.checked,
                                })
                            }
                        />
                    </FormField>
                    <FormField label="ICE Servers">
                        <textarea
                            rows={4}
                            value={iceServersText}
                            placeholder={
                                'stun:stun.example.com:3478\nturn:turn.example.com:3478|user|password'
                            }
                            onChange={(event) =>
                                updateIceServers(event.target.value)
                            }
                        />
                    </FormField>
                </div>
                <div className="status-note network-port-note">
                    <strong>端口映射</strong>
                    <span>
                        需要开放 UDP {webrtcConfig.local_port_base}-
                        {webrtcConfig.local_port_base +
                            Math.max(webrtcConfig.max_peers - 1, 0)}
                        ，HTTP/HTTPS 管理口也必须外网可访问。
                    </span>
                </div>
                <ConfigActionBar
                    error={webrtcError}
                    message={webrtcMsg}
                    onSave={() => void saveWebrtc()}
                    saveLabel="保存 WebRTC"
                    saving={webrtcSaving}
                />
            </section>
        </div>
    );
}

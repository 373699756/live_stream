import { useNetworkConfig } from '../hooks/useNetworkConfig';
import { useWebrtcConfig } from '../hooks/useWebrtcConfig';
import { FormField } from '../components/FormField';

export function NetworkConfigPage() {
  const { config, setConfig, save, reset, savedMsg, loading, saving, error } = useNetworkConfig();
  const {
    config: webrtcConfig,
    error: webrtcError,
    loading: webrtcLoading,
    message: webrtcMessage,
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
    .map((server) => [
      server.url,
      server.username || '',
      server.credential || '',
    ].join('|'))
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
    <div className="page-grid network-config-grid">
      <section className="panel">
        <div className="page-heading">
          <div>
            <h2>网络设置</h2>
            <p>配置主机名和服务端口，IP 配置由后端 network_config 应用。</p>
          </div>
        </div>
        <div className="form-grid">
          <FormField label="主机名">
            <input value={config.hostname} onChange={(e) => setConfig({ ...config, hostname: e.target.value })} />
          </FormField>
          {Object.entries(config.ports).map(([key, value]) => (
            <FormField label={`${key.toUpperCase()} 端口`} key={key}>
              <input type="number" value={value} onChange={(e) => setConfig({ ...config, ports: { ...config.ports, [key]: Number(e.target.value) } })} />
            </FormField>
          ))}
        </div>
        <div className="form-actions">
          <button type="button" onClick={reset}>恢复默认</button>
          <button type="button" className="primary" disabled={saving} onClick={() => void save()}>
            {saving ? '保存中' : '保存'}
          </button>
        </div>
        {savedMsg && <div className="save-hint">{savedMsg}</div>}
        {error && <div className="status-note error-note">{error}</div>}
      </section>

      <section className="panel">
        <div className="page-heading">
          <div>
            <h2>WebRTC 外网直连</h2>
            <p>公网 IP、端口映射或 VPN 场景使用，CGNAT 仍需要 TURN 或中继。</p>
          </div>
        </div>
        <div className="form-grid">
          <FormField label="启用 WebRTC">
            <input
              checked={webrtcConfig.enabled}
              type="checkbox"
              onChange={(event) => setWebrtcConfig({
                ...webrtcConfig,
                enabled: event.target.checked,
              })}
            />
          </FormField>
          <FormField label="对外 IP">
            <input
              value={webrtcConfig.public_ip}
              placeholder="auto 或公网 IPv4"
              onChange={(event) => setWebrtcConfig({
                ...webrtcConfig,
                public_ip: event.target.value.trim(),
              })}
            />
          </FormField>
          <FormField label="UDP 起始端口">
            <input
              min={1}
              max={65535}
              type="number"
              value={webrtcConfig.local_port_base}
              onChange={(event) => setWebrtcConfig({
                ...webrtcConfig,
                local_port_base: Number(event.target.value),
              })}
            />
          </FormField>
          <FormField label="最大 Peer">
            <input
              min={1}
              type="number"
              value={webrtcConfig.max_peers}
              onChange={(event) => setWebrtcConfig({
                ...webrtcConfig,
                max_peers: Number(event.target.value),
              })}
            />
          </FormField>
          <FormField label="优先 TCP">
            <input
              checked={webrtcConfig.prefer_tcp}
              type="checkbox"
              onChange={(event) => setWebrtcConfig({
                ...webrtcConfig,
                prefer_tcp: event.target.checked,
              })}
            />
          </FormField>
          <FormField label="ICE Servers">
            <textarea
              rows={4}
              value={iceServersText}
              placeholder={'stun:stun.example.com:3478\nturn:turn.example.com:3478|user|password'}
              onChange={(event) => updateIceServers(event.target.value)}
            />
          </FormField>
        </div>
        <div className="status-note">
          端口映射需要开放 UDP {webrtcConfig.local_port_base}-
          {webrtcConfig.local_port_base + Math.max(webrtcConfig.max_peers - 1, 0)}
          ；HTTP/HTTPS 管理口也必须外网可访问。
        </div>
        <div className="form-actions">
          <button
            type="button"
            className="primary"
            disabled={webrtcSaving}
            onClick={() => void saveWebrtc()}
          >
            {webrtcSaving ? '保存中' : '保存 WebRTC'}
          </button>
        </div>
        {webrtcMessage && <div className="save-hint">{webrtcMessage}</div>}
        {webrtcError && <div className="status-note error-note">{webrtcError}</div>}
      </section>
    </div>
  );
}

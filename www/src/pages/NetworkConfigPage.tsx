import { useNetworkConfig } from '../hooks/useNetworkConfig';
import { FormField } from '../components/FormField';

export function NetworkConfigPage() {
  const { config, setConfig, save, reset, savedMsg, loading, saving, error } = useNetworkConfig();

  if (loading) {
    return <div className="panel">加载网络配置...</div>;
  }
  if (!config) {
    return <div className="panel">网络配置加载失败：{error || '无可用配置'}</div>;
  }

  return (
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
  );
}

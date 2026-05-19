import { useOsdConfig } from '../hooks/useOsdConfig';
import { FormField } from '../components/FormField';

export function OsdConfigPage() {
  const { config, setConfig, save, reset, savedMsg, loading, saving, error } = useOsdConfig();

  if (loading) {
    return <div className="panel">加载 OSD 配置...</div>;
  }
  if (!config) {
    return <div className="panel">OSD 配置加载失败：{error || '无可用配置'}</div>;
  }

  return (
    <div className="page-grid osd-grid">
      <section className="panel">
        <div className="page-heading">
          <div>
            <h2>OSD 设置</h2>
            <p>Web 侧编辑业务配置，后端将文字渲染为 SDK RGN bitmap。</p>
          </div>
        </div>
        <div className="form-grid">
          <FormField label="启用 OSD">
            <input type="checkbox" checked={config.enabled} onChange={(e) => setConfig({ ...config, enabled: e.target.checked })} />
          </FormField>
          <FormField label="时间水印">
            <input type="checkbox" checked={config.items.timestamp.enabled} onChange={(e) => setConfig({ ...config, items: { ...config.items, timestamp: { ...config.items.timestamp, enabled: e.target.checked } } })} />
          </FormField>
          <FormField label="时间格式">
            <input value={config.items.timestamp.format} onChange={(e) => setConfig({ ...config, items: { ...config.items, timestamp: { ...config.items.timestamp, format: e.target.value } } })} />
          </FormField>
          <FormField label="设备名称">
            <input value={config.items.device_name.text} onChange={(e) => setConfig({ ...config, items: { ...config.items, device_name: { ...config.items.device_name, text: e.target.value } } })} />
          </FormField>
          <FormField label="字体大小">
            <input type="number" value={config.font_size} onChange={(e) => setConfig({ ...config, font_size: Number(e.target.value) })} />
          </FormField>
          <FormField label="字体颜色">
            <input type="color" value={config.font_color} onChange={(e) => setConfig({ ...config, font_color: e.target.value })} />
          </FormField>
          <FormField label="背景">
            <input type="checkbox" checked={config.background} onChange={(e) => setConfig({ ...config, background: e.target.checked })} />
          </FormField>
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
        <div className="panel-title">叠加预览</div>
        <div className="osd-preview">
          {config.enabled && config.items.timestamp.enabled && (
            <span className={config.background ? 'osd-text background' : 'osd-text'} style={{ color: config.font_color, left: config.items.timestamp.x, top: config.items.timestamp.y }}>
              2026-04-26 10:30:12
            </span>
          )}
          {config.enabled && config.items.device_name.enabled && (
            <span className={config.background ? 'osd-text background' : 'osd-text'} style={{ color: config.font_color, left: config.items.device_name.x, top: config.items.device_name.y + 32 }}>
              {config.items.device_name.text}
            </span>
          )}
        </div>
      </section>
    </div>
  );
}

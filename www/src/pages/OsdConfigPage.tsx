import { useEffect, useState } from 'react';
import { api } from '../api/client';
import { cloneDefaultConfig, mockOsdConfig } from '../api/mock';
import type { OsdConfig } from '../api/types';
import { FormField } from '../components/FormField';

export function OsdConfigPage() {
  const [config, setConfig] = useState<OsdConfig | null>(null);
  const [saved, setSaved] = useState('');

  useEffect(() => {
    void api.getOsdConfig().then(setConfig);
  }, []);

  if (!config) {
    return <div className="panel">加载 OSD 配置...</div>;
  }

  const resetDefault = () => {
    setConfig(cloneDefaultConfig(mockOsdConfig));
    setSaved('已恢复默认值，保存后生效');
  };

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
          <button type="button" onClick={resetDefault}>恢复默认</button>
          <button type="button" className="primary" onClick={() => void api.saveOsdConfig(config).then((ok) => setSaved(ok ? '已提交保存' : '后端未连接，已保留本地修改'))}>保存</button>
        </div>
        {saved && <div className="save-hint">{saved}</div>}
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

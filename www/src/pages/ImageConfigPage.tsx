import { useEffect, useState } from 'react';
import { api } from '../api/client';
import { cloneDefaultConfig, mockImageConfig } from '../api/mock';
import type { ImageConfig, StreamName, StreamStatus } from '../api/types';
import { FormField } from '../components/FormField';
import { VideoPreview } from '../components/VideoPreview';

type ImageRecordSection = 'exposure' | 'white_balance' | 'enhancement' | 'backlight';

const basicItems = [
  ['brightness', '亮度'],
  ['contrast', '对比度'],
  ['saturation', '饱和度'],
  ['sharpness', '锐度'],
  ['hue', '色调'],
] as const;

function numberValue(record: Record<string, unknown>, key: string, fallback: number): number {
  const value = record[key];
  return typeof value === 'number' ? value : fallback;
}

function stringValue(record: Record<string, unknown>, key: string, fallback: string): string {
  const value = record[key];
  return typeof value === 'string' ? value : fallback;
}

function boolValue(record: Record<string, unknown>, key: string, fallback: boolean): boolean {
  const value = record[key];
  return typeof value === 'boolean' ? value : fallback;
}

function RangeField({
  label,
  value,
  onChange,
}: {
  label: string;
  value: number;
  onChange: (value: number) => void;
}) {
  return (
    <FormField label={label}>
      <input type="range" min="0" max="100" value={value} onChange={(e) => onChange(Number(e.target.value))} />
      <span className="range-value">{value}</span>
    </FormField>
  );
}

export function ImageConfigPage() {
  const [config, setConfig] = useState<ImageConfig | null>(null);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [previewStream, setPreviewStream] = useState<StreamName>('main');
  const [saved, setSaved] = useState('');

  useEffect(() => {
    void api.getImageConfig().then(setConfig);
    void api.getStreamStatus().then(setStatuses);
  }, []);

  if (!config) {
    return <div className="panel">加载图像配置...</div>;
  }

  const updateBasic = (key: string, value: number) => {
    setConfig({ ...config, basic: { ...config.basic, [key]: value } });
  };
  const updateSection = (section: ImageRecordSection, key: string, value: unknown) => {
    setConfig({ ...config, [section]: { ...config[section], [key]: value } });
  };
  const updateColorMode = (mode: string) => {
    setConfig({ ...config, color_mode: { ...config.color_mode, mode } });
  };
  const resetDefault = () => {
    setConfig(cloneDefaultConfig(mockImageConfig));
    setSaved('已恢复默认值，保存后生效');
  };

  return (
    <div className="config-preview-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>图像参数</h2>
            <p>调整基础画质参数，运行态应用由后端图像管线完成。</p>
          </div>
        </div>
        <div className="form-grid form-grid-single">
          <div className="form-section-title">基础画质</div>
          {basicItems.map(([key, label]) => (
            <RangeField label={label} value={config.basic[key] ?? 50} onChange={(value) => updateBasic(key, value)} key={key} />
          ))}

          <div className="form-section-title">曝光控制</div>
          <FormField label="曝光模式">
            <select value={stringValue(config.exposure, 'mode', 'auto')} onChange={(e) => updateSection('exposure', 'mode', e.target.value)}>
              <option value="auto">自动</option>
              <option value="manual">手动</option>
            </select>
          </FormField>
          <FormField label="防闪烁">
            <select value={stringValue(config.exposure, 'anti_flicker', '50hz')} onChange={(e) => updateSection('exposure', 'anti_flicker', e.target.value)}>
              <option value="50hz">50Hz</option>
              <option value="60hz">60Hz</option>
              <option value="off">关闭</option>
            </select>
          </FormField>
          <FormField label="曝光时间">
            <select value={stringValue(config.exposure, 'exposure_time', 'auto')} onChange={(e) => updateSection('exposure', 'exposure_time', e.target.value)}>
              <option value="auto">自动</option>
              <option value="1/25">1/25</option>
              <option value="1/50">1/50</option>
              <option value="1/100">1/100</option>
              <option value="1/250">1/250</option>
            </select>
          </FormField>
          <FormField label="增益">
            <select value={stringValue(config.exposure, 'gain', 'auto')} onChange={(e) => updateSection('exposure', 'gain', e.target.value)}>
              <option value="auto">自动</option>
              <option value="low">低</option>
              <option value="medium">中</option>
              <option value="high">高</option>
            </select>
          </FormField>
          <RangeField label="曝光补偿" value={numberValue(config.exposure, 'compensation', 50)} onChange={(value) => updateSection('exposure', 'compensation', value)} />
          <FormField label="慢快门">
            <input type="checkbox" checked={boolValue(config.exposure, 'slow_shutter', true)} onChange={(e) => updateSection('exposure', 'slow_shutter', e.target.checked)} />
          </FormField>
          <FormField label="最长曝光">
            <select value={stringValue(config.exposure, 'max_exposure_time', '1/25')} onChange={(e) => updateSection('exposure', 'max_exposure_time', e.target.value)}>
              <option value="1/12">1/12</option>
              <option value="1/25">1/25</option>
              <option value="1/50">1/50</option>
            </select>
          </FormField>

          <div className="form-section-title">白平衡</div>
          <FormField label="白平衡模式">
            <select value={stringValue(config.white_balance, 'mode', 'auto')} onChange={(e) => updateSection('white_balance', 'mode', e.target.value)}>
              <option value="auto">自动</option>
              <option value="manual">手动</option>
              <option value="indoor">室内</option>
              <option value="outdoor">室外</option>
            </select>
          </FormField>
          <RangeField label="红色增益" value={numberValue(config.white_balance, 'red_gain', 50)} onChange={(value) => updateSection('white_balance', 'red_gain', value)} />
          <RangeField label="蓝色增益" value={numberValue(config.white_balance, 'blue_gain', 50)} onChange={(value) => updateSection('white_balance', 'blue_gain', value)} />

          <div className="form-section-title">图像增强</div>
          <RangeField label="2D 降噪" value={numberValue(config.enhancement, 'denoise_2d', 50)} onChange={(value) => updateSection('enhancement', 'denoise_2d', value)} />
          <RangeField label="3D 降噪" value={numberValue(config.enhancement, 'denoise_3d', 50)} onChange={(value) => updateSection('enhancement', 'denoise_3d', value)} />
          <RangeField label="Gamma" value={numberValue(config.enhancement, 'gamma', 50)} onChange={(value) => updateSection('enhancement', 'gamma', value)} />
          <FormField label="透雾">
            <input type="checkbox" checked={boolValue(config.enhancement, 'defog', false)} onChange={(e) => updateSection('enhancement', 'defog', e.target.checked)} />
          </FormField>

          <div className="form-section-title">背光与日夜</div>
          <FormField label="背光模式">
            <select value={stringValue(config.backlight, 'mode', 'off')} onChange={(e) => updateSection('backlight', 'mode', e.target.value)}>
              <option value="off">关闭</option>
              <option value="wdr">宽动态</option>
              <option value="blc">背光补偿</option>
              <option value="hlc">强光抑制</option>
            </select>
          </FormField>
          <RangeField label="背光等级" value={numberValue(config.backlight, 'level', 50)} onChange={(value) => updateSection('backlight', 'level', value)} />
          <FormField label="日夜模式">
            <select value={config.color_mode.mode} onChange={(e) => updateColorMode(e.target.value)}>
              <option value="color">彩色</option>
              <option value="black_white">黑白</option>
              <option value="auto">自动</option>
            </select>
          </FormField>

          <div className="form-section-title">方向</div>
          <FormField label="镜像">
            <input type="checkbox" checked={config.orientation.mirror} onChange={(e) => setConfig({ ...config, orientation: { ...config.orientation, mirror: e.target.checked } })} />
          </FormField>
          <FormField label="翻转">
            <input type="checkbox" checked={config.orientation.flip} onChange={(e) => setConfig({ ...config, orientation: { ...config.orientation, flip: e.target.checked } })} />
          </FormField>
        </div>
        <div className="form-actions">
          <button type="button" onClick={resetDefault}>恢复默认</button>
          <button type="button" className="primary" onClick={() => void api.saveImageConfig(config).then((ok) => setSaved(ok ? '已提交保存' : '后端未连接，已保留本地修改'))}>保存</button>
        </div>
        {saved && <div className="save-hint">{saved}</div>}
      </section>
      <VideoPreview stream={previewStream} statuses={statuses} onStreamChange={setPreviewStream} />
    </div>
  );
}

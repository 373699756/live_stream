import { useState } from 'react';
import { useImageConfig } from '../hooks/useImageConfig';
import type {
  ImageCapabilities,
  NumericControlCapability,
  OptionControlCapability,
  StreamName,
} from '../api/types';
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

const optionLabels: Record<string, string> = {
  auto: '自动',
  manual: '手动',
  '50hz': '50Hz',
  '60hz': '60Hz',
  off: '关闭',
  low: '低',
  medium: '中',
  high: '高',
  indoor: '室内',
  outdoor: '室外',
  wdr: '宽动态',
  blc: '背光补偿',
  hlc: '强光抑制',
  color: '彩色',
  black_white: '黑白',
};

const tierLabels: Record<string, string> = {
  day: '日间',
  indoor: '室内',
  low_light: '弱光',
  very_low_light: '极弱光',
};

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

function optionLabel(value: string): string {
  return optionLabels[value] || value;
}

function numericCapability(
  controls: Record<string, NumericControlCapability>,
  key: string,
): NumericControlCapability | undefined {
  return controls[key];
}

function optionCapability(
  controls: Record<string, OptionControlCapability>,
  key: string,
  fallback: string[],
): OptionControlCapability {
  return controls[key] || { values: fallback, default: fallback[0] || '' };
}

function supportsOptionValue(
  controls: Record<string, OptionControlCapability>,
  key: string,
  value: string,
): boolean {
  const capability = controls[key];
  return capability ? capability.values.includes(value) : true;
}

function RangeField({
  label,
  capability,
  value,
  onChange,
}: {
  label: string;
  capability: NumericControlCapability;
  value: number;
  onChange: (value: number) => void;
}) {
  return (
    <FormField label={label}>
      <input
        type="range"
        min={capability.min}
        max={capability.max}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
      />
      <span className="range-value">{value}</span>
    </FormField>
  );
}

function OptionField({
  label,
  capability,
  value,
  onChange,
}: {
  label: string;
  capability: OptionControlCapability;
  value: string;
  onChange: (value: string) => void;
}) {
  return (
    <FormField label={label}>
      <select value={value} onChange={(e) => onChange(e.target.value)}>
        {capability.values.map((item) => (
          <option value={item} key={item}>
            {optionLabel(item)}
          </option>
        ))}
      </select>
    </FormField>
  );
}

function tierLabel(value: string): string {
  return tierLabels[value] || value || '-';
}

export function ImageConfigPage() {
  const {
    config,
    setConfig,
    capabilities: mediaCapabilities,
    statuses,
    strategyStatus,
    save,
    reset,
    savedMsg,
    loading,
    saving,
    error,
  } = useImageConfig();
  const capabilities: ImageCapabilities = mediaCapabilities.image;
  const [previewStream, setPreviewStream] = useState<StreamName>('sub');

  if (loading) {
    return <div className="panel">加载图像配置...</div>;
  }
  if (!config) {
    return <div className="panel">图像配置加载失败：{error || '无可用配置'}</div>;
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
  const updateStrategyEnabled = (enabled: boolean) => {
    setConfig({
      ...config,
      strategy: { enabled, mode: config.strategy?.mode || 'balanced' },
    });
  };
  const strategyEnabled = config.strategy?.enabled ?? true;

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
          <div className="form-section-title">自动画质策略</div>
          <FormField label="自动策略">
            <input
              type="checkbox"
              checked={strategyEnabled}
              onChange={(event) => updateStrategyEnabled(event.target.checked)}
            />
          </FormField>
          <div className="strategy-status">
            <span>状态 {strategyStatus.active ? '运行中' : '未运行'}</span>
            <span>ISO {strategyStatus.exposure_valid ? strategyStatus.iso : '-'}</span>
            <span>曝光 {strategyStatus.exposure_valid ? `${strategyStatus.exposure_time_us} us` : '-'}</span>
            <span>场景 {tierLabel(strategyStatus.tier)}</span>
            <span>实际 饱和 {strategyStatus.saturation}</span>
            <span>锐度 {strategyStatus.sharpness}</span>
            <span>2DNR {strategyStatus.denoise_2d}</span>
            <span>3DNR {strategyStatus.denoise_3d}</span>
            <span>Gamma {strategyStatus.gamma}</span>
          </div>

          <div className="form-section-title">基础画质</div>
          {basicItems.map(([key, label]) => {
            const capability = numericCapability(capabilities.basic, key);
            return capability && capability.runtime_supported !== false ? (
              <RangeField
                label={label}
                capability={capability}
                value={config.basic[key] ?? capability.default}
                onChange={(value) => updateBasic(key, value)}
                key={key}
              />
            ) : null;
          })}

          <div className="form-section-title">曝光控制</div>
          <OptionField
            label="曝光模式"
            capability={optionCapability(capabilities.exposure.options, 'mode', [
              'auto',
              'manual',
            ])}
            value={stringValue(config.exposure, 'mode', 'auto')}
            onChange={(value) => updateSection('exposure', 'mode', value)}
          />
          <OptionField
            label="防闪烁"
            capability={optionCapability(
              capabilities.exposure.options,
              'anti_flicker',
              ['50hz', '60hz', 'off'],
            )}
            value={stringValue(config.exposure, 'anti_flicker', '50hz')}
            onChange={(value) =>
              updateSection('exposure', 'anti_flicker', value)
            }
          />
          <OptionField
            label="曝光时间"
            capability={optionCapability(
              capabilities.exposure.options,
              'exposure_time',
              ['auto', '1/25', '1/50', '1/100', '1/250'],
            )}
            value={stringValue(config.exposure, 'exposure_time', 'auto')}
            onChange={(value) =>
              updateSection('exposure', 'exposure_time', value)
            }
          />
          <OptionField
            label="增益"
            capability={optionCapability(capabilities.exposure.options, 'gain', [
              'auto',
              'low',
              'medium',
              'high',
            ])}
            value={stringValue(config.exposure, 'gain', 'auto')}
            onChange={(value) => updateSection('exposure', 'gain', value)}
          />
          <RangeField
            label="曝光补偿"
            capability={
              numericCapability(capabilities.exposure.ranges, 'compensation') || {
                min: 0,
                max: 100,
                default: 50,
              }
            }
            value={numberValue(config.exposure, 'compensation', 50)}
            onChange={(value) =>
              updateSection('exposure', 'compensation', value)
            }
          />
          {supportsOptionValue(
            capabilities.exposure.options,
            'slow_shutter',
            'true',
          ) && (
            <FormField label="慢快门">
              <input
                type="checkbox"
                checked={boolValue(config.exposure, 'slow_shutter', true)}
                onChange={(e) =>
                  updateSection('exposure', 'slow_shutter', e.target.checked)
                }
              />
            </FormField>
          )}
          {capabilities.exposure.options.max_exposure_time && (
            <OptionField
              label="最长曝光"
              capability={capabilities.exposure.options.max_exposure_time}
              value={stringValue(config.exposure, 'max_exposure_time', '1/25')}
              onChange={(value) =>
                updateSection('exposure', 'max_exposure_time', value)
              }
            />
          )}

          <div className="form-section-title">白平衡</div>
          <OptionField
            label="白平衡模式"
            capability={optionCapability(
              capabilities.white_balance.options,
              'mode',
              ['auto', 'manual', 'indoor', 'outdoor'],
            )}
            value={stringValue(config.white_balance, 'mode', 'auto')}
            onChange={(value) => updateSection('white_balance', 'mode', value)}
          />
          <RangeField
            label="红色增益"
            capability={
              numericCapability(capabilities.white_balance.ranges, 'red_gain') || {
                min: 0,
                max: 100,
                default: 50,
              }
            }
            value={numberValue(config.white_balance, 'red_gain', 50)}
            onChange={(value) =>
              updateSection('white_balance', 'red_gain', value)
            }
          />
          <RangeField
            label="蓝色增益"
            capability={
              numericCapability(capabilities.white_balance.ranges, 'blue_gain') || {
                min: 0,
                max: 100,
                default: 50,
              }
            }
            value={numberValue(config.white_balance, 'blue_gain', 45)}
            onChange={(value) =>
              updateSection('white_balance', 'blue_gain', value)
            }
          />

          <div className="form-section-title">图像增强</div>
          <RangeField
            label="2D 降噪"
            capability={
              numericCapability(capabilities.enhancement.ranges, 'denoise_2d') || {
                min: 0,
                max: 100,
                default: 50,
              }
            }
            value={numberValue(config.enhancement, 'denoise_2d', 50)}
            onChange={(value) =>
              updateSection('enhancement', 'denoise_2d', value)
            }
          />
          <RangeField
            label="3D 降噪"
            capability={
              numericCapability(capabilities.enhancement.ranges, 'denoise_3d') || {
                min: 0,
                max: 100,
                default: 50,
              }
            }
            value={numberValue(config.enhancement, 'denoise_3d', 50)}
            onChange={(value) =>
              updateSection('enhancement', 'denoise_3d', value)
            }
          />
          <RangeField
            label="Gamma"
            capability={
              numericCapability(capabilities.enhancement.ranges, 'gamma') || {
                min: 0,
                max: 100,
                default: 50,
              }
            }
            value={numberValue(config.enhancement, 'gamma', 50)}
            onChange={(value) => updateSection('enhancement', 'gamma', value)}
          />
          {supportsOptionValue(
            capabilities.enhancement.options,
            'defog',
            'true',
          ) && (
            <FormField label="透雾">
              <input
                type="checkbox"
                checked={boolValue(config.enhancement, 'defog', false)}
                onChange={(e) =>
                  updateSection('enhancement', 'defog', e.target.checked)
                }
              />
            </FormField>
          )}

          <div className="form-section-title">背光与日夜</div>
          <OptionField
            label="背光模式"
            capability={optionCapability(capabilities.backlight.options, 'mode', [
              'off',
              'wdr',
              'blc',
              'hlc',
            ])}
            value={stringValue(config.backlight, 'mode', 'off')}
            onChange={(value) => updateSection('backlight', 'mode', value)}
          />
          <RangeField
            label="背光等级"
            capability={
              numericCapability(capabilities.backlight.ranges, 'level') || {
                min: 0,
                max: 100,
                default: 50,
              }
            }
            value={numberValue(config.backlight, 'level', 50)}
            onChange={(value) => updateSection('backlight', 'level', value)}
          />
          {capabilities.color_mode.mode &&
            capabilities.color_mode.mode.runtime_supported !== false && (
              <OptionField
                label="日夜模式"
                capability={optionCapability(capabilities.color_mode, 'mode', [
                  'color',
                  'black_white',
                  'auto',
                ])}
                value={config.color_mode.mode || capabilities.color_mode.mode.default}
                onChange={updateColorMode}
              />
            )}

          <div className="form-section-title">方向</div>
          {capabilities.orientation.mirror && (
            <FormField label="镜像">
              <input type="checkbox" checked={config.orientation.mirror} onChange={(e) => setConfig({ ...config, orientation: { ...config.orientation, mirror: e.target.checked } })} />
            </FormField>
          )}
          {capabilities.orientation.flip && (
            <FormField label="翻转">
              <input type="checkbox" checked={config.orientation.flip} onChange={(e) => setConfig({ ...config, orientation: { ...config.orientation, flip: e.target.checked } })} />
            </FormField>
          )}
        </div>
        <div className="form-actions">
          <button type="button" onClick={reset}>恢复默认</button>
          <button
            type="button"
            className="primary"
            disabled={saving}
            onClick={() => void save(config)}
          >
            {saving ? '保存中' : '保存'}
          </button>
        </div>
        {savedMsg && <div className="save-hint">{savedMsg}</div>}
        {error && <div className="status-note error-note">{error}</div>}
      </section>
      <VideoPreview stream={previewStream} statuses={statuses} onStreamChange={setPreviewStream} />
    </div>
  );
}

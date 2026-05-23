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
  drc: '动态范围压缩',
  color: '彩色',
  black_white: '黑白',
};

const tierLabels: Record<string, string> = {
  day: '日间',
  indoor: '室内',
  low_light: '弱光',
  very_low_light: '极弱光',
};

const strategyModeLabels: Record<string, string> = {
  balanced: '均衡',
  low_noise: '低噪声',
  detail: '细节优先',
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

function strategyModeLabel(value: string): string {
  return strategyModeLabels[value] || value || '-';
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
  const updateStrategyMode = (mode: string) => {
    setConfig({
      ...config,
      strategy: {
        enabled: strategyEnabled,
        mode: mode as NonNullable<typeof config.strategy>['mode'],
      },
    });
  };
  const showOrientationControls =
    capabilities.orientation.mirror || capabilities.orientation.flip;

  return (
    <div className="config-preview-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>图像参数</h2>
            <p>调整基础画质参数，运行态应用由后端图像管线完成。</p>
          </div>
        </div>
        <div className="form-grid image-settings-grid image-primary-grid">
          <div className="form-section-title">自动画质策略</div>
          <FormField label="自动策略">
            <input
              type="checkbox"
              checked={strategyEnabled}
              onChange={(event) => updateStrategyEnabled(event.target.checked)}
            />
          </FormField>
          <OptionField
            label="策略模式"
            capability={{
              values: ['balanced', 'low_noise', 'detail'],
              default: 'balanced',
            }}
            value={config.strategy?.mode || 'balanced'}
            onChange={updateStrategyMode}
          />
          <div className="strategy-status image-status-strip">
            <span><strong>状态</strong>{strategyStatus.active ? '运行中' : '未运行'}</span>
            <span><strong>场景</strong>{tierLabel(strategyStatus.tier)}</span>
            <span><strong>模式</strong>{strategyModeLabel(strategyStatus.mode)}</span>
            <span><strong>ISO</strong>{strategyStatus.exposure_valid ? strategyStatus.iso : '-'}</span>
            <span>
              <strong>曝光</strong>
              {strategyStatus.exposure_valid ? `${strategyStatus.exposure_time_us} us` : '-'}
            </span>
          </div>
          <details className="image-status-details">
            <summary>更多运行值</summary>
            <div className="strategy-status image-status-grid">
              <span><strong>饱和</strong>{strategyStatus.saturation}</span>
              <span><strong>锐度</strong>{strategyStatus.sharpness}</span>
              <span><strong>2DNR</strong>{strategyStatus.denoise_2d}</span>
              <span><strong>3DNR</strong>{strategyStatus.denoise_3d}</span>
              <span><strong>Gamma</strong>{strategyStatus.gamma}</span>
            </div>
          </details>

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
        </div>

        <div className="image-advanced-list">
          <details className="image-advanced-section">
            <summary>曝光控制</summary>
            <div className="form-grid image-settings-grid image-detail-grid">
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
                checked={boolValue(config.exposure, 'slow_shutter', false)}
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
            </div>
          </details>

          <details className="image-advanced-section">
            <summary>白平衡</summary>
            <div className="form-grid image-settings-grid image-detail-grid">
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
            </div>
          </details>

          <details className="image-advanced-section">
            <summary>图像增强</summary>
            <div className="form-grid image-settings-grid image-detail-grid">
          <RangeField
            label="2D 降噪"
            capability={
              numericCapability(capabilities.enhancement.ranges, 'denoise_2d') || {
                min: 0,
                max: 100,
                default: 60,
              }
            }
            value={numberValue(config.enhancement, 'denoise_2d', 60)}
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
                default: 52,
              }
            }
            value={numberValue(config.enhancement, 'denoise_3d', 52)}
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
            </div>
          </details>

          <details className="image-advanced-section">
            <summary>背光与日夜</summary>
            <div className="form-grid image-settings-grid image-detail-grid">
          <OptionField
            label="背光模式"
            capability={optionCapability(capabilities.backlight.options, 'mode', [
              'off',
              'drc',
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
            </div>
          </details>

          {showOrientationControls && (
            <details className="image-advanced-section">
              <summary>方向</summary>
              <div className="form-grid image-settings-grid image-detail-grid">
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
            </details>
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

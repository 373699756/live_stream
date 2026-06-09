import { useState } from 'react';
import { useImageConfig } from '../hooks/useImageConfig';
import type {
  ImageCapabilities,
  StreamName,
} from '../api/types';
import { VideoPreview } from '../components/VideoPreview';
import { ImageAdvancedSettings } from './ImageAdvancedSettings';
import { ImagePrimarySettings } from './ImagePrimarySettings';

type ImageRecordSection = 'exposure' | 'white_balance' | 'enhancement' | 'backlight';

export function ImageConfigPage() {
  const [previewStream, setPreviewStream] = useState<StreamName>('main');
  const {
    config,
    setConfig,
    capabilities: mediaCapabilities,
    statuses,
    playbackUrls,
    strategyStatus,
    save,
    reset,
    savedMsg,
    loading,
    saving,
    error,
  } = useImageConfig(previewStream);
  const capabilities: ImageCapabilities = mediaCapabilities.image;

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
      strategy: { enabled, mode: config.strategy?.mode || 'low_noise' },
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

  return (
    <div className="config-preview-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>图像参数</h2>
            <p>调整基础画质参数，运行态应用由后端图像管线完成。</p>
          </div>
        </div>
        <ImagePrimarySettings
          capabilities={capabilities}
          config={config}
          onBasicChange={updateBasic}
          onStrategyEnabledChange={updateStrategyEnabled}
          onStrategyModeChange={updateStrategyMode}
          strategyEnabled={strategyEnabled}
          strategyStatus={strategyStatus}
        />
        <ImageAdvancedSettings
          capabilities={capabilities}
          config={config}
          onColorModeChange={updateColorMode}
          onLensCorrectionChange={(lensCorrection) =>
            setConfig({ ...config, lens_correction: lensCorrection })
          }
          onOrientationChange={(orientation) => setConfig({ ...config, orientation })}
          onSectionChange={updateSection}
        />
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
      <VideoPreview
        stream={previewStream}
        statuses={statuses}
        playbackUrls={playbackUrls}
        onStreamChange={setPreviewStream}
      />
    </div>
  );
}

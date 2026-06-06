import { useState } from 'react';
import type { OverlayConfig, PrivacyMaskConfig, StreamName } from '../api/types';
import { resolutionValue } from '../api/resolution';
import { FormField } from '../components/FormField';
import { VideoPreview } from '../components/VideoPreview';
import { useOverlayConfig } from '../hooks/useOverlayConfig';
import { useVideoConfig } from '../hooks/useVideoConfig';
import { useOverlayMaskEditor } from './OverlayMaskEditor';

const parseResolution = (resolution: string) => {
  const [width, height] = resolution.split('x').map((value) => Number(value));
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
    return { width: 1, height: 1 };
  }
  return { width, height };
};

function updateMask(
  config: OverlayConfig,
  stream: StreamName,
  slot: number,
  patch: Partial<PrivacyMaskConfig>,
): OverlayConfig {
  const masks = config.privacy_masks[stream].map((item, index) =>
    index === slot ? { ...item, ...patch } : item,
  );
  return {
    ...config,
    privacy_masks: {
      ...config.privacy_masks,
      [stream]: masks,
    },
  };
}

export function OverlayConfigPage() {
  const { config, setConfig, save, reset, savedMsg, loading, saving, error } = useOverlayConfig();
  const {
    config: videoConfig,
    capabilities,
    statuses,
    loading: videoLoading,
  } = useVideoConfig();
  const [activeStream, setActiveStream] = useState<StreamName>('sub');
  const [activeSlot, setActiveSlot] = useState(0);

  if (loading || videoLoading) {
    return <div className="panel">加载 Overlay 配置...</div>;
  }
  if (!config) {
    return <div className="panel">Overlay 配置加载失败：{error || '无可用配置'}</div>;
  }

  const streamConfig = videoConfig?.streams[activeStream];
  const capability = capabilities.streams[activeStream];
  const fallbackResolution = capability.resolutions[0]
    ? resolutionValue(capability.resolutions[0])
    : activeStream === 'main'
      ? '1920x1080'
      : '640x360';
  const frame = parseResolution(streamConfig?.resolution || fallbackResolution);
  const activeMasks = config.privacy_masks[activeStream];
  const activeMask = activeMasks[activeSlot];
  const previewStatuses = statuses.map((status) => ({
    ...status,
    resolution: videoConfig?.streams[status.stream]?.resolution || status.resolution,
    fps: videoConfig?.streams[status.stream]?.fps || status.fps,
    bitrateKbps: videoConfig?.streams[status.stream]?.bitrate_kbps || status.bitrateKbps,
  }));

  const setMask = (slot: number, patch: Partial<PrivacyMaskConfig>) => {
    setConfig(updateMask(config, activeStream, slot, patch));
  };

  const maskEditor = useOverlayMaskEditor({
    activeMask,
    activeMasks,
    activeSlot,
    activeStream,
    frame,
    onClearCurrent: () => setMask(activeSlot, { enabled: false }),
    onMaskPatch: setMask,
    onSlotChange: setActiveSlot,
    onStreamChange: setActiveStream,
    reset,
    save,
    savedMsg,
    saving,
    error,
  });

  return (
    <div className="config-preview-layout overlay-config-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>视频叠加</h2>
            <p>文字叠加与隐私遮挡由设备端 region 统一应用。</p>
          </div>
        </div>
        <div className="form-grid">
          <FormField label="启用文字叠加">
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
        {maskEditor.controls}
      </section>

      <div className="overlay-preview-stack">
        <VideoPreview
          stream={activeStream}
          statuses={previewStatuses}
          onStreamChange={setActiveStream}
          surfaceOverlay={maskEditor.drawLayer}
        />
      </div>
    </div>
  );
}

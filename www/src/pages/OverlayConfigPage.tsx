import { useLayoutEffect, useRef, useState } from 'react';
import type { OverlayConfig, PrivacyMaskConfig, StreamName } from '../api/types';
import { resolutionValue } from '../api/resolution';
import { FormField } from '../components/FormField';
import { VideoPreview } from '../components/VideoPreview';
import { useOverlayConfig } from '../hooks/useOverlayConfig';
import { useVideoConfig } from '../hooks/useVideoConfig';

interface DragState {
  slot: number;
  start_x: number;
  start_y: number;
}

const streamLabel = (stream: StreamName) => (stream === 'main' ? '主码流' : '子码流');

const parseResolution = (resolution: string) => {
  const [width, height] = resolution.split('x').map((value) => Number(value));
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
    return { width: 1, height: 1 };
  }
  return { width, height };
};

const clamp = (value: number, min: number, max: number) =>
  Math.min(max, Math.max(min, value));

function scaleMaskToSurface(
  mask: PrivacyMaskConfig,
  frame: { width: number; height: number },
  videoArea: { left: number; top: number; width: number; height: number },
) {
  return {
    left: videoArea.left + (mask.x / frame.width) * videoArea.width,
    top: videoArea.top + (mask.y / frame.height) * videoArea.height,
    width: (mask.width / frame.width) * videoArea.width,
    height: (mask.height / frame.height) * videoArea.height,
  };
}

function contentAreaForSurface(
  frame: { width: number; height: number },
  surface: { width: number; height: number },
) {
  const frameRatio = frame.width / frame.height;
  const surfaceRatio = surface.width / surface.height;
  if (surfaceRatio > frameRatio) {
    const height = surface.height;
    const width = height * frameRatio;
    return { left: (surface.width - width) / 2, top: 0, width, height };
  }
  const width = surface.width;
  const height = width / frameRatio;
  return { left: 0, top: (surface.height - height) / 2, width, height };
}

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
  const [drag, setDrag] = useState<DragState | null>(null);
  const [surfaceSize, setSurfaceSize] = useState({ width: 0, height: 0 });
  const drawRef = useRef<HTMLDivElement | null>(null);

  useLayoutEffect(() => {
    const drawLayer = drawRef.current;
    if (!drawLayer) {
      return undefined;
    }
    const updateSurfaceSize = () => {
      const rect = drawLayer.getBoundingClientRect();
      setSurfaceSize({ width: rect.width, height: rect.height });
    };
    updateSurfaceSize();
    const observer = new ResizeObserver(updateSurfaceSize);
    observer.observe(drawLayer);
    return () => observer.disconnect();
  }, [activeStream]);

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
  const videoArea = surfaceSize.width > 0 && surfaceSize.height > 0
    ? contentAreaForSurface(frame, surfaceSize)
    : null;

  const setMask = (slot: number, patch: Partial<PrivacyMaskConfig>) => {
    setConfig(updateMask(config, activeStream, slot, patch));
  };

  const pointerToFrame = (event: React.PointerEvent<HTMLDivElement>) => {
    const surface = drawRef.current?.getBoundingClientRect();
    if (!surface) {
      return { x: 0, y: 0 };
    }
    const contentArea = contentAreaForSurface(frame, {
      width: surface.width,
      height: surface.height,
    });
    const x = clamp(
      event.clientX - surface.left - contentArea.left,
      0,
      contentArea.width,
    );
    const y = clamp(
      event.clientY - surface.top - contentArea.top,
      0,
      contentArea.height,
    );
    return {
      x: Math.round((x / contentArea.width) * frame.width),
      y: Math.round((y / contentArea.height) * frame.height),
    };
  };

  const beginDraw = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0) {
      return;
    }
    const point = pointerToFrame(event);
    setDrag({ slot: activeSlot, start_x: point.x, start_y: point.y });
    setMask(activeSlot, {
      enabled: true,
      x: point.x,
      y: point.y,
      width: 1,
      height: 1,
    });
    event.currentTarget.setPointerCapture(event.pointerId);
  };

  const updateDraw = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!drag) {
      return;
    }
    const point = pointerToFrame(event);
    const x = Math.min(drag.start_x, point.x);
    const y = Math.min(drag.start_y, point.y);
    const width = Math.max(1, Math.abs(point.x - drag.start_x));
    const height = Math.max(1, Math.abs(point.y - drag.start_y));
    setMask(drag.slot, {
      enabled: true,
      x,
      y,
      width: Math.min(width, frame.width - x),
      height: Math.min(height, frame.height - y),
    });
  };

  const finishDraw = (event: React.PointerEvent<HTMLDivElement>) => {
    if (drag) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
    setDrag(null);
  };

  return (
    <div className="config-preview-layout overlay-config-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>视频叠加</h2>
            <p>文字叠加与隐私遮挡由设备端 region_service 统一应用。</p>
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

        <div className="mask-editor">
          <div className="panel-title">隐私遮挡</div>
          <div className="tabs">
            <button type="button" className={activeStream === 'main' ? 'active' : ''} onClick={() => setActiveStream('main')}>主码流</button>
            <button type="button" className={activeStream === 'sub' ? 'active' : ''} onClick={() => setActiveStream('sub')}>子码流</button>
          </div>
          <div className="mask-slots">
            {activeMasks.map((mask, index) => (
              <button
                type="button"
                key={index}
                className={activeSlot === index ? 'active' : ''}
                onClick={() => setActiveSlot(index)}
              >
                <span>{index + 1}</span>
                <em>{mask.enabled ? `${mask.width}x${mask.height}` : '未启用'}</em>
              </button>
            ))}
          </div>
          <div className="form-grid form-grid-single">
            <FormField label="启用当前遮挡">
              <input type="checkbox" checked={activeMask.enabled} onChange={(e) => setMask(activeSlot, { enabled: e.target.checked })} />
            </FormField>
            <FormField label="遮挡颜色">
              <input type="color" value={activeMask.color} onChange={(e) => setMask(activeSlot, { color: e.target.value })} />
            </FormField>
          </div>
          <div className="mask-coordinate-row">
            <span>{streamLabel(activeStream)}</span>
            <span>{frame.width}x{frame.height}</span>
            <span>x {activeMask.x}</span>
            <span>y {activeMask.y}</span>
            <span>w {activeMask.width}</span>
            <span>h {activeMask.height}</span>
          </div>
          <div className="form-actions">
            <button type="button" onClick={() => setMask(activeSlot, { enabled: false })}>清除当前</button>
            <button type="button" onClick={reset}>恢复默认</button>
            <button type="button" className="primary" disabled={saving} onClick={() => void save()}>
              {saving ? '设置中' : '完成'}
            </button>
          </div>
          {savedMsg && <div className="save-hint">{savedMsg}</div>}
          {error && <div className="status-note error-note">{error}</div>}
        </div>
      </section>

      <div className="overlay-preview-stack">
        <VideoPreview
          stream={activeStream}
          statuses={previewStatuses}
          onStreamChange={setActiveStream}
          surfaceOverlay={
            <div
              ref={drawRef}
              className="mask-draw-layer"
              onPointerDown={beginDraw}
              onPointerMove={updateDraw}
              onPointerUp={finishDraw}
              onPointerCancel={finishDraw}
            >
              {activeMasks.map((mask, index) => {
                if (!mask.enabled) {
                  return null;
                }
                const rect = videoArea
                  ? scaleMaskToSurface(mask, frame, videoArea)
                  : { left: 0, top: 0, width: 0, height: 0 };
                return (
                  <div
                    key={index}
                    className={activeSlot === index ? 'mask-rect active' : 'mask-rect'}
                    style={{
                      left: rect.left,
                      top: rect.top,
                      width: rect.width,
                      height: rect.height,
                      backgroundColor: mask.color,
                    }}
                  >
                    {index + 1}
                  </div>
                );
              })}
            </div>
          }
        />
      </div>
    </div>
  );
}

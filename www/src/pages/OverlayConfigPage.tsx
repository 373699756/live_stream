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
const minFontSize = 8;
const maxFontSize = 32;
const maskAlignment = 4;

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
  videoArea: { width: number; height: number },
) {
  return {
    left: (mask.x / frame.width) * videoArea.width,
    top: (mask.y / frame.height) * videoArea.height,
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

function alignFloor(value: number, alignment: number) {
  return Math.floor(value / alignment) * alignment;
}

function alignCeil(value: number, alignment: number) {
  return Math.ceil(value / alignment) * alignment;
}

function normalizeMaskRect(
  frame: { width: number; height: number },
  x: number,
  y: number,
  width: number,
  height: number,
) {
  const maxX = alignFloor(Math.max(0, frame.width - maskAlignment), maskAlignment);
  const maxY = alignFloor(Math.max(0, frame.height - maskAlignment), maskAlignment);
  const alignedX = clamp(alignFloor(x, maskAlignment), 0, maxX);
  const alignedY = clamp(alignFloor(y, maskAlignment), 0, maxY);
  const maxWidth = Math.max(1, frame.width - alignedX);
  const maxHeight = Math.max(1, frame.height - alignedY);
  const alignedMaxWidth = maxWidth >= maskAlignment
    ? alignFloor(maxWidth, maskAlignment)
    : maxWidth;
  const alignedMaxHeight = maxHeight >= maskAlignment
    ? alignFloor(maxHeight, maskAlignment)
    : maxHeight;
  return {
    x: alignedX,
    y: alignedY,
    width: clamp(
      alignCeil(width, maskAlignment),
      Math.min(maskAlignment, alignedMaxWidth),
      alignedMaxWidth,
    ),
    height: clamp(
      alignCeil(height, maskAlignment),
      Math.min(maskAlignment, alignedMaxHeight),
      alignedMaxHeight,
    ),
  };
}

function scaledOverlayFontSize(
  fontSize: number,
  stream: StreamName,
  frame: { width: number; height: number },
  mainFrame: { width: number; height: number },
) {
  if (stream !== 'sub' || mainFrame.width <= 0 || mainFrame.height <= 0) {
    return fontSize;
  }
  const scale = Math.min(frame.width / mainFrame.width, frame.height / mainFrame.height);
  return clamp(Math.round(fontSize * scale), 6, fontSize);
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

function SwitchButton({
  checked,
  label,
  onChange,
}: {
  checked: boolean;
  label: string;
  onChange: (checked: boolean) => void;
}) {
  return (
    <button
      type="button"
      className={checked ? 'switch-button active' : 'switch-button'}
      aria-pressed={checked}
      onClick={() => onChange(!checked)}
    >
      <span className="switch-track" aria-hidden="true">
        <span />
      </span>
      <strong>{label}</strong>
    </button>
  );
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
  const [drawingEnabled, setDrawingEnabled] = useState(false);
  const [drag, setDrag] = useState<DragState | null>(null);
  const [surfaceSize, setSurfaceSize] = useState({ width: 0, height: 0 });
  const drawRef = useRef<HTMLDivElement | null>(null);
  const overlayReady = !loading && !videoLoading && config !== null;

  useLayoutEffect(() => {
    if (!overlayReady) {
      return undefined;
    }
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
    const animationFrame = window.requestAnimationFrame(updateSurfaceSize);
    return () => {
      window.cancelAnimationFrame(animationFrame);
      observer.disconnect();
    };
  }, [activeStream, overlayReady]);

  if (loading || videoLoading) {
    return <div className="panel">加载 Overlay 配置...</div>;
  }
  if (!config) {
    return <div className="panel">Overlay 配置加载失败：{error || '无可用配置'}</div>;
  }

  const streamConfig = videoConfig?.streams[activeStream];
  const capability = capabilities.streams[activeStream];
  const mainCapability = capabilities.streams.main;
  const mainFallbackResolution = mainCapability.resolutions[0]
    ? resolutionValue(mainCapability.resolutions[0])
    : '1920x1080';
  const fallbackResolution = capability.resolutions[0]
    ? resolutionValue(capability.resolutions[0])
    : activeStream === 'main'
      ? '1920x1080'
      : '640x360';
  const frame = parseResolution(streamConfig?.resolution || fallbackResolution);
  const mainFrame = parseResolution(
    videoConfig?.streams.main?.resolution || mainFallbackResolution,
  );
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
  const previewFontSize = scaledOverlayFontSize(
    config.font_size,
    activeStream,
    frame,
    mainFrame,
  );

  const setMask = (slot: number, patch: Partial<PrivacyMaskConfig>) => {
    setConfig(updateMask(config, activeStream, slot, patch));
  };

  const startDrawing = () => {
    setDrawingEnabled(true);
    setMask(activeSlot, { enabled: true });
  };

  const finishDrawing = () => {
    setDrag(null);
    setDrawingEnabled(false);
  };

  const pointerToFrame = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!videoArea) {
      return null;
    }
    const drawLayer = event.currentTarget.getBoundingClientRect();
    const x = clamp(
      event.clientX - drawLayer.left - videoArea.left,
      0,
      videoArea.width,
    );
    const y = clamp(
      event.clientY - drawLayer.top - videoArea.top,
      0,
      videoArea.height,
    );
    return {
      x: Math.round((x / videoArea.width) * frame.width),
      y: Math.round((y / videoArea.height) * frame.height),
    };
  };

  const beginDraw = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!drawingEnabled) {
      return;
    }
    if (event.button !== 0) {
      return;
    }
    const point = pointerToFrame(event);
    if (!point) {
      return;
    }
    const rect = normalizeMaskRect(frame, point.x, point.y, 1, 1);
    setDrag({ slot: activeSlot, start_x: point.x, start_y: point.y });
    setMask(activeSlot, {
      enabled: true,
      ...rect,
    });
    event.currentTarget.setPointerCapture(event.pointerId);
  };

  const updateDraw = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!drawingEnabled || !drag) {
      return;
    }
    const point = pointerToFrame(event);
    if (!point) {
      return;
    }
    const x = Math.min(drag.start_x, point.x);
    const y = Math.min(drag.start_y, point.y);
    const width = Math.max(1, Math.abs(point.x - drag.start_x));
    const height = Math.max(1, Math.abs(point.y - drag.start_y));
    setMask(drag.slot, {
      enabled: true,
      ...normalizeMaskRect(frame, x, y, width, height),
    });
  };

  const finishDraw = (event: React.PointerEvent<HTMLDivElement>) => {
    if (drag) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
    setDrag(null);
  };

  const timestampRect = videoArea
    ? {
        left: (config.items.timestamp.x / frame.width) * videoArea.width,
        top: (config.items.timestamp.y / frame.height) * videoArea.height,
        maxWidth:
          videoArea.width -
          (config.items.timestamp.x / frame.width) * videoArea.width,
      }
    : { left: 0, top: 0, maxWidth: 0 };
  const deviceNameRect = videoArea
    ? {
        left: (config.items.device_name.x / frame.width) * videoArea.width,
        top: (config.items.device_name.y / frame.height) * videoArea.height,
        maxWidth:
          videoArea.width -
          (config.items.device_name.x / frame.width) * videoArea.width,
      }
    : { left: 0, top: 0, maxWidth: 0 };
  const timestampPreview = config.items.timestamp.format
    .replace('%Y', '2026')
    .replace('%m', '05')
    .replace('%d', '26')
    .replace('%H', '12')
    .replace('%M', '34')
    .replace('%S', '56');

  return (
    <div className="config-preview-layout overlay-config-layout">
      <section className="panel settings-column overlay-settings-column">
        <div className="page-heading">
          <div>
            <h2>视频叠加</h2>
            <p>文字叠加与隐私遮挡由设备端 region_service 统一应用</p>
          </div>
        </div>
        <div className="form-grid overlay-text-form">
          <FormField label="文字叠加">
            <SwitchButton
              checked={config.enabled}
              label={config.enabled ? '已开启' : '已关闭'}
              onChange={(checked) => setConfig({ ...config, enabled: checked })}
            />
          </FormField>
          <FormField label="时间水印">
            <SwitchButton
              checked={config.items.timestamp.enabled}
              label={config.items.timestamp.enabled ? '显示' : '隐藏'}
              onChange={(checked) => setConfig({ ...config, items: { ...config.items, timestamp: { ...config.items.timestamp, enabled: checked } } })}
            />
          </FormField>
          <FormField label="时间格式">
            <input value={config.items.timestamp.format} onChange={(e) => setConfig({ ...config, items: { ...config.items, timestamp: { ...config.items.timestamp, format: e.target.value } } })} />
          </FormField>
          <FormField label="设备名称">
            <input value={config.items.device_name.text} onChange={(e) => setConfig({ ...config, items: { ...config.items, device_name: { ...config.items.device_name, text: e.target.value } } })} />
          </FormField>
          <FormField label="字体大小">
            <input
              type="number"
              min={minFontSize}
              max={maxFontSize}
              step={1}
              value={config.font_size}
              onChange={(e) => setConfig({
                ...config,
                font_size: clamp(Number(e.target.value), minFontSize, maxFontSize),
              })}
            />
          </FormField>
          <FormField label="字体颜色">
            <input type="color" value={config.font_color} onChange={(e) => setConfig({ ...config, font_color: e.target.value })} />
          </FormField>
          <FormField label="背景">
            <SwitchButton
              checked={config.background}
              label={config.background ? '显示' : '隐藏'}
              onChange={(checked) => setConfig({ ...config, background: checked })}
            />
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
              <SwitchButton
                checked={activeMask.enabled}
                label={activeMask.enabled ? '已开启' : '已关闭'}
                onChange={(checked) => setMask(activeSlot, { enabled: checked })}
              />
            </FormField>
            <FormField label="遮挡颜色">
              <input type="color" value={activeMask.color} onChange={(e) => setMask(activeSlot, { color: e.target.value })} />
            </FormField>
          </div>
          <div className="mask-draw-actions">
            <button
              type="button"
              className={drawingEnabled ? 'active' : ''}
              onClick={startDrawing}
            >
              开始画矩形
            </button>
            <button
              type="button"
              disabled={!drawingEnabled}
              onClick={finishDrawing}
            >
              完成绘制
            </button>
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
            <button type="button" onClick={() => { finishDrawing(); setMask(activeSlot, { enabled: false }); }}>清除当前</button>
            <button type="button" onClick={reset}>恢复默认</button>
            <button type="button" className="primary" disabled={saving} onClick={() => void save()}>
              {saving ? '设置中' : '保存设置'}
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
              className={drawingEnabled ? 'mask-draw-layer drawing' : 'mask-draw-layer'}
              onPointerDown={beginDraw}
              onPointerMove={updateDraw}
              onPointerUp={finishDraw}
              onPointerCancel={finishDraw}
            >
              {videoArea && (
                <div
                  className="mask-video-layer"
                  style={{
                    left: videoArea.left,
                    top: videoArea.top,
                    width: videoArea.width,
                    height: videoArea.height,
                  }}
                >
                  {config.enabled && config.items.timestamp.enabled && (
                    <div
                      className={config.background ? 'overlay-text background' : 'overlay-text'}
                      style={{
                        left: timestampRect.left,
                        top: timestampRect.top,
                        maxWidth: timestampRect.maxWidth,
                        color: config.font_color,
                        fontSize: previewFontSize,
                      }}
                    >
                      {timestampPreview}
                    </div>
                  )}
                  {config.enabled && config.items.device_name.enabled && (
                    <div
                      className={config.background ? 'overlay-text background' : 'overlay-text'}
                      style={{
                        left: deviceNameRect.left,
                        top: deviceNameRect.top,
                        maxWidth: deviceNameRect.maxWidth,
                        color: config.font_color,
                        fontSize: previewFontSize,
                      }}
                    >
                      {config.items.device_name.text}
                    </div>
                  )}
                  {activeMasks.map((mask, index) => {
                    if (!mask.enabled) {
                      return null;
                    }
                    const rect = scaleMaskToSurface(mask, frame, videoArea);
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
              )}
            </div>
          }
        />
      </div>
    </div>
  );
}

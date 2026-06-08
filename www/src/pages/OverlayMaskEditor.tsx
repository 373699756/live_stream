import { useLayoutEffect, useRef, useState } from 'react';
import type { PrivacyMaskConfig, StreamName } from '../api/types';
import { FormField } from '../components/FormField';

interface DragState {
  slot: number;
  start_x: number;
  start_y: number;
}

interface FrameSize {
  width: number;
  height: number;
}

interface SurfaceRect {
  left: number;
  top: number;
  width: number;
  height: number;
}

interface UseOverlayMaskEditorOptions {
  activeMask: PrivacyMaskConfig;
  activeMasks: PrivacyMaskConfig[];
  activeSlot: number;
  activeStream: StreamName;
  frame: FrameSize;
  onClearCurrent: () => void;
  onMaskPatch: (slot: number, patch: Partial<PrivacyMaskConfig>) => void;
  onSlotChange: (slot: number) => void;
  onStreamChange: (stream: StreamName) => void;
  reset: () => void;
  save: () => Promise<void>;
  savedMsg: string;
  saving: boolean;
  error: string;
}

const streamLabel = (stream: StreamName) => (stream === 'main' ? '主码流' : '子码流');

const clamp = (value: number, min: number, max: number) =>
  Math.min(max, Math.max(min, value));

function scaleMaskToSurface(
  mask: PrivacyMaskConfig,
  frame: FrameSize,
  videoArea: SurfaceRect,
) {
  return {
    left: videoArea.left + (mask.x / frame.width) * videoArea.width,
    top: videoArea.top + (mask.y / frame.height) * videoArea.height,
    width: (mask.width / frame.width) * videoArea.width,
    height: (mask.height / frame.height) * videoArea.height,
  };
}

function contentAreaForSurface(
  frame: FrameSize,
  surface: { width: number; height: number },
): SurfaceRect | null {
  if (
    frame.width <= 0 ||
    frame.height <= 0 ||
    surface.width <= 0 ||
    surface.height <= 0
  ) {
    return null;
  }
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

export function useOverlayMaskEditor({
  activeMask,
  activeMasks,
  activeSlot,
  activeStream,
  frame,
  onClearCurrent,
  onMaskPatch,
  onSlotChange,
  onStreamChange,
  reset,
  save,
  savedMsg,
  saving,
  error,
}: UseOverlayMaskEditorOptions) {
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
    if (typeof ResizeObserver === 'undefined') {
      window.addEventListener('resize', updateSurfaceSize);
      return () => window.removeEventListener('resize', updateSurfaceSize);
    }
    const observer = new ResizeObserver(updateSurfaceSize);
    observer.observe(drawLayer);
    return () => observer.disconnect();
  }, [activeStream]);

  const videoArea = surfaceSize.width > 0 && surfaceSize.height > 0
    ? contentAreaForSurface(frame, surfaceSize)
    : null;

  const pointerToFrame = (event: React.PointerEvent<HTMLDivElement>) => {
    const surface = drawRef.current?.getBoundingClientRect();
    if (!surface) {
      return { x: 0, y: 0 };
    }
    const contentArea = contentAreaForSurface(frame, {
      width: surface.width,
      height: surface.height,
    });
    if (!contentArea) {
      return null;
    }
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
    if (!point) {
      return;
    }
    setDrag({ slot: activeSlot, start_x: point.x, start_y: point.y });
    onMaskPatch(activeSlot, {
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
    if (!point) {
      return;
    }
    const x = Math.min(drag.start_x, point.x);
    const y = Math.min(drag.start_y, point.y);
    const width = Math.max(1, Math.abs(point.x - drag.start_x));
    const height = Math.max(1, Math.abs(point.y - drag.start_y));
    onMaskPatch(drag.slot, {
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

  const drawLayer = (
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
  );

  const controls = (
    <div className="mask-editor">
      <div className="panel-title">隐私遮挡</div>
      <div className="tabs">
        <button
          type="button"
          className={activeStream === 'main' ? 'active' : ''}
          onClick={() => onStreamChange('main')}
        >
          主码流
        </button>
        <button
          type="button"
          className={activeStream === 'sub' ? 'active' : ''}
          onClick={() => onStreamChange('sub')}
        >
          子码流
        </button>
      </div>
      <div className="mask-slots">
        {activeMasks.map((mask, index) => (
          <button
            type="button"
            key={index}
            className={activeSlot === index ? 'active' : ''}
            onClick={() => onSlotChange(index)}
          >
            <span>{index + 1}</span>
            <em>{mask.enabled ? `${mask.width}x${mask.height}` : '未启用'}</em>
          </button>
        ))}
      </div>
      <div className="form-grid form-grid-single">
        <FormField label="启用当前遮挡">
          <input
            type="checkbox"
            checked={activeMask.enabled}
            onChange={(e) => onMaskPatch(activeSlot, { enabled: e.target.checked })}
          />
        </FormField>
        <FormField label="遮挡颜色">
          <input
            type="color"
            value={activeMask.color}
            onChange={(e) => onMaskPatch(activeSlot, { color: e.target.value })}
          />
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
        <button type="button" onClick={onClearCurrent}>清除当前</button>
        <button type="button" onClick={reset}>恢复默认</button>
        <button
          type="button"
          className="primary"
          disabled={saving}
          onClick={() => void save()}
        >
          {saving ? '设置中' : '完成'}
        </button>
      </div>
      {savedMsg && <div className="save-hint">{savedMsg}</div>}
      {error && <div className="status-note error-note">{error}</div>}
    </div>
  );

  return { controls, drawLayer };
}

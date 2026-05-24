import { useLayoutEffect, useRef, useState } from 'react';
import type { AiDetection, AiStatus, StreamName } from '../api/types';

interface AiDetectionOverlayProps {
  frameResolution?: string;
  status: AiStatus | null;
  stream: StreamName;
  error?: string;
}

interface SurfaceSize {
  width: number;
  height: number;
}

const streamLabel = (stream: StreamName) => (stream === 'main' ? '主码流' : '子码流');

function percent(value: number) {
  return `${Math.round(value * 100)}%`;
}

function parseResolution(resolution: string | undefined) {
  const [width, height] = (resolution || '').split('x').map((value) => Number(value));
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
    return { width: 16, height: 9 };
  }
  return { width, height };
}

function contentAreaStyle(
  frame: { width: number; height: number },
  surface: SurfaceSize,
) {
  if (surface.width <= 0 || surface.height <= 0) {
    return { left: 0, top: 0, width: '100%', height: '100%' };
  }
  const frameRatio = frame.width / frame.height;
  const surfaceRatio = surface.width / surface.height;
  if (surfaceRatio > frameRatio) {
    const height = surface.height;
    const width = height * frameRatio;
    return {
      left: (surface.width - width) / 2,
      top: 0,
      width,
      height,
    };
  }
  const width = surface.width;
  const height = width / frameRatio;
  return {
    left: 0,
    top: (surface.height - height) / 2,
    width,
    height,
  };
}

function clampUnit(value: number) {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(1, Math.max(0, value));
}

function detectionStyle(detection: AiDetection) {
  const x = clampUnit(detection.x);
  const y = clampUnit(detection.y);
  const right = clampUnit(detection.x + detection.width);
  const bottom = clampUnit(detection.y + detection.height);
  const width = Math.max(0.01, right - x);
  const height = Math.max(0.01, bottom - y);
  return {
    left: percent(x),
    top: percent(y),
    width: percent(width),
    height: percent(height),
  };
}

export function AiDetectionOverlay({
  frameResolution,
  status,
  stream,
  error = '',
}: AiDetectionOverlayProps) {
  const overlayRef = useRef<HTMLDivElement | null>(null);
  const [surfaceSize, setSurfaceSize] = useState<SurfaceSize>({ width: 0, height: 0 });
  const result = status?.last_result;
  const aiReady =
    !error && Boolean(status?.config.enabled) && Boolean(status?.stats.backend_available);
  const detections =
    aiReady && result?.success && result.stream === stream
      ? result.detections
      : [];
  const resultCount =
    aiReady && result?.success ? result.detections.length : 0;
  const frame = parseResolution(frameResolution);
  const contentStyle = contentAreaStyle(frame, surfaceSize);

  useLayoutEffect(() => {
    const overlay = overlayRef.current;
    if (!overlay) {
      return undefined;
    }
    const updateSurfaceSize = () => {
      const rect = overlay.getBoundingClientRect();
      setSurfaceSize({ width: rect.width, height: rect.height });
    };
    updateSurfaceSize();
    const observer = new ResizeObserver(updateSurfaceSize);
    observer.observe(overlay);
    return () => observer.disconnect();
  }, []);

  let statusText = '读取中';
  if (error) {
    statusText = '状态异常';
  } else if (status && !status.config.enabled) {
    statusText = '未启用';
  } else if (status && !status.stats.backend_available) {
    statusText = '后端不可用';
  } else if (aiReady && result?.success && result.stream !== stream) {
    statusText = `${streamLabel(result.stream)} ${resultCount} 个目标`;
  } else if (aiReady && result?.success) {
    statusText = `${detections.length} 个目标`;
  } else if (status?.config.enabled) {
    statusText = '无有效结果';
  }

  return (
    <div className="ai-preview-overlay" ref={overlayRef} aria-hidden="true">
      <div className="ai-detection-layer" style={contentStyle}>
        {detections.map((detection, index) => (
          <div
            className={
              detection.y <= 0.08 ? 'ai-detection-box near-top' : 'ai-detection-box'
            }
            key={`${result?.sequence || 0}-${index}`}
            style={detectionStyle(detection)}
          >
            <span>
              {detection.label || 'target'} {percent(detection.confidence)}
            </span>
          </div>
        ))}
      </div>
      <div className="ai-preview-status">
        <strong>AI</strong>
        <span>{statusText}</span>
      </div>
    </div>
  );
}

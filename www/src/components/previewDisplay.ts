import type { MediaStreamRuntime, StreamName } from '../api/types';
import { previewModeLabels, type PreviewMode } from '../hooks/previewMode';

export interface PreviewStreamSummary {
  detail: string;
  label: string;
  running: boolean;
  state: string;
}

export function previewStreamLabel(stream: StreamName) {
  return stream === 'main' ? '主码流' : '子码流';
}

export function previewValueText(
  value: string | number | undefined,
  fallback = '未知',
) {
  return value === undefined || value === '' ? fallback : String(value);
}

export function previewDetailText(stream: StreamName, mode: PreviewMode) {
  return `${previewStreamLabel(stream)} / ${previewModeLabels[mode]}`;
}

export function previewStreamSummary(
  statuses: MediaStreamRuntime[],
  stream: StreamName,
): PreviewStreamSummary {
  const status = statuses.find((item) => item.stream === stream);
  const running = status?.running ?? false;

  return {
    label: previewStreamLabel(stream),
    running,
    state: running ? '运行中' : '未运行',
    detail: [
      previewValueText(status?.codec),
      previewValueText(status?.resolution, '--'),
      `${previewValueText(status?.fps, '--')}fps`,
    ].join(' / '),
  };
}

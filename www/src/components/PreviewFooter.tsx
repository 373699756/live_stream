import type { MediaStreamInfo, StreamName } from '../api/types';
import { previewModeLabels, type PreviewMode } from '../hooks/previewMode';
import { StatusBadge } from './StatusBadge';
import { previewStreamLabel, previewValueText } from './previewDisplay';

interface PreviewFooterProps {
    active?: MediaStreamInfo;
    decodedSize: string;
    displaySize: string;
    mode: PreviewMode;
    stream: StreamName;
}

export function PreviewFooter({
    active,
    decodedSize,
    displaySize,
    mode,
    stream,
}: PreviewFooterProps) {
    return (
        <div className="preview-footer">
            <StatusBadge state={active?.running ? 'running' : 'pending'} />
            <span>{previewStreamLabel(stream)}</span>
            <span>{previewModeLabels[mode]}</span>
            <span>{previewValueText(active?.codec)}</span>
            <span>分辨率 {previewValueText(active?.resolution, '--')}</span>
            {decodedSize && <span>实际 {decodedSize}</span>}
            {displaySize && <span>显示 {displaySize}</span>}
            <span>{previewValueText(active?.fps, '--')} fps</span>
            <span>{previewValueText(active?.bitrate_kbps, '--')} kbps</span>
        </div>
    );
}

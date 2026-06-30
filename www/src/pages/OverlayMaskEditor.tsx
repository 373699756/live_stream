import type { PrivacyMaskConfig, StreamName } from '../api/types';
import { FormField } from '../components/FormField';
import {
    clampUnit,
    VideoRegionDrawLayer,
    type VideoRegionDrag,
    type VideoRegionPoint,
    type VideoRegionRect,
} from '../components/VideoRegionDrawLayer';

interface FrameSize {
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

const streamLabel = (stream: StreamName) =>
    stream === 'main' ? '主码流' : '子码流';

function maskToRect(
    mask: PrivacyMaskConfig,
    frame: FrameSize,
): VideoRegionRect {
    return {
        x: clampUnit(mask.x / frame.width),
        y: clampUnit(mask.y / frame.height),
        width: clampUnit(mask.width / frame.width),
        height: clampUnit(mask.height / frame.height),
    };
}

function maskPatchFromPoints(
    frame: FrameSize,
    start: VideoRegionPoint,
    end: VideoRegionPoint,
) {
    const startX = Math.round(start.x * frame.width);
    const startY = Math.round(start.y * frame.height);
    const endX = Math.round(end.x * frame.width);
    const endY = Math.round(end.y * frame.height);
    const x = Math.min(startX, endX);
    const y = Math.min(startY, endY);
    const width = Math.max(1, Math.abs(endX - startX));
    const height = Math.max(1, Math.abs(endY - startY));
    return {
        enabled: true,
        x,
        y,
        width: Math.min(width, frame.width - x),
        height: Math.min(height, frame.height - y),
    };
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
    const beginDraw = (point: VideoRegionPoint): VideoRegionDrag => {
        onMaskPatch(activeSlot, {
            ...maskPatchFromPoints(frame, point, point),
        });
        return { regionIndex: activeSlot, start: point };
    };

    const updateDraw = (drag: VideoRegionDrag, point: VideoRegionPoint) =>
        onMaskPatch(
            drag.regionIndex,
            maskPatchFromPoints(frame, drag.start, point),
        );

    const drawLayer = (
        <VideoRegionDrawLayer
            className="mask-draw-layer"
            drawing
            frame={frame}
            items={activeMasks
                .map((mask, index) =>
                    mask.enabled
                        ? {
                              className:
                                  activeSlot === index
                                      ? 'mask-rect active'
                                      : 'mask-rect',
                              content: index + 1,
                              key: String(index),
                              rect: maskToRect(mask, frame),
                              style: { backgroundColor: mask.color },
                          }
                        : null,
                )
                .filter((item) => item !== null)}
            showGrid
            onDrawStart={beginDraw}
            onDrawMove={updateDraw}
        />
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
                        <em>
                            {mask.enabled
                                ? `${mask.width}x${mask.height}`
                                : '未启用'}
                        </em>
                    </button>
                ))}
            </div>
            <div className="form-grid form-grid-single">
                <FormField label="启用当前遮挡">
                    <input
                        type="checkbox"
                        checked={activeMask.enabled}
                        onChange={(e) =>
                            onMaskPatch(activeSlot, {
                                enabled: e.target.checked,
                            })
                        }
                    />
                </FormField>
                <FormField label="遮挡颜色">
                    <input
                        type="color"
                        value={activeMask.color}
                        onChange={(e) =>
                            onMaskPatch(activeSlot, { color: e.target.value })
                        }
                    />
                </FormField>
            </div>
            <div className="mask-coordinate-row">
                <span>{streamLabel(activeStream)}</span>
                <span>
                    {frame.width}x{frame.height}
                </span>
                <span>x {activeMask.x}</span>
                <span>y {activeMask.y}</span>
                <span>w {activeMask.width}</span>
                <span>h {activeMask.height}</span>
            </div>
            <div className="form-actions">
                <button type="button" onClick={onClearCurrent}>
                    清除当前
                </button>
                <button type="button" onClick={reset}>
                    恢复默认
                </button>
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

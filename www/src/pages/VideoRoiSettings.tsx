import type {
    VideoRoiConfig,
    VideoRoiRegion,
    VideoStreamCapabilities,
    VideoStreamConfig,
} from '../api/types';
import { FormField } from '../components/FormField';

function parseResolutionSize(resolution: string) {
    const match = /^(\d+)x(\d+)$/.exec(resolution);
    if (!match) {
        return { width: 0, height: 0 };
    }
    return { width: Number(match[1]), height: Number(match[2]) };
}

function clampNumber(value: number, min: number, max: number) {
    if (!Number.isFinite(value)) return min;
    return Math.min(Math.max(Math.round(value), min), max);
}

interface VideoRoiSettingsProps {
    activeRoiRegionIndex: number;
    available: boolean;
    stream: VideoStreamConfig;
    capabilities: VideoStreamCapabilities;
    roiDrawing: boolean;
    onRoiChange: (roi: VideoRoiConfig) => void;
    onRoiRegionSelect: (index: number) => void;
    onStartRoiDraw: (index: number) => void;
}

export function VideoRoiSettings({
    activeRoiRegionIndex,
    available,
    stream,
    capabilities,
    roiDrawing,
    onRoiChange,
    onRoiRegionSelect,
    onStartRoiDraw,
}: VideoRoiSettingsProps) {
    const roiSupported =
        Boolean(capabilities.roi_supported) &&
        (stream.codec === 'h264' || stream.codec === 'h265');
    const maxRoiRegions = capabilities.max_roi_regions || 0;
    const roiRegions = stream.roi?.regions ?? [];
    const streamSize = parseResolutionSize(stream.resolution);
    const activeRegionAvailable =
        activeRoiRegionIndex >= 0 && activeRoiRegionIndex < roiRegions.length;

    const patchRoi = (value: Partial<VideoRoiConfig>) => {
        onRoiChange({
            enabled: stream.roi?.enabled ?? false,
            regions: roiRegions,
            ...value,
        });
    };
    const startAddRoiRegion = () => {
        if (!roiSupported || roiRegions.length >= maxRoiRegions) return;
        onStartRoiDraw(roiRegions.length);
    };
    const updateRoiRegion = (index: number, value: Partial<VideoRoiRegion>) => {
        const nextRegions = roiRegions.map((region, regionIndex) => {
            if (regionIndex !== index) return region;
            const next = { ...region, ...value };
            next.x = clampNumber(next.x, 0, Math.max(0, streamSize.width - 1));
            next.y = clampNumber(next.y, 0, Math.max(0, streamSize.height - 1));
            next.width = clampNumber(
                next.width,
                1,
                Math.max(1, streamSize.width - next.x),
            );
            next.height = clampNumber(
                next.height,
                1,
                Math.max(1, streamSize.height - next.y),
            );
            next.qp = clampNumber(next.qp, -51, 51);
            return next;
        });
        patchRoi({ regions: nextRegions });
    };
    const removeRoiRegion = (index: number) => {
        patchRoi({
            regions: roiRegions.filter(
                (_, regionIndex) => regionIndex !== index,
            ),
        });
        onRoiRegionSelect(Math.max(0, index - 1));
    };

    return (
        <div className="stream-advanced-block">
            <div className="stream-advanced-block-title">ROI 编码</div>
            <FormField label="启用">
                <input
                    type="checkbox"
                    disabled={!available || !roiSupported}
                    checked={Boolean(stream.roi?.enabled)}
                    onChange={(e) => patchRoi({ enabled: e.target.checked })}
                />
            </FormField>
            <div className="stream-state-line">
                <span>
                    {roiRegions.length} / {maxRoiRegions || 0} 区域
                </span>
                <button
                    type="button"
                    disabled={
                        !available ||
                        !roiSupported ||
                        roiRegions.length >= maxRoiRegions
                    }
                    onClick={startAddRoiRegion}
                >
                    {roiDrawing && activeRoiRegionIndex >= roiRegions.length
                        ? '在预览中拖拽'
                        : '添加区域'}
                </button>
                <button
                    type="button"
                    disabled={
                        !available || !roiSupported || !activeRegionAvailable
                    }
                    onClick={() => onStartRoiDraw(activeRoiRegionIndex)}
                >
                    {roiDrawing && activeRegionAvailable
                        ? '重新拖拽中'
                        : '重画选中'}
                </button>
            </div>
            {roiRegions.length > 0 && (
                <div className="roi-region-table">
                    <div className="roi-region-head">
                        <span>开关</span>
                        <span>X</span>
                        <span>Y</span>
                        <span>宽</span>
                        <span>高</span>
                        <span>QP</span>
                        <span>模式</span>
                        <span>操作</span>
                    </div>
                    {roiRegions.map((region, index) => (
                        <div
                            className={
                                activeRoiRegionIndex === index
                                    ? 'roi-region-row active'
                                    : 'roi-region-row'
                            }
                            key={`${index}-${region.x}-${region.y}`}
                            onClick={() => onRoiRegionSelect(index)}
                        >
                            <input
                                type="checkbox"
                                disabled={!available || !roiSupported}
                                checked={region.enabled}
                                onChange={(e) =>
                                    updateRoiRegion(index, {
                                        enabled: e.target.checked,
                                    })
                                }
                            />
                            <input
                                type="number"
                                min={0}
                                max={Math.max(0, streamSize.width - 1)}
                                disabled={!available || !roiSupported}
                                value={region.x}
                                onChange={(e) =>
                                    updateRoiRegion(index, {
                                        x: Number(e.target.value),
                                    })
                                }
                            />
                            <input
                                type="number"
                                min={0}
                                max={Math.max(0, streamSize.height - 1)}
                                disabled={!available || !roiSupported}
                                value={region.y}
                                onChange={(e) =>
                                    updateRoiRegion(index, {
                                        y: Number(e.target.value),
                                    })
                                }
                            />
                            <input
                                type="number"
                                min={1}
                                max={Math.max(1, streamSize.width - region.x)}
                                disabled={!available || !roiSupported}
                                value={region.width}
                                onChange={(e) =>
                                    updateRoiRegion(index, {
                                        width: Number(e.target.value),
                                    })
                                }
                            />
                            <input
                                type="number"
                                min={1}
                                max={Math.max(1, streamSize.height - region.y)}
                                disabled={!available || !roiSupported}
                                value={region.height}
                                onChange={(e) =>
                                    updateRoiRegion(index, {
                                        height: Number(e.target.value),
                                    })
                                }
                            />
                            <input
                                type="number"
                                min={-51}
                                max={51}
                                disabled={!available || !roiSupported}
                                value={region.qp}
                                onChange={(e) =>
                                    updateRoiRegion(index, {
                                        qp: Number(e.target.value),
                                    })
                                }
                            />
                            <select
                                disabled={!available || !roiSupported}
                                value={
                                    region.absolute_qp ? 'absolute' : 'relative'
                                }
                                onChange={(e) =>
                                    updateRoiRegion(index, {
                                        absolute_qp:
                                            e.target.value === 'absolute',
                                    })
                                }
                            >
                                <option value="relative">相对</option>
                                <option value="absolute">绝对</option>
                            </select>
                            <button
                                type="button"
                                disabled={!available}
                                onClick={(e) => {
                                    e.stopPropagation();
                                    removeRoiRegion(index);
                                }}
                            >
                                删除
                            </button>
                        </div>
                    ))}
                </div>
            )}
            <div className="stream-advanced-hint">
                {roiSupported
                    ? '添加或重画区域后在右侧预览拖拽；相对 QP 使用负值可提升区域清晰度。'
                    : 'ROI 仅对支持的 H.264/H.265 编码通道生效。'}
            </div>
        </div>
    );
}

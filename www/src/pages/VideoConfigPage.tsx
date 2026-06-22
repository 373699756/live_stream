import { useEffect, useRef, useState } from 'react';
import { saveVideoConfig } from '../api/video';
import { isStreamSupported } from '../api/resolution';
import { cloneDefaultConfig } from '../api/configDefaults';
import { mockVideoConfig } from '../api/mockVideo';
import type {
    StreamName,
    VideoRoiRegion,
    VideoStreamConfig,
} from '../api/types';
import { VideoPreview } from '../components/VideoPreview';
import {
    clampUnit,
    VideoRegionDrawLayer,
    type VideoRegionPoint,
    type VideoRegionRect,
} from '../components/VideoRegionDrawLayer';
import { useVideoConfig } from '../hooks/useVideoConfig';
import { useWebrtcConfig } from '../hooks/useWebrtcConfig';
import { VideoStreamForm } from './VideoStreamForm';

function parseResolutionSize(resolution: string) {
    const match = /^(\d+)x(\d+)$/.exec(resolution);
    if (!match) {
        return { width: 1, height: 1 };
    }
    const width = Number(match[1]);
    const height = Number(match[2]);
    if (!Number.isFinite(width) || !Number.isFinite(height)) {
        return { width: 1, height: 1 };
    }
    return {
        width: Math.max(1, width),
        height: Math.max(1, height),
    };
}

function clampNumber(value: number, min: number, max: number) {
    if (!Number.isFinite(value)) return min;
    return Math.min(Math.max(Math.round(value), min), max);
}

function clampRoiRegion(
    region: VideoRoiRegion,
    stream: VideoStreamConfig,
): VideoRoiRegion {
    const size = parseResolutionSize(stream.resolution);
    const x = clampNumber(region.x, 0, Math.max(0, size.width - 1));
    const y = clampNumber(region.y, 0, Math.max(0, size.height - 1));
    return {
        ...region,
        x,
        y,
        width: clampNumber(region.width, 1, Math.max(1, size.width - x)),
        height: clampNumber(region.height, 1, Math.max(1, size.height - y)),
        qp: clampNumber(region.qp, -51, 51),
    };
}

function defaultRoiRegion(region: VideoRoiRegion | undefined): VideoRoiRegion {
    return {
        enabled: region?.enabled ?? true,
        x: region?.x ?? 0,
        y: region?.y ?? 0,
        width: region?.width ?? 1,
        height: region?.height ?? 1,
        qp: region?.qp ?? -6,
        absolute_qp: region?.absolute_qp ?? false,
    };
}

function roiRegionFromPoints(
    frame: { width: number; height: number },
    baseRegion: VideoRoiRegion | undefined,
    start: VideoRegionPoint,
    end: VideoRegionPoint,
): VideoRoiRegion {
    const region = defaultRoiRegion(baseRegion);
    const left = Math.min(start.x, end.x) * frame.width;
    const top = Math.min(start.y, end.y) * frame.height;
    const right = Math.max(start.x, end.x) * frame.width;
    const bottom = Math.max(start.y, end.y) * frame.height;
    const x = clampNumber(left, 0, Math.max(0, frame.width - 1));
    const y = clampNumber(top, 0, Math.max(0, frame.height - 1));
    return {
        ...region,
        enabled: true,
        x,
        y,
        width: clampNumber(
            Math.max(1, right - left),
            1,
            Math.max(1, frame.width - x),
        ),
        height: clampNumber(
            Math.max(1, bottom - top),
            1,
            Math.max(1, frame.height - y),
        ),
    };
}

function roiRegionToRect(
    region: VideoRoiRegion,
    frame: { width: number; height: number },
): VideoRegionRect {
    return {
        x: clampUnit(region.x / frame.width),
        y: clampUnit(region.y / frame.height),
        width: clampUnit(region.width / frame.width),
        height: clampUnit(region.height / frame.height),
    };
}

export function VideoConfigPage() {
    const [active, setActive] = useState<StreamName>('main');
    const [activeRoiRegionByStream, setActiveRoiRegionByStream] = useState<
        Record<StreamName, number>
    >({ main: 0, sub: 0 });
    const [roiDrawTarget, setRoiDrawTarget] = useState<{
        stream: StreamName;
        regionIndex: number;
    } | null>(null);
    const {
        config,
        setConfig,
        capabilities,
        statuses,
        previewUrls,
        reloadConfig,
        refreshStatuses,
        loading,
        error,
    } = useVideoConfig(active);
    const { config: webrtcConfig } = useWebrtcConfig();
    const [saved, setSaved] = useState<string>('');
    const [saving, setSaving] = useState(false);
    const [previewEnabled, setPreviewEnabled] = useState(true);
    const refreshTimerRef = useRef(0);
    const previewTimerRef = useRef(0);

    useEffect(() => {
        return () => {
            window.clearTimeout(refreshTimerRef.current);
            window.clearTimeout(previewTimerRef.current);
        };
    }, []);

    useEffect(() => {
        if (!config) {
            return;
        }
        setActiveRoiRegionByStream((current) => {
            let changed = false;
            const next = { ...current };
            (['main', 'sub'] as StreamName[]).forEach((stream) => {
                const regions = config.streams[stream].roi?.regions ?? [];
                const maxIndex = Math.max(0, regions.length - 1);
                const clampedIndex = clampNumber(
                    current[stream] ?? 0,
                    0,
                    maxIndex,
                );
                if (clampedIndex !== current[stream]) {
                    next[stream] = clampedIndex;
                    changed = true;
                }
            });
            return changed ? next : current;
        });
    }, [config]);

    if (loading) {
        return <div className="panel">加载视频配置...</div>;
    }
    if (!config) {
        return (
            <div className="panel">
                视频配置加载失败：{error || '无可用配置'}
            </div>
        );
    }

    const changeActiveStream = (stream: StreamName) => {
        setActive(stream);
        setRoiDrawTarget(null);
    };
    const updateStream = (name: StreamName, stream: VideoStreamConfig) => {
        setConfig({
            ...config,
            streams: { ...config.streams, [name]: stream },
        });
    };
    const selectRoiRegion = (index: number) => {
        setActiveRoiRegionByStream((current) => ({
            ...current,
            [active]: Math.max(0, Math.round(index)),
        }));
    };
    const activeStream = config.streams[active];
    const activeRoiRegions = activeStream.roi?.regions ?? [];
    const activeCapabilities = capabilities.streams[active];
    const activeRoiRegionIndex =
        roiDrawTarget?.stream === active &&
        roiDrawTarget.regionIndex === activeRoiRegions.length
            ? roiDrawTarget.regionIndex
            : activeRoiRegions.length > 0
              ? clampNumber(
                    activeRoiRegionByStream[active] ?? 0,
                    0,
                    activeRoiRegions.length - 1,
                )
              : 0;
    const activeRoiSupported =
        activeCapabilities.available !== false &&
        Boolean(activeCapabilities.roi_supported) &&
        (activeStream.codec === 'h264' || activeStream.codec === 'h265');
    const activeFrame = parseResolutionSize(activeStream.resolution);
    const roiDrawing =
        roiDrawTarget?.stream === active &&
        roiDrawTarget.regionIndex === activeRoiRegionIndex &&
        activeRoiSupported;
    const updateRoiRegionFromPreview = (
        index: number,
        region: VideoRoiRegion,
    ) => {
        if (!activeRoiSupported) {
            return;
        }
        const maxRoiRegions = activeCapabilities.max_roi_regions || 0;
        if (
            index > activeRoiRegions.length ||
            (index === activeRoiRegions.length &&
                activeRoiRegions.length >= maxRoiRegions)
        ) {
            return;
        }
        const nextRegions = [...activeRoiRegions];
        nextRegions[index] = clampRoiRegion(region, activeStream);
        updateStream(active, {
            ...activeStream,
            roi: {
                enabled: activeStream.roi?.enabled ?? false,
                regions: nextRegions,
            },
        });
        selectRoiRegion(index);
    };
    const startRoiDraw = (index: number) => {
        if (!activeRoiSupported) {
            return;
        }
        const maxRoiRegions = activeCapabilities.max_roi_regions || 0;
        if (
            index > activeRoiRegions.length ||
            (index === activeRoiRegions.length &&
                activeRoiRegions.length >= maxRoiRegions)
        ) {
            return;
        }
        selectRoiRegion(index);
        setRoiDrawTarget({ stream: active, regionIndex: index });
    };
    const finishRoiDraw = (index: number) => {
        selectRoiRegion(index);
        setRoiDrawTarget(null);
    };
    const roiRegionItems = activeRoiRegions.map((region, index) => {
        const regionClassName = [
            'roi-region-rect',
            index === activeRoiRegionIndex ? 'active' : '',
            activeStream.roi?.enabled && region.enabled ? '' : 'disabled',
        ]
            .filter(Boolean)
            .join(' ');
        return {
            className: regionClassName,
            key: `${index}-${region.x}-${region.y}-${region.width}-${region.height}`,
            rect: roiRegionToRect(region, activeFrame),
            content: index + 1,
        };
    });
    const previewStatuses = statuses.map((status) => ({
        ...status,
        resolution: config.streams[status.stream].resolution,
        fps: config.streams[status.stream].fps,
        bitrate_kbps: config.streams[status.stream].bitrate_kbps,
    }));
    const resetDefault = () => {
        setConfig(cloneDefaultConfig(mockVideoConfig));
        setRoiDrawTarget(null);
        setActiveRoiRegionByStream({ main: 0, sub: 0 });
        setSaved('已恢复默认值，保存后生效');
    };
    const activeSupported = isStreamSupported(
        config.streams[active],
        activeCapabilities,
    );
    const allSupported =
        (capabilities.streams.main.available === false ||
            isStreamSupported(
                config.streams.main,
                capabilities.streams.main,
            )) &&
        (capabilities.streams.sub.available === false ||
            isStreamSupported(config.streams.sub, capabilities.streams.sub));
    const saveConfig = async () => {
        setRoiDrawTarget(null);
        setSaving(true);
        setPreviewEnabled(false);
        window.clearTimeout(refreshTimerRef.current);
        window.clearTimeout(previewTimerRef.current);
        try {
            await saveVideoConfig(config);
            setSaved('已提交保存');
            await refreshStatuses();
            refreshTimerRef.current = window.setTimeout(
                () => void refreshStatuses(),
                2500,
            );
        } catch (err: unknown) {
            const message = err instanceof Error ? err.message : '保存失败';
            try {
                await reloadConfig();
                await refreshStatuses();
                setSaved(`保存失败，已恢复当前生效配置：${message}`);
            } catch {
                setSaved(`保存失败：${message}`);
            }
        } finally {
            setSaving(false);
            previewTimerRef.current = window.setTimeout(
                () => setPreviewEnabled(true),
                2500,
            );
        }
    };

    return (
        <div className="config-preview-layout">
            <section className="panel settings-column">
                <div className="page-heading">
                    <div>
                        <h2>视频参数</h2>
                        <p>
                            主码流用于高清预览和协议输出，子码流用于低码率预览。
                        </p>
                    </div>
                </div>
                <div className="tabs">
                    <button
                        type="button"
                        className={active === 'main' ? 'active' : ''}
                        onClick={() => changeActiveStream('main')}
                    >
                        主码流
                    </button>
                    <button
                        type="button"
                        className={active === 'sub' ? 'active' : ''}
                        disabled={capabilities.streams.sub.available === false}
                        onClick={() => changeActiveStream('sub')}
                    >
                        子码流
                    </button>
                </div>
                <VideoStreamForm
                    activeRoiRegionIndex={activeRoiRegionIndex}
                    stream={config.streams[active]}
                    capabilities={capabilities.streams[active]}
                    roiDrawing={roiDrawing}
                    onChange={(stream) => updateStream(active, stream)}
                    onRoiRegionSelect={selectRoiRegion}
                    onStartRoiDraw={startRoiDraw}
                />
                <div className="form-actions">
                    <button type="button" onClick={resetDefault}>
                        恢复默认
                    </button>
                    <button
                        type="button"
                        className="primary"
                        disabled={!allSupported || saving}
                        onClick={() => void saveConfig()}
                    >
                        {saving ? '保存中' : '保存'}
                    </button>
                </div>
                {!activeSupported && (
                    <div className="save-hint">
                        当前码流包含设备不支持的参数。
                    </div>
                )}
                {saved && <div className="save-hint">{saved}</div>}
                {error && <div className="status-note error-note">{error}</div>}
            </section>
            <VideoPreview
                stream={active}
                statuses={previewStatuses}
                previewUrls={previewUrls}
                onStreamChange={changeActiveStream}
                enabled={previewEnabled}
                surfaceOverlay={
                    <VideoRegionDrawLayer
                        className="roi-draw-layer"
                        disabled={!activeRoiSupported || saving}
                        drawing={roiDrawing}
                        frame={activeFrame}
                        items={roiRegionItems}
                        onDrawStart={(point) => {
                            const regionIndex =
                                activeRoiRegionIndex >= 0 &&
                                activeRoiRegionIndex < activeRoiRegions.length
                                    ? activeRoiRegionIndex
                                    : activeRoiRegions.length;
                            updateRoiRegionFromPreview(
                                regionIndex,
                                roiRegionFromPoints(
                                    activeFrame,
                                    activeRoiRegions[regionIndex],
                                    point,
                                    point,
                                ),
                            );
                            return { regionIndex, start: point };
                        }}
                        onDrawMove={(drag, point) => {
                            updateRoiRegionFromPreview(
                                drag.regionIndex,
                                roiRegionFromPoints(
                                    activeFrame,
                                    activeRoiRegions[drag.regionIndex],
                                    drag.start,
                                    point,
                                ),
                            );
                        }}
                        onDrawEnd={(drag) => finishRoiDraw(drag.regionIndex)}
                    />
                }
                webrtcConfig={webrtcConfig}
            />
        </div>
    );
}

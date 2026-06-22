import {
    useCallback,
    useEffect,
    useLayoutEffect,
    useMemo,
    useRef,
    useState,
    type PointerEvent as ReactPointerEvent,
} from 'react';
import { saveAiConfig } from '../../api/ai';
import type {
    AiModelConfig,
    AiPerimeterRegion,
    AiStatus,
    StreamName,
} from '../../api/types';
import { VideoPreview } from '../../components/VideoPreview';
import { useVideoConfig } from '../../hooks/useVideoConfig';
import { normalizeAiRootConfigForSave } from './aiAlertFormat';
import { isAiTaskAvailable, taskUnavailableText } from './aiAlertTasks';

interface AiPerimeterEditorProps {
    status: AiStatus | null;
    onSaved: () => Promise<void>;
}

interface DragState {
    region_index: number;
    start_x: number;
    start_y: number;
    region_name: string;
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

const kMinimumRegionSize = 0.01;

const streamLabel = (stream: StreamName) =>
    stream === 'main' ? '主码流' : '子码流';

function clampUnit(value: number) {
    if (!Number.isFinite(value)) {
        return 0;
    }
    return Math.min(1, Math.max(0, value));
}

function clampRegionStart(value: number) {
    return Math.min(1 - kMinimumRegionSize, clampUnit(value));
}

function formatPercent(value: number) {
    return `${Math.round(clampUnit(value) * 100)}%`;
}

function parseResolution(resolution: string | undefined): FrameSize {
    const [width, height] = (resolution || '')
        .split('x')
        .map((value) => Number(value));
    if (
        !Number.isFinite(width) ||
        !Number.isFinite(height) ||
        width <= 0 ||
        height <= 0
    ) {
        return { width: 16, height: 9 };
    }
    return { width, height };
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

function normalizeRegion(
    region: AiPerimeterRegion,
    index: number,
): AiPerimeterRegion {
    const x = clampRegionStart(region.x);
    const y = clampRegionStart(region.y);
    const right = Math.max(
        x + kMinimumRegionSize,
        clampUnit(region.x + Math.max(0, region.width)),
    );
    const bottom = Math.max(
        y + kMinimumRegionSize,
        clampUnit(region.y + Math.max(0, region.height)),
    );
    return {
        name: region.name || `region-${index + 1}`,
        x,
        y,
        width: Math.min(right - x, 1 - x),
        height: Math.min(bottom - y, 1 - y),
    };
}

function regionFromPoints(
    name: string,
    start: { x: number; y: number },
    end: { x: number; y: number },
): AiPerimeterRegion {
    const x = clampRegionStart(Math.min(start.x, end.x));
    const y = clampRegionStart(Math.min(start.y, end.y));
    const width = Math.max(kMinimumRegionSize, Math.abs(end.x - start.x));
    const height = Math.max(kMinimumRegionSize, Math.abs(end.y - start.y));
    return {
        name,
        x,
        y,
        width: Math.min(width, 1 - x),
        height: Math.min(height, 1 - y),
    };
}

function replaceRegionAt(
    regions: AiPerimeterRegion[],
    index: number,
    nextRegion: AiPerimeterRegion,
) {
    const nextRegions = [...regions];
    nextRegions[index] = nextRegion;
    return nextRegions;
}

function regionRectStyle(region: AiPerimeterRegion, videoArea: SurfaceRect) {
    return {
        left: videoArea.left + region.x * videoArea.width,
        top: videoArea.top + region.y * videoArea.height,
        width: region.width * videoArea.width,
        height: region.height * videoArea.height,
    };
}

function videoAreaStyle(videoArea: SurfaceRect) {
    return {
        left: videoArea.left,
        top: videoArea.top,
        width: videoArea.width,
        height: videoArea.height,
    };
}

function regionsSignature(regions: AiPerimeterRegion[]) {
    return JSON.stringify(
        regions.map((region, index) => normalizeRegion(region, index)),
    );
}

function perimeterTask(status: AiStatus | null): AiModelConfig | null {
    return (
        status?.config.tasks.find(
            (task) => task.task === 'perimeter_detection',
        ) ?? null
    );
}

export function AiPerimeterEditor({ status, onSaved }: AiPerimeterEditorProps) {
    const [activeStream, setActiveStream] = useState<StreamName>('sub');
    const [regions, setRegions] = useState<AiPerimeterRegion[]>([]);
    const [activeRegionIndex, setActiveRegionIndex] = useState(0);
    const [drag, setDrag] = useState<DragState | null>(null);
    const [saving, setSaving] = useState(false);
    const [saveMessage, setSaveMessage] = useState('');
    const [surfaceSize, setSurfaceSize] = useState({ width: 0, height: 0 });
    const drawRef = useRef<HTMLDivElement | null>(null);
    const draftDirtyRef = useRef(false);
    const lastStatusConfigRef = useRef('');
    const pendingDragPointRef = useRef<{ x: number; y: number } | null>(null);
    const dragFrameRef = useRef(0);
    const [drawLayer, setDrawLayer] = useState<HTMLDivElement | null>(null);
    const {
        config: videoConfig,
        statuses,
        previewUrls,
        loading: videoLoading,
        error: videoError,
    } = useVideoConfig(activeStream);
    const activeStatus = statuses.find((item) => item.stream === activeStream);
    const frameResolution =
        activeStatus?.resolution ||
        videoConfig?.streams[activeStream]?.resolution;
    const frame = useMemo(
        () => parseResolution(frameResolution),
        [frameResolution],
    );
    const videoArea =
        surfaceSize.width > 0 && surfaceSize.height > 0
            ? contentAreaForSurface(frame, surfaceSize)
            : null;
    const pendingNewRegion = activeRegionIndex >= regions.length;
    const activeRegion = pendingNewRegion ? null : regions[activeRegionIndex];
    const sourceState = videoLoading
        ? '加载中'
        : activeStatus?.running
          ? '运行中'
          : '未运行';
    const perimeterConfig = perimeterTask(status);
    const perimeterAvailable = isAiTaskAvailable('perimeter_detection');

    useEffect(() => {
        const nextPerimeterConfig = perimeterTask(status);
        if (!nextPerimeterConfig) {
            return;
        }
        const nextRegions = nextPerimeterConfig.perimeter_regions.map(
            (region: AiPerimeterRegion, index: number) =>
                normalizeRegion(region, index),
        );
        const nextConfigSignature = [
            nextPerimeterConfig.stream,
            regionsSignature(nextRegions),
        ].join('|');
        if (draftDirtyRef.current && lastStatusConfigRef.current !== '') {
            return;
        }
        if (lastStatusConfigRef.current === nextConfigSignature) {
            return;
        }
        lastStatusConfigRef.current = nextConfigSignature;
        setActiveStream(nextPerimeterConfig.stream);
        setRegions(nextRegions);
        setActiveRegionIndex(0);
        setSaveMessage('');
    }, [status]);

    useEffect(
        () => () => {
            if (dragFrameRef.current !== 0) {
                window.cancelAnimationFrame(dragFrameRef.current);
            }
        },
        [],
    );

    const markDraftDirty = () => {
        draftDirtyRef.current = true;
    };

    const applyDragPoint = useCallback(
        (dragState: DragState, point: { x: number; y: number }) => {
            setRegions((currentRegions) =>
                replaceRegionAt(
                    currentRegions,
                    dragState.region_index,
                    regionFromPoints(
                        dragState.region_name,
                        { x: dragState.start_x, y: dragState.start_y },
                        point,
                    ),
                ),
            );
        },
        [],
    );

    const setDrawLayerRef = useCallback((node: HTMLDivElement | null) => {
        drawRef.current = node;
        setDrawLayer(node);
    }, []);

    useLayoutEffect(() => {
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
    }, [drawLayer]);

    const pointerToRegionPoint = (event: ReactPointerEvent<HTMLDivElement>) => {
        const surface = drawRef.current?.getBoundingClientRect();
        if (!surface) {
            return null;
        }
        const contentArea = contentAreaForSurface(frame, {
            width: surface.width,
            height: surface.height,
        });
        if (!contentArea) {
            return null;
        }
        const x = Math.min(
            contentArea.width,
            Math.max(0, event.clientX - surface.left - contentArea.left),
        );
        const y = Math.min(
            contentArea.height,
            Math.max(0, event.clientY - surface.top - contentArea.top),
        );
        return {
            x: x / contentArea.width,
            y: y / contentArea.height,
        };
    };

    const beginDraw = (event: ReactPointerEvent<HTMLDivElement>) => {
        if (!perimeterAvailable) {
            return;
        }
        if (event.button !== 0) {
            return;
        }
        const point = pointerToRegionPoint(event);
        if (!point) {
            return;
        }
        const regionIndex = pendingNewRegion
            ? regions.length
            : activeRegionIndex;
        const regionName =
            regions[regionIndex]?.name || `region-${regionIndex + 1}`;
        markDraftDirty();
        setDrag({
            region_index: regionIndex,
            start_x: point.x,
            start_y: point.y,
            region_name: regionName,
        });
        setActiveRegionIndex(regionIndex);
        setSaveMessage('');
        setRegions((currentRegions) =>
            replaceRegionAt(
                currentRegions,
                regionIndex,
                regionFromPoints(regionName, point, point),
            ),
        );
        event.currentTarget.setPointerCapture(event.pointerId);
    };

    const updateDraw = (event: ReactPointerEvent<HTMLDivElement>) => {
        if (!drag) {
            return;
        }
        const point = pointerToRegionPoint(event);
        if (!point) {
            return;
        }
        pendingDragPointRef.current = point;
        if (dragFrameRef.current !== 0) {
            return;
        }
        dragFrameRef.current = window.requestAnimationFrame(() => {
            dragFrameRef.current = 0;
            const pendingPoint = pendingDragPointRef.current;
            pendingDragPointRef.current = null;
            if (!pendingPoint) {
                return;
            }
            applyDragPoint(drag, pendingPoint);
        });
    };

    const finishDraw = (event: ReactPointerEvent<HTMLDivElement>) => {
        if (drag && event.currentTarget.hasPointerCapture(event.pointerId)) {
            event.currentTarget.releasePointerCapture(event.pointerId);
        }
        if (drag && pendingDragPointRef.current) {
            applyDragPoint(drag, pendingDragPointRef.current);
            pendingDragPointRef.current = null;
        }
        if (dragFrameRef.current !== 0) {
            window.cancelAnimationFrame(dragFrameRef.current);
            dragFrameRef.current = 0;
        }
        setDrag(null);
    };

    const clearRegions = () => {
        if (!perimeterAvailable) {
            return;
        }
        markDraftDirty();
        setRegions([]);
        setActiveRegionIndex(0);
        setSaveMessage('');
    };

    const deleteRegion = (index: number) => {
        if (!perimeterAvailable) {
            return;
        }
        markDraftDirty();
        const nextRegions = regions.filter(
            (_, itemIndex) => itemIndex !== index,
        );
        setRegions(nextRegions);
        setActiveRegionIndex((currentIndex) =>
            Math.max(0, Math.min(currentIndex, nextRegions.length - 1)),
        );
        setSaveMessage('');
    };

    const saveRegions = () => {
        if (!status || !perimeterConfig) {
            return;
        }
        if (!perimeterAvailable) {
            setSaveMessage(taskUnavailableText('perimeter_detection'));
            return;
        }
        setSaving(true);
        setSaveMessage('');
        const normalizedRegions = regions.map((region, index) =>
            normalizeRegion(region, index),
        );
        const nextConfig = {
            ...status.config,
            tasks: status.config.tasks.map((task) =>
                task.task === 'perimeter_detection'
                    ? {
                          ...task,
                          stream: activeStream,
                          perimeter_regions: normalizedRegions,
                      }
                    : task,
            ),
        };
        void saveAiConfig(normalizeAiRootConfigForSave(nextConfig))
            .then(onSaved)
            .then(() => {
                draftDirtyRef.current = false;
                lastStatusConfigRef.current = [
                    activeStream,
                    regionsSignature(normalizedRegions),
                ].join('|');
                setRegions(normalizedRegions);
                setSaveMessage(
                    perimeterConfig.enabled
                        ? '已保存并应用'
                        : '已保存周界区域，启用周界任务后生效',
                );
            })
            .catch((err: unknown) => {
                setSaveMessage(err instanceof Error ? err.message : '保存失败');
            })
            .finally(() => setSaving(false));
    };

    const perimeterOverlay = (
        <div
            ref={setDrawLayerRef}
            className="ai-perimeter-draw-layer"
            onPointerDown={beginDraw}
            onPointerMove={updateDraw}
            onPointerUp={finishDraw}
            onPointerCancel={finishDraw}
        >
            {videoArea ? (
                <div
                    className="ai-perimeter-video-area"
                    style={videoAreaStyle(videoArea)}
                />
            ) : null}
            {videoArea
                ? regions.map((region, index) => (
                      <div
                          className={
                              activeRegionIndex === index
                                  ? 'ai-perimeter-region active'
                                  : 'ai-perimeter-region'
                          }
                          key={`${region.name}-${index}`}
                          style={regionRectStyle(region, videoArea)}
                      >
                          <span>{index + 1}</span>
                      </div>
                  ))
                : null}
        </div>
    );

    if (!status || !perimeterConfig) {
        return (
            <section className="ai-perimeter-editor">
                <div className="empty-state">加载周界配置...</div>
            </section>
        );
    }

    return (
        <section className="ai-perimeter-editor">
            <div className="ai-perimeter-heading">
                <div>
                    <h3>周界事件源</h3>
                    <p>
                        {perimeterConfig.enabled
                            ? perimeterConfig.model_path
                            : perimeterAvailable
                              ? '当前未启用周界任务'
                              : taskUnavailableText('perimeter_detection')}
                    </p>
                </div>
                <div className="ai-perimeter-source">
                    <span>事件源</span>
                    <strong>{streamLabel(activeStream)}</strong>
                    <em>{sourceState}</em>
                </div>
            </div>
            <div className="ai-perimeter-layout">
                <div className="ai-perimeter-video">
                    <VideoPreview
                        stream={activeStream}
                        statuses={statuses}
                        previewUrls={previewUrls}
                        onStreamChange={(nextStream) => {
                            if (!perimeterAvailable) {
                                return;
                            }
                            markDraftDirty();
                            setActiveStream(nextStream);
                            setSaveMessage('');
                        }}
                        surfaceOverlay={perimeterOverlay}
                    />
                </div>
                <aside className="ai-perimeter-controls">
                    <div className="panel-title">周界区域</div>
                    <div className="ai-region-source-row">
                        <span>{streamLabel(activeStream)}</span>
                        <span>
                            {frame.width}x{frame.height}
                        </span>
                        <span>
                            {regions.length > 0
                                ? `${regions.length} 个区域`
                                : '整幅画面'}
                        </span>
                    </div>
                    <div className="ai-region-current">
                        <span>当前</span>
                        <strong>
                            {activeRegion
                                ? activeRegion.name
                                : pendingNewRegion
                                  ? `region-${regions.length + 1}`
                                  : '整幅画面'}
                        </strong>
                        {activeRegion ? (
                            <em>
                                x {formatPercent(activeRegion.x)} / y{' '}
                                {formatPercent(activeRegion.y)} / w{' '}
                                {formatPercent(activeRegion.width)} / h{' '}
                                {formatPercent(activeRegion.height)}
                            </em>
                        ) : null}
                    </div>
                    <div className="ai-region-list">
                        {regions.length === 0 ? (
                            <div className="ai-region-empty">整幅画面</div>
                        ) : (
                            regions.map((region, index) => (
                                <button
                                    type="button"
                                    disabled={!perimeterAvailable}
                                    className={
                                        activeRegionIndex === index
                                            ? 'active'
                                            : ''
                                    }
                                    key={`${region.name}-${index}`}
                                    onClick={() => {
                                        setActiveRegionIndex(index);
                                        setSaveMessage('');
                                    }}
                                >
                                    <strong>{region.name}</strong>
                                    <span>
                                        x {formatPercent(region.x)} / y{' '}
                                        {formatPercent(region.y)}
                                    </span>
                                    <span>
                                        w {formatPercent(region.width)} / h{' '}
                                        {formatPercent(region.height)}
                                    </span>
                                </button>
                            ))
                        )}
                    </div>
                    <div className="ai-region-actions">
                        <button
                            type="button"
                            disabled={!perimeterAvailable}
                            onClick={() => {
                                markDraftDirty();
                                setActiveRegionIndex(regions.length);
                                setSaveMessage('');
                            }}
                        >
                            新增区域
                        </button>
                        <button
                            type="button"
                            disabled={!perimeterAvailable || regions.length === 0}
                            onClick={clearRegions}
                        >
                            清空
                        </button>
                        <button
                            type="button"
                            disabled={!perimeterAvailable || !activeRegion}
                            onClick={() => deleteRegion(activeRegionIndex)}
                        >
                            删除当前
                        </button>
                        <button
                            type="button"
                            className="primary"
                            disabled={saving || !perimeterAvailable}
                            onClick={saveRegions}
                        >
                            {saving ? '设置中' : '保存'}
                        </button>
                    </div>
                    {saveMessage ? (
                        <div className="save-hint">{saveMessage}</div>
                    ) : null}
                    {videoError ? (
                        <div className="status-note error-note">
                            {videoError}
                        </div>
                    ) : null}
                </aside>
            </div>
        </section>
    );
}

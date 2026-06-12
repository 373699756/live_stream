import {
    useCallback,
    useEffect,
    useLayoutEffect,
    useMemo,
    useRef,
    useState,
    type PointerEvent as ReactPointerEvent,
} from 'react';
import { aiAlertImageUrl, saveAiConfig } from '../api/ai';
import type {
    AiAlertRecord,
    AiConfig,
    AiModelConfig,
    AiPerimeterRegion,
    AiStats,
    AiTaskName,
    AiTaskStatus,
    StreamName,
} from '../api/types';
import { AiDetectionOverlay } from '../components/AiDetectionOverlay';
import { StatusBadge } from '../components/StatusBadge';
import { VideoPreview } from '../components/VideoPreview';
import { useAiAlerts } from '../hooks/useAiAlerts';
import { useLiveView } from '../hooks/useLiveView';
import {
    latestAlarmTimeText,
    normalizeAiRootConfigForSave,
} from '../features/ai-alerts/aiAlertFormat';
import { taskLabel } from '../features/ai-alerts/aiAlertTasks';
import { formatTimestamp } from '../utils/format';
import '../styles/ai-alerts.css';

type SensitivityLevel = 'low' | 'medium' | 'high';

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

const kTaskOrder: AiTaskName[] = [
    'object_detection',
    'perimeter_detection',
    'motion_classification',
    'occlusion_detection',
];

const kSensitivityOptions: Array<{
    label: string;
    threshold: number;
    value: SensitivityLevel;
}> = [
    { label: '低', threshold: 0.7, value: 'low' },
    { label: '中', threshold: 0.5, value: 'medium' },
    { label: '高', threshold: 0.35, value: 'high' },
];

const kMinimumRegionSize = 0.01;

function defaultTaskConfig(task: AiTaskName): AiModelConfig {
    return {
        enabled: false,
        backend: 'hisi3516dv300_nnie',
        task,
        stream: 'sub',
        model_path:
            task === 'object_detection' || task === 'perimeter_detection'
                ? 'models/inst_ssd_cycle.wk'
                : '',
        input_width: 300,
        input_height: 300,
        inference_interval_ms: 500,
        confidence_threshold: 0.5,
        max_results: 16,
        perimeter_regions: [],
    };
}

function cloneTaskConfig(task: AiModelConfig): AiModelConfig {
    return {
        ...task,
        perimeter_regions: (task.perimeter_regions ?? []).map((region) => ({
            ...region,
        })),
    };
}

function positiveConfigInteger(value: number, fallback: number) {
    return Number.isFinite(value) && value > 0 ? Math.round(value) : fallback;
}

function sharedHiddenTaskConfig(config: AiConfig) {
    const fallback = defaultTaskConfig('object_detection');
    const source =
        draftTaskByName(config, 'object_detection') ??
        config.tasks[0] ??
        fallback;
    return {
        backend: source.backend,
        stream: source.stream,
        model_path: source.model_path.trim() || fallback.model_path,
        input_width: positiveConfigInteger(
            source.input_width,
            fallback.input_width,
        ),
        input_height: positiveConfigInteger(
            source.input_height,
            fallback.input_height,
        ),
        inference_interval_ms: positiveConfigInteger(
            source.inference_interval_ms,
            fallback.inference_interval_ms,
        ),
        max_results: positiveConfigInteger(
            source.max_results,
            fallback.max_results,
        ),
    };
}

function withSharedHiddenDefaults(config: AiConfig): AiConfig {
    const shared = sharedHiddenTaskConfig(config);
    const threshold = thresholdForSensitivity(sensitivityForConfig(config));
    return {
        ...config,
        tasks: config.tasks.map((task) => {
            const fallback = defaultTaskConfig(task.task);
            return {
                ...fallback,
                ...task,
                ...shared,
                confidence_threshold: threshold,
                perimeter_regions:
                    task.task === 'perimeter_detection'
                        ? (task.perimeter_regions ?? [])
                        : [],
            };
        }),
    };
}

function completeAiConfig(config: AiConfig): AiConfig {
    const tasks = kTaskOrder.map((taskName) =>
        cloneTaskConfig(
            config.tasks.find((task) => task.task === taskName) ??
                defaultTaskConfig(taskName),
        ),
    );
    return withSharedHiddenDefaults({
        ...config,
        enabled: tasks.some((task) => task.enabled),
        tasks,
    });
}

function emptyStats(): AiStats {
    return {
        enabled: false,
        backend_available: false,
        alarm_linked: false,
        last_success_time_ms: 0,
        last_failure_time_ms: 0,
        received_frames: 0,
        skipped_frames: 0,
        inference_count: 0,
        inference_failed_count: 0,
        dropped_tasks: 0,
        last_inference_time_ms: 0,
        max_inference_time_ms: 0,
        average_inference_time_ms: 0,
        active_results: 0,
    };
}

function streamLabel(stream: StreamName) {
    return stream === 'main' ? '主码流' : '子码流';
}

function numberText(value: number) {
    return Number.isFinite(value) ? String(Math.round(value)) : '--';
}

function formatPercent(value: number) {
    return `${Math.round(clampUnit(value) * 100)}%`;
}

function maxConfidence(alert: AiAlertRecord) {
    return `${Math.round(alert.confidence_max * 100)}%`;
}

function taskByName(tasks: AiTaskStatus[], name: AiTaskName) {
    return tasks.find((task) => task.config.task === name);
}

function draftTaskByName(config: AiConfig | null, name: AiTaskName) {
    return config?.tasks.find((task) => task.task === name);
}

function updateTaskConfig(
    config: AiConfig,
    taskName: AiTaskName,
    patch: Partial<AiModelConfig>,
): AiConfig {
    return {
        ...config,
        tasks: config.tasks.map((task) =>
            task.task === taskName ? { ...task, ...patch } : task,
        ),
    };
}

function taskStatusText(task: AiTaskStatus | undefined) {
    if (!task) {
        return '未配置';
    }
    if (!task.config.enabled) {
        return '关闭';
    }
    if (!task.stats.enabled) {
        return '未运行';
    }
    if (!task.stats.backend_available) {
        return '后端异常';
    }
    return '运行';
}

function taskBadgeState(task: AiTaskStatus | undefined) {
    if (!task || !task.config.enabled || !task.stats.enabled) {
        return 'pending' as const;
    }
    return task.stats.backend_available
        ? ('running' as const)
        : ('error' as const);
}

function sensitivityFromThreshold(threshold: number): SensitivityLevel {
    let closest = kSensitivityOptions[1];
    for (const option of kSensitivityOptions) {
        if (
            Math.abs(option.threshold - threshold) <
            Math.abs(closest.threshold - threshold)
        ) {
            closest = option;
        }
    }
    return closest.value;
}

function thresholdForSensitivity(value: SensitivityLevel) {
    return (
        kSensitivityOptions.find((option) => option.value === value) ??
        kSensitivityOptions[1]
    ).threshold;
}

function sensitivityForConfig(config: AiConfig | null): SensitivityLevel {
    const sourceTask =
        draftTaskByName(config, 'object_detection') ??
        draftTaskByName(config, 'perimeter_detection') ??
        config?.tasks[0];
    return sensitivityFromThreshold(sourceTask?.confidence_threshold ?? 0.5);
}

function alertCountsByTask(alerts: AiAlertRecord[]) {
    const counts: Record<AiTaskName, number> = {
        object_detection: 0,
        perimeter_detection: 0,
        motion_classification: 0,
        occlusion_detection: 0,
    };
    alerts.forEach((alert) => {
        counts[alert.task] += 1;
    });
    return counts;
}

function clampUnit(value: number) {
    if (!Number.isFinite(value)) {
        return 0;
    }
    return Math.min(1, Math.max(0, value));
}

function clampRegionStart(value: number) {
    return Math.min(1 - kMinimumRegionSize, clampUnit(value));
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
    return nextRegions.map(normalizeRegion);
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

function SnapshotRail({ alerts }: { alerts: AiAlertRecord[] }) {
    const latestAlerts = alerts
        .slice()
        .sort((left, right) => right.timestamp_ms - left.timestamp_ms)
        .slice(0, 10);

    return (
        <aside className="ai-snapshot-rail" aria-label="AI 实时抓图">
            <div className="ai-snapshot-rail-header">
                <div>
                    <h3>实时抓图</h3>
                    <span>最新 {latestAlerts.length}/10</span>
                </div>
            </div>
            <div className="ai-snapshot-list">
                {latestAlerts.length === 0 ? (
                    <div className="ai-snapshot-empty">暂无抓图</div>
                ) : (
                    latestAlerts.map((alert) => {
                        const imageUrl = aiAlertImageUrl(
                            alert.image_url,
                            alert.timestamp_ms,
                        );
                        return (
                            <button
                                type="button"
                                className="ai-snapshot-card"
                                key={alert.id}
                                onClick={() =>
                                    window.open(
                                        imageUrl,
                                        '_blank',
                                        'noopener,noreferrer',
                                    )
                                }
                            >
                                <img
                                    alt={`${taskLabel(alert.task)} ${alert.id}`}
                                    src={imageUrl}
                                />
                                <span className="ai-snapshot-card-body">
                                    <strong>{taskLabel(alert.task)}</strong>
                                    <em>
                                        {formatTimestamp(alert.timestamp_ms)}
                                    </em>
                                    <span>{streamLabel(alert.stream)}</span>
                                    <span>{alert.detection_count} 个目标</span>
                                    <span>{maxConfidence(alert)}</span>
                                </span>
                            </button>
                        );
                    })
                )}
            </div>
        </aside>
    );
}

export function AiAlertsPage() {
    const [stream, setStream] = useState<StreamName>('sub');
    const [draft, setDraft] = useState<AiConfig | null>(null);
    const [dirty, setDirty] = useState(false);
    const [saving, setSaving] = useState(false);
    const [saveMessage, setSaveMessage] = useState('');
    const [editingPerimeter, setEditingPerimeter] = useState(false);
    const [activeRegionIndex, setActiveRegionIndex] = useState(0);
    const [drag, setDrag] = useState<DragState | null>(null);
    const [surfaceSize, setSurfaceSize] = useState({ width: 0, height: 0 });
    const [drawLayer, setDrawLayer] = useState<HTMLDivElement | null>(null);
    const drawRef = useRef<HTMLDivElement | null>(null);
    const pendingDragPointRef = useRef<{ x: number; y: number } | null>(null);
    const dragFrameRef = useRef(0);
    const perimeterTask = draftTaskByName(draft, 'perimeter_detection');
    const previewStream =
        editingPerimeter && perimeterTask ? perimeterTask.stream : stream;
    const { statuses, previewUrls } = useLiveView(previewStream);
    const {
        status,
        alarmStatus,
        lastAlarmEvent,
        alerts,
        loading,
        error,
        refresh,
    } = useAiAlerts();
    const activeStatus = statuses.find((item) => item.stream === previewStream);
    const summary = status?.summary ?? emptyStats();
    const frame = useMemo(
        () => parseResolution(activeStatus?.resolution),
        [activeStatus?.resolution],
    );
    const videoArea =
        surfaceSize.width > 0 && surfaceSize.height > 0
            ? contentAreaForSurface(frame, surfaceSize)
            : null;
    const sensitivity = sensitivityForConfig(draft);
    const alertCounts = useMemo(() => alertCountsByTask(alerts), [alerts]);
    const perimeterRegions = perimeterTask?.perimeter_regions ?? [];
    const activeRegion =
        activeRegionIndex >= 0 && activeRegionIndex < perimeterRegions.length
            ? perimeterRegions[activeRegionIndex]
            : null;
    const orderedTaskStatuses = useMemo(
        () => kTaskOrder.map((task) => taskByName(status?.tasks ?? [], task)),
        [status],
    );

    useEffect(() => {
        if (!status || dirty) {
            return;
        }
        setDraft(completeAiConfig(status.config));
        setSaveMessage('');
    }, [dirty, status]);

    useEffect(() => {
        if (perimeterRegions.length === 0 && activeRegionIndex !== 0) {
            setActiveRegionIndex(0);
            return;
        }
        if (
            perimeterRegions.length > 0 &&
            activeRegionIndex >= perimeterRegions.length
        ) {
            setActiveRegionIndex(perimeterRegions.length - 1);
        }
    }, [activeRegionIndex, perimeterRegions.length]);

    useEffect(
        () => () => {
            if (dragFrameRef.current !== 0) {
                window.cancelAnimationFrame(dragFrameRef.current);
            }
        },
        [],
    );

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

    const setDrawLayerRef = useCallback((node: HTMLDivElement | null) => {
        drawRef.current = node;
        setDrawLayer(node);
    }, []);

    const updateDraftWith = useCallback(
        (updater: (config: AiConfig) => AiConfig) => {
            setDraft((current) => (current ? updater(current) : current));
            setDirty(true);
            setSaveMessage('');
        },
        [],
    );

    const updateTaskEnabled = (taskName: AiTaskName, enabled: boolean) => {
        updateDraftWith((current) =>
            updateTaskConfig(current, taskName, { enabled }),
        );
    };

    const updateSensitivity = (nextSensitivity: SensitivityLevel) => {
        const threshold = thresholdForSensitivity(nextSensitivity);
        updateDraftWith((current) => ({
            ...current,
            tasks: current.tasks.map((task) => ({
                ...task,
                confidence_threshold: threshold,
            })),
        }));
    };

    const selectRegion = (index: number) => {
        setActiveRegionIndex(index);
    };

    const updatePerimeterRegions = useCallback(
        (updater: (regions: AiPerimeterRegion[]) => AiPerimeterRegion[]) => {
            updateDraftWith((current) => {
                const task = draftTaskByName(current, 'perimeter_detection');
                if (!task) {
                    return current;
                }
                return updateTaskConfig(current, 'perimeter_detection', {
                    perimeter_regions: updater(
                        task.perimeter_regions ?? [],
                    ).map(normalizeRegion),
                });
            });
        },
        [updateDraftWith],
    );

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

    const applyDragPoint = useCallback(
        (dragState: DragState, point: { x: number; y: number }) => {
            updatePerimeterRegions((currentRegions) =>
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
        [updatePerimeterRegions],
    );

    const beginDraw = (event: ReactPointerEvent<HTMLDivElement>) => {
        if (!editingPerimeter || event.button !== 0 || !draft) {
            return;
        }
        const point = pointerToRegionPoint(event);
        if (!point) {
            return;
        }
        const regionIndex =
            activeRegionIndex >= perimeterRegions.length
                ? perimeterRegions.length
                : activeRegionIndex;
        const regionName =
            perimeterRegions[regionIndex]?.name || `region-${regionIndex + 1}`;
        setDrag({
            region_index: regionIndex,
            start_x: point.x,
            start_y: point.y,
            region_name: regionName,
        });
        setActiveRegionIndex(regionIndex);
        setSaveMessage('');
        updatePerimeterRegions((currentRegions) =>
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
            if (pendingPoint) {
                applyDragPoint(drag, pendingPoint);
            }
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

    const addRegion = () => {
        setEditingPerimeter(true);
        setActiveRegionIndex(perimeterRegions.length);
        setSaveMessage('');
    };

    const clearRegions = () => {
        updatePerimeterRegions(() => []);
        setActiveRegionIndex(0);
    };

    const deleteRegion = () => {
        if (!activeRegion) {
            return;
        }
        updatePerimeterRegions((currentRegions) =>
            currentRegions.filter((_, index) => index !== activeRegionIndex),
        );
        setActiveRegionIndex((currentIndex) => Math.max(0, currentIndex - 1));
    };

    const captureSnapshot = (nextStream: StreamName) => {
        const snapshot =
            previewUrls?.stream === nextStream ? previewUrls.snapshot : '';
        if (!snapshot) {
            return;
        }
        const separator = snapshot.includes('?') ? '&' : '?';
        window.open(
            `${snapshot}${separator}t=${Date.now()}`,
            '_blank',
            'noopener,noreferrer',
        );
    };

    const handleStreamChange = (nextStream: StreamName) => {
        if (editingPerimeter) {
            return;
        }
        setStream(nextStream);
    };

    const togglePerimeterEdit = () => {
        if (!editingPerimeter && perimeterTask) {
            setStream(perimeterTask.stream);
        }
        setEditingPerimeter((current) => !current);
    };

    const restoreDraft = () => {
        if (!status) {
            return;
        }
        setDraft(completeAiConfig(status.config));
        setDirty(false);
        setSaveMessage('');
    };

    const saveDraft = () => {
        if (!draft) {
            return;
        }
        setSaving(true);
        setSaveMessage('');
        const nextConfig = normalizeAiRootConfigForSave(
            withSharedHiddenDefaults(draft),
        );
        void saveAiConfig(nextConfig)
            .then(refresh)
            .then(() => {
                setDirty(false);
                setDraft(completeAiConfig(nextConfig));
                setSaveMessage('已保存并应用');
            })
            .catch((err: unknown) => {
                setSaveMessage(err instanceof Error ? err.message : '保存失败');
            })
            .finally(() => setSaving(false));
    };

    const perimeterOverlay = (
        <div
            ref={setDrawLayerRef}
            className={
                editingPerimeter
                    ? 'ai-perimeter-draw-layer editing'
                    : 'ai-perimeter-draw-layer'
            }
            onPointerDown={beginDraw}
            onPointerMove={updateDraw}
            onPointerUp={finishDraw}
            onPointerCancel={finishDraw}
        >
            {videoArea && editingPerimeter ? (
                <div
                    className="ai-perimeter-video-area"
                    style={videoAreaStyle(videoArea)}
                />
            ) : null}
            {videoArea
                ? perimeterRegions.map((region, index) => {
                      const regionClassName = [
                          'ai-perimeter-region',
                          activeRegionIndex === index ? 'active' : '',
                          editingPerimeter ? 'selectable' : '',
                      ]
                          .filter(Boolean)
                          .join(' ');
                      return (
                          <div
                              className={regionClassName}
                              key={`${region.name}-${index}`}
                              style={regionRectStyle(region, videoArea)}
                          >
                              <span
                                  role={editingPerimeter ? 'button' : undefined}
                                  tabIndex={editingPerimeter ? 0 : -1}
                                  onKeyDown={(event) => {
                                      if (
                                          editingPerimeter &&
                                          (event.key === 'Enter' ||
                                              event.key === ' ')
                                      ) {
                                          event.preventDefault();
                                          selectRegion(index);
                                      }
                                  }}
                                  onPointerDown={(event) => {
                                      if (!editingPerimeter) {
                                          return;
                                      }
                                      event.stopPropagation();
                                      selectRegion(index);
                                  }}
                              >
                                  {index + 1}
                              </span>
                          </div>
                      );
                  })
                : null}
        </div>
    );

    return (
        <div className="page-grid ai-console-page">
            <div className="page-heading ai-console-heading">
                <div>
                    <h2>AI 智能事件</h2>
                    <p>事件开关、周界区域和实时抓图集中管理</p>
                </div>
                <button
                    type="button"
                    className="primary"
                    disabled={loading}
                    onClick={() => {
                        void refresh();
                    }}
                >
                    刷新
                </button>
            </div>

            {error ? (
                <div className="status-note error-note">{error}</div>
            ) : null}

            <div className="ai-console-layout">
                <main className="ai-console-main">
                    <VideoPreview
                        stream={previewStream}
                        statuses={statuses}
                        previewUrls={previewUrls}
                        onStreamChange={handleStreamChange}
                        onSnapshot={captureSnapshot}
                        surfaceOverlay={
                            <>
                                <AiDetectionOverlay
                                    frameResolution={activeStatus?.resolution}
                                    status={status}
                                    stream={previewStream}
                                    error={error}
                                />
                                {perimeterOverlay}
                            </>
                        }
                    />

                    <section className="ai-status-compact">
                        <div className="ai-status-kpis">
                            <div>
                                <span>事件</span>
                                <strong>
                                    {draft?.tasks.filter((task) => task.enabled)
                                        .length ?? 0}{' '}
                                    启用
                                </strong>
                            </div>
                            <div>
                                <span>后端</span>
                                <strong>
                                    {summary.backend_available
                                        ? '可用'
                                        : '异常'}
                                </strong>
                            </div>
                            <div>
                                <span>有效结果</span>
                                <strong>
                                    {numberText(summary.active_results)}
                                </strong>
                            </div>
                            <div>
                                <span>最近耗时</span>
                                <strong>
                                    {numberText(summary.last_inference_time_ms)}{' '}
                                    ms
                                </strong>
                            </div>
                            <div>
                                <span>最近报警</span>
                                <strong>
                                    {latestAlarmTimeText(
                                        alarmStatus,
                                        lastAlarmEvent,
                                    )}
                                </strong>
                            </div>
                        </div>
                    </section>

                    <section className="ai-event-workbench">
                        <div className="ai-event-workbench-header">
                            <div>
                                <h3>事件配置</h3>
                                <span>开关事件，调整全局灵敏度</span>
                            </div>
                            <div
                                className="ai-sensitivity-control"
                                aria-label="AI 灵敏度"
                            >
                                <span>灵敏度</span>
                                <div>
                                    {kSensitivityOptions.map((option) => (
                                        <button
                                            type="button"
                                            className={
                                                sensitivity === option.value
                                                    ? 'active'
                                                    : ''
                                            }
                                            disabled={!draft}
                                            key={option.value}
                                            onClick={() =>
                                                updateSensitivity(option.value)
                                            }
                                        >
                                            {option.label}
                                        </button>
                                    ))}
                                </div>
                            </div>
                        </div>

                        {!draft ? (
                            <div className="empty-state">加载智能配置...</div>
                        ) : (
                            <div className="ai-event-switch-list">
                                {kTaskOrder.map((taskName, index) => {
                                    const task = draftTaskByName(
                                        draft,
                                        taskName,
                                    );
                                    const taskStatus =
                                        orderedTaskStatuses[index];
                                    const enabled = Boolean(task?.enabled);
                                    return (
                                        <article
                                            className="ai-event-switch-row"
                                            key={taskName}
                                        >
                                            <label className="ai-event-switch-main">
                                                <input
                                                    checked={enabled}
                                                    type="checkbox"
                                                    onChange={(event) =>
                                                        updateTaskEnabled(
                                                            taskName,
                                                            event.target
                                                                .checked,
                                                        )
                                                    }
                                                />
                                                <span>
                                                    <strong>
                                                        {taskLabel(taskName)}
                                                    </strong>
                                                    <em>
                                                        {alertCounts[taskName]}{' '}
                                                        张抓图
                                                    </em>
                                                </span>
                                            </label>
                                            <StatusBadge
                                                state={taskBadgeState(
                                                    taskStatus,
                                                )}
                                                label={taskStatusText(
                                                    taskStatus,
                                                )}
                                            />
                                            <div className="ai-event-switch-stats">
                                                <span>
                                                    {taskStatus
                                                        ? streamLabel(
                                                              taskStatus.config
                                                                  .stream,
                                                          )
                                                        : '--'}
                                                </span>
                                                <span>
                                                    {numberText(
                                                        taskStatus?.stats
                                                            .inference_count ??
                                                            0,
                                                    )}{' '}
                                                    次
                                                </span>
                                                <span>
                                                    {numberText(
                                                        taskStatus?.stats
                                                            .active_results ??
                                                            0,
                                                    )}{' '}
                                                    个结果
                                                </span>
                                            </div>
                                            {taskName ===
                                            'perimeter_detection' ? (
                                                <button
                                                    type="button"
                                                    className={
                                                        editingPerimeter
                                                            ? 'active'
                                                            : ''
                                                    }
                                                    onClick={
                                                        togglePerimeterEdit
                                                    }
                                                >
                                                    {editingPerimeter
                                                        ? '完成画框'
                                                        : '编辑区域'}
                                                </button>
                                            ) : null}
                                        </article>
                                    );
                                })}
                            </div>
                        )}

                        {editingPerimeter ? (
                            <div className="ai-perimeter-toolbar">
                                <div>
                                    <strong>周界区域</strong>
                                    <span>
                                        {perimeterRegions.length > 0
                                            ? `${perimeterRegions.length} 个区域`
                                            : '整幅画面'}
                                    </span>
                                    {activeRegion ? (
                                        <em>
                                            当前 {activeRegionIndex + 1}: x{' '}
                                            {formatPercent(activeRegion.x)} / y{' '}
                                            {formatPercent(activeRegion.y)} / w{' '}
                                            {formatPercent(activeRegion.width)}{' '}
                                            / h{' '}
                                            {formatPercent(activeRegion.height)}
                                        </em>
                                    ) : null}
                                    {perimeterRegions.length > 0 ? (
                                        <div
                                            className="ai-perimeter-region-list"
                                            aria-label="周界区域"
                                        >
                                            {perimeterRegions.map(
                                                (region, index) => (
                                                    <button
                                                        type="button"
                                                        className={
                                                            activeRegionIndex ===
                                                            index
                                                                ? 'active'
                                                                : ''
                                                        }
                                                        key={`${region.name}-${index}`}
                                                        onClick={() =>
                                                            selectRegion(index)
                                                        }
                                                    >
                                                        {index + 1}
                                                    </button>
                                                ),
                                            )}
                                        </div>
                                    ) : null}
                                </div>
                                <div className="ai-perimeter-actions">
                                    <button type="button" onClick={addRegion}>
                                        新增区域
                                    </button>
                                    <button
                                        type="button"
                                        disabled={!activeRegion}
                                        onClick={deleteRegion}
                                    >
                                        删除当前
                                    </button>
                                    <button
                                        type="button"
                                        disabled={perimeterRegions.length === 0}
                                        onClick={clearRegions}
                                    >
                                        清空
                                    </button>
                                </div>
                            </div>
                        ) : null}

                        <div className="ai-config-actions">
                            {dirty ? <span>有未保存修改</span> : null}
                            {saveMessage ? <span>{saveMessage}</span> : null}
                            <button
                                type="button"
                                disabled={!draft || saving}
                                onClick={restoreDraft}
                            >
                                恢复
                            </button>
                            <button
                                type="button"
                                className="primary"
                                disabled={!draft || saving}
                                onClick={saveDraft}
                            >
                                {saving ? '保存中' : '保存配置'}
                            </button>
                        </div>
                    </section>
                </main>

                <SnapshotRail alerts={alerts} />
            </div>
        </div>
    );
}

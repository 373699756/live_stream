import {
    useCallback,
    useEffect,
    useMemo,
    useState,
} from 'react';
import { saveAiAlarmRule } from '../api/alarm';
import { aiAlertImageUrl, saveAiConfig } from '../api/ai';
import type {
    AiAlertRecord,
    AiCapabilities,
    AiConfig,
    AiModelConfig,
    AiPerimeterRegion,
    AiStats,
    AiTaskName,
    AiTaskStatus,
    AlarmRuleConfig,
    StreamName,
} from '../api/types';
import { AiDetectionOverlay } from '../components/AiDetectionOverlay';
import { StatusBadge } from '../components/StatusBadge';
import {
    VideoRegionDrawLayer,
    type VideoRegionDrag,
    type VideoRegionPoint,
    type VideoRegionRect,
} from '../components/VideoRegionDrawLayer';
import { VideoPreview } from '../components/VideoPreview';
import { useAiAlerts } from '../hooks/useAiAlerts';
import { useLiveView } from '../hooks/useLiveView';
import {
    latestAlarmTimeText,
    nonNegativeInteger,
    normalizeAiRootConfigForSave,
} from '../features/ai-alerts/aiAlertFormat';
import {
    isAiTaskAvailable,
    taskLabel,
    taskUnavailableText,
} from '../features/ai-alerts/aiAlertTasks';
import { formatTimestamp } from '../utils/format';
import '../styles/ai-alerts.css';

type SensitivityLevel = 'low' | 'medium' | 'high';

interface FrameSize {
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

const kStreamOptions: Array<{ label: string; value: StreamName }> = [
    { label: '子码流', value: 'sub' },
    { label: '主码流', value: 'main' },
];

const kInferenceIntervalOptions = [
    { label: '高频 250 ms', value: 250 },
    { label: '标准 500 ms', value: 500 },
    { label: '低负载 1 s', value: 1000 },
    { label: '巡检 2 s', value: 2000 },
];

const kMaxResultsOptions = [
    { label: '少量 8 个', value: 8 },
    { label: '标准 16 个', value: 16 },
    { label: '更多 32 个', value: 32 },
];

const kAlarmDurationOptions = [
    { label: '立即触发', value: 0 },
    { label: '持续 0.5 s', value: 500 },
    { label: '持续 1 s', value: 1000 },
    { label: '持续 3 s', value: 3000 },
    { label: '持续 5 s', value: 5000 },
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

function optionValue(value: number) {
    return String(Math.round(value));
}

function numericOptionsWithCurrent(
    options: Array<{ label: string; value: number }>,
    currentValue: number,
    label: (value: number) => string,
) {
    const roundedValue = Math.round(currentValue);
    if (options.some((option) => option.value === roundedValue)) {
        return options;
    }
    return [...options, { label: label(roundedValue), value: roundedValue }];
}

function sharedHiddenTaskConfig(
    config: AiConfig,
    capabilities?: AiCapabilities | null,
) {
    const fallback = defaultTaskConfig('object_detection');
    const source =
        config.tasks.find((task) =>
            isAiTaskAvailable(task.task, capabilities),
        ) ??
        draftTaskByName(config, 'object_detection') ??
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

function withSharedHiddenDefaults(
    config: AiConfig,
    capabilities?: AiCapabilities | null,
): AiConfig {
    const shared = sharedHiddenTaskConfig(config, capabilities);
    const threshold = thresholdForSensitivity(
        sensitivityForConfig(config, capabilities),
    );
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

function completeAiConfig(
    config: AiConfig,
    capabilities?: AiCapabilities | null,
): AiConfig {
    const tasks = kTaskOrder.map((taskName) =>
        cloneTaskConfig(
            config.tasks.find((task) => task.task === taskName) ??
                defaultTaskConfig(taskName),
        ),
    );
    return withSharedHiddenDefaults({
        ...config,
        enabled: tasks.some(
            (task) =>
                task.enabled && isAiTaskAvailable(task.task, capabilities),
        ),
        tasks,
    }, capabilities);
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

function taskStatusText(
    task: AiTaskStatus | undefined,
    capabilities?: AiCapabilities | null,
) {
    if (!task) {
        return '未配置';
    }
    if (!isAiTaskAvailable(task.config.task, capabilities)) {
        return taskUnavailableText(task.config.task, capabilities);
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

function taskBadgeState(
    task: AiTaskStatus | undefined,
    capabilities?: AiCapabilities | null,
) {
    if (task && !isAiTaskAvailable(task.config.task, capabilities)) {
        return 'pending' as const;
    }
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

function sensitivityForConfig(
    config: AiConfig | null,
    capabilities?: AiCapabilities | null,
): SensitivityLevel {
    const sourceTask =
        config?.tasks.find((task) =>
            isAiTaskAvailable(task.task, capabilities),
        ) ??
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
    start: VideoRegionPoint,
    end: VideoRegionPoint,
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

function regionToRect(region: AiPerimeterRegion): VideoRegionRect {
    return {
        x: region.x,
        y: region.y,
        width: region.width,
        height: region.height,
    };
}

function SnapshotRail({
    alerts,
    capabilities,
}: {
    alerts: AiAlertRecord[];
    capabilities: AiCapabilities | null;
}) {
    const latestAlerts = alerts
        .filter((alert) => isAiTaskAvailable(alert.task, capabilities))
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
    const [alarmRule, setAlarmRule] = useState<AlarmRuleConfig | null>(null);
    const [dirty, setDirty] = useState(false);
    const [alarmDirty, setAlarmDirty] = useState(false);
    const [saving, setSaving] = useState(false);
    const [saveMessage, setSaveMessage] = useState('');
    const [editingPerimeter, setEditingPerimeter] = useState(false);
    const [activeRegionIndex, setActiveRegionIndex] = useState(0);
    const perimeterTask = draftTaskByName(draft, 'perimeter_detection');
    const previewStream =
        editingPerimeter && perimeterTask ? perimeterTask.stream : stream;
    const { statuses, previewUrls } = useLiveView(previewStream);
    const {
        status,
        alarmConfig,
        alarmStatus,
        lastAlarmEvent,
        alerts,
        loading,
        error,
        refresh,
    } = useAiAlerts();
    const aiCapabilities = status?.capabilities ?? null;
    const activeStatus = statuses.find((item) => item.stream === previewStream);
    const summary = status?.summary ?? emptyStats();
    const supportedTaskSummary = useMemo(() => {
        if (!status) {
            return { backendLabel: '读取中', enabledCount: 0 };
        }
        const enabledTasks = status.tasks.filter(
            (task) =>
                task.config.enabled &&
                isAiTaskAvailable(task.config.task, status.capabilities),
        );
        return {
            backendLabel:
                !status.summary.enabled || enabledTasks.length === 0
                    ? '未运行'
                    : enabledTasks.every((task) => task.stats.backend_available)
                      ? '可用'
                      : '异常',
            enabledCount: enabledTasks.length,
        };
    }, [status]);
    const frame = useMemo(
        () => parseResolution(activeStatus?.resolution),
        [activeStatus?.resolution],
    );
    const sensitivity = sensitivityForConfig(draft, aiCapabilities);
    const sharedConfig = draft
        ? sharedHiddenTaskConfig(draft, aiCapabilities)
        : null;
    const intervalOptions = numericOptionsWithCurrent(
        kInferenceIntervalOptions,
        sharedConfig?.inference_interval_ms ?? 500,
        (value) => `${value} ms`,
    );
    const maxResultsOptions = numericOptionsWithCurrent(
        kMaxResultsOptions,
        sharedConfig?.max_results ?? 16,
        (value) => `${value} 个`,
    );
    const alarmDurationOptions = numericOptionsWithCurrent(
        kAlarmDurationOptions,
        alarmRule?.min_duration_ms ?? 0,
        (value) => `${value} ms`,
    );
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
        setDraft(completeAiConfig(status.config, status.capabilities));
        setSaveMessage('');
    }, [dirty, status]);

    useEffect(() => {
        if (!alarmConfig || alarmDirty) {
            return;
        }
        setAlarmRule({
            ...alarmConfig.ai_detection,
            regions: [...alarmConfig.ai_detection.regions],
        });
    }, [alarmConfig, alarmDirty]);

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

    const updateDraftWith = useCallback(
        (updater: (config: AiConfig) => AiConfig) => {
            setDraft((current) => (current ? updater(current) : current));
            setDirty(true);
            setSaveMessage('');
        },
        [],
    );

    const updateTaskEnabled = (taskName: AiTaskName, enabled: boolean) => {
        if (!isAiTaskAvailable(taskName, aiCapabilities)) {
            setSaveMessage(
                `${taskLabel(taskName)}${taskUnavailableText(
                    taskName,
                    aiCapabilities,
                )}`,
            );
            return;
        }
        updateDraftWith((current) =>
            updateTaskConfig(current, taskName, { enabled }),
        );
    };

    const updateAllTasks = (patch: Partial<AiModelConfig>) => {
        updateDraftWith((current) => ({
            ...current,
            tasks: current.tasks.map((task) => ({
                ...task,
                ...patch,
            })),
        }));
    };

    const updateAlarmRuleWith = (patch: Partial<AlarmRuleConfig>) => {
        setAlarmRule((current) => (current ? { ...current, ...patch } : current));
        setAlarmDirty(true);
        setSaveMessage('');
    };

    const updateSensitivity = (nextSensitivity: SensitivityLevel) => {
        const threshold = thresholdForSensitivity(nextSensitivity);
        updateAllTasks({ confidence_threshold: threshold });
    };

    const selectRegion = (index: number) => {
        setActiveRegionIndex(index);
    };

    const updatePerimeterRegions = useCallback(
        (updater: (regions: AiPerimeterRegion[]) => AiPerimeterRegion[]) => {
            if (!isAiTaskAvailable('perimeter_detection', aiCapabilities)) {
                return;
            }
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
        [aiCapabilities, updateDraftWith],
    );

    const beginDraw = (point: VideoRegionPoint): VideoRegionDrag | null => {
        if (!editingPerimeter || !draft) {
            return null;
        }
        const regionIndex =
            activeRegionIndex >= perimeterRegions.length
                ? perimeterRegions.length
                : activeRegionIndex;
        const regionName =
            perimeterRegions[regionIndex]?.name || `region-${regionIndex + 1}`;
        setActiveRegionIndex(regionIndex);
        setSaveMessage('');
        updatePerimeterRegions((currentRegions) =>
            replaceRegionAt(
                currentRegions,
                regionIndex,
                regionFromPoints(regionName, point, point),
            ),
        );
        return { regionIndex, start: point };
    };

    const updateDraw = (drag: VideoRegionDrag, point: VideoRegionPoint) => {
        const regionName =
            perimeterRegions[drag.regionIndex]?.name ||
            `region-${drag.regionIndex + 1}`;
        updatePerimeterRegions((currentRegions) =>
            replaceRegionAt(
                currentRegions,
                drag.regionIndex,
                regionFromPoints(regionName, drag.start, point),
            ),
        );
    };

    const addRegion = () => {
        if (!isAiTaskAvailable('perimeter_detection', aiCapabilities)) {
            return;
        }
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
        if (!isAiTaskAvailable('perimeter_detection', aiCapabilities)) {
            return;
        }
        if (!editingPerimeter && perimeterTask) {
            setStream(perimeterTask.stream);
        }
        setEditingPerimeter((current) => !current);
    };

    const restoreDraft = () => {
        if (!status) {
            return;
        }
        setDraft(completeAiConfig(status.config, status.capabilities));
        setDirty(false);
        if (alarmConfig) {
            setAlarmRule({
                ...alarmConfig.ai_detection,
                regions: [...alarmConfig.ai_detection.regions],
            });
            setAlarmDirty(false);
        }
        setSaveMessage('');
    };

    const saveDraft = () => {
        if (!draft) {
            return;
        }
        setSaving(true);
        setSaveMessage('');
        const nextConfig = normalizeAiRootConfigForSave(
            withSharedHiddenDefaults(draft, aiCapabilities),
            aiCapabilities,
        );
        const requests: Promise<void>[] = [saveAiConfig(nextConfig)];
        const nextAlarmRule = alarmRule
            ? {
                  ...alarmRule,
                  min_duration_ms: nonNegativeInteger(
                      alarmRule.min_duration_ms,
                      0,
                  ),
              }
            : null;
        if (alarmConfig && nextAlarmRule) {
            requests.push(saveAiAlarmRule(alarmConfig, nextAlarmRule));
        }
        void Promise.all(requests)
            .then(refresh)
            .then(() => {
                setDirty(false);
                setAlarmDirty(false);
                setDraft(completeAiConfig(nextConfig, aiCapabilities));
                if (nextAlarmRule) {
                    setAlarmRule(nextAlarmRule);
                }
                setSaveMessage('已保存并应用');
            })
            .catch((err: unknown) => {
                setSaveMessage(err instanceof Error ? err.message : '保存失败');
            })
            .finally(() => setSaving(false));
    };

    const perimeterOverlay = (
        <VideoRegionDrawLayer
            className="ai-perimeter-draw-layer"
            drawing={editingPerimeter}
            drawingClassName="editing"
            frame={frame}
            items={perimeterRegions.map((region, index) => {
                const regionClassName = [
                    'ai-perimeter-region',
                    activeRegionIndex === index ? 'active' : '',
                    editingPerimeter ? 'selectable' : '',
                ]
                    .filter(Boolean)
                    .join(' ');
                return {
                    className: regionClassName,
                    key: `${region.name}-${index}`,
                    rect: regionToRect(region),
                    content: (
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
                    ),
                };
            })}
            showVideoArea={editingPerimeter}
            videoAreaClassName="ai-perimeter-video-area"
            onDrawStart={beginDraw}
            onDrawMove={updateDraw}
        />
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
                                    {supportedTaskSummary.enabledCount}{' '}
                                    启用
                                </strong>
                            </div>
                            <div>
                                <span>后端</span>
                                <strong>{supportedTaskSummary.backendLabel}</strong>
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
                                <span>开关事件，调整检测参数</span>
                            </div>
                        </div>

                        <div className="ai-event-option-grid">
                            <label>
                                <span>检测码流</span>
                                <select
                                    disabled={!draft}
                                    value={sharedConfig?.stream ?? 'sub'}
                                    onChange={(event) =>
                                        updateAllTasks({
                                            stream: event.target.value as StreamName,
                                        })
                                    }
                                >
                                    {kStreamOptions.map((option) => (
                                        <option
                                            key={option.value}
                                            value={option.value}
                                        >
                                            {option.label}
                                        </option>
                                    ))}
                                </select>
                            </label>
                            <label>
                                <span>灵敏度</span>
                                <select
                                    disabled={!draft}
                                    value={sensitivity}
                                    onChange={(event) =>
                                        updateSensitivity(
                                            event.target.value as SensitivityLevel,
                                        )
                                    }
                                >
                                    {kSensitivityOptions.map((option) => (
                                        <option
                                            key={option.value}
                                            value={option.value}
                                        >
                                            {option.label}
                                        </option>
                                    ))}
                                </select>
                            </label>
                            <label>
                                <span>推理频率</span>
                                <select
                                    disabled={!draft}
                                    value={optionValue(
                                        sharedConfig?.inference_interval_ms ?? 500,
                                    )}
                                    onChange={(event) =>
                                        updateAllTasks({
                                            inference_interval_ms: Number(
                                                event.target.value,
                                            ),
                                        })
                                    }
                                >
                                    {intervalOptions.map((option) => (
                                        <option
                                            key={option.value}
                                            value={optionValue(option.value)}
                                        >
                                            {option.label}
                                        </option>
                                    ))}
                                </select>
                            </label>
                            <label>
                                <span>结果上限</span>
                                <select
                                    disabled={!draft}
                                    value={optionValue(
                                        sharedConfig?.max_results ?? 16,
                                    )}
                                    onChange={(event) =>
                                        updateAllTasks({
                                            max_results: Number(event.target.value),
                                        })
                                    }
                                >
                                    {maxResultsOptions.map((option) => (
                                        <option
                                            key={option.value}
                                            value={optionValue(option.value)}
                                        >
                                            {option.label}
                                        </option>
                                    ))}
                                </select>
                            </label>
                            <label>
                                <span>报警持续</span>
                                <select
                                    disabled={!alarmRule}
                                    value={optionValue(
                                        alarmRule?.min_duration_ms ?? 0,
                                    )}
                                    onChange={(event) =>
                                        updateAlarmRuleWith({
                                            min_duration_ms: Number(
                                                event.target.value,
                                            ),
                                        })
                                    }
                                >
                                    {alarmDurationOptions.map((option) => (
                                        <option
                                            key={option.value}
                                            value={optionValue(option.value)}
                                        >
                                            {option.label}
                                        </option>
                                    ))}
                                </select>
                            </label>
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
                                    const available =
                                        isAiTaskAvailable(
                                            taskName,
                                            aiCapabilities,
                                        );
                                    return (
                                        <article
                                            className="ai-event-switch-row"
                                            key={taskName}
                                        >
                                            <label className="ai-event-switch-main">
                                                <input
                                                    checked={enabled}
                                                    disabled={!available}
                                                    type="checkbox"
                                                    onChange={(event) =>
                                                        updateTaskEnabled(
                                                            taskName,
                                                            event.target
                                                                .checked,
                                                        )
                                                    }
                                                />
                                                <span
                                                    className="ai-event-toggle"
                                                    aria-hidden="true"
                                                />
                                                <span>
                                                    <strong>
                                                        {taskLabel(taskName)}
                                                    </strong>
                                                    <em>
                                                        {available
                                                            ? `${alertCounts[taskName]} 张抓图`
                                                            : taskUnavailableText(
                                                                  taskName,
                                                                  aiCapabilities,
                                                              )}
                                                    </em>
                                                </span>
                                            </label>
                                            <StatusBadge
                                                state={taskBadgeState(
                                                    taskStatus,
                                                    aiCapabilities,
                                                )}
                                                label={taskStatusText(
                                                    taskStatus,
                                                    aiCapabilities,
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
                                                    disabled={!available}
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
                            {dirty || alarmDirty ? (
                                <span>有未保存修改</span>
                            ) : null}
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

                <SnapshotRail
                    alerts={alerts}
                    capabilities={aiCapabilities}
                />
            </div>
        </div>
    );
}

import {
    useCallback,
    useEffect,
    useMemo,
    useState,
} from 'react';
import type {
    AiPerimeterRegion,
    StreamName,
} from '../api/types';
import type {
    VideoRegionDrag,
    VideoRegionPoint,
} from '../components/VideoRegionDrawLayer';
import { useAiAlerts } from '../hooks/useAiAlerts';
import { useLiveView } from '../hooks/useLiveView';
import {
    isAiTaskAvailable,
} from '../features/ai-alerts/aiAlertTasks';
import { kTaskOrder } from '../features/ai-alerts/aiAlertOptions';
import {
    alertSizesByTask,
    draftTaskByName,
    emptyStats,
    taskByName,
    updateTaskConfig,
} from '../features/ai-alerts/aiConfigDraft';
import {
    normalizeRegion,
    parseResolution,
    regionFromPoints,
    replaceRegionAt,
} from '../features/ai-alerts/aiPerimeterRegions';
import { AiEventWorkbench } from '../features/ai-alerts/AiEventWorkbench';
import { AiPerimeterOverlay } from '../features/ai-alerts/AiPerimeterOverlay';
import { AiPreviewPanel } from '../features/ai-alerts/AiPreviewPanel';
import { AiSnapshotRail } from '../features/ai-alerts/AiSnapshotRail';
import { useAiAlertConfigForm } from '../features/ai-alerts/useAiAlertConfigForm';
import '../styles/ai-alerts.css';

export function AiAlertsPage() {
    const [stream, setStream] = useState<StreamName>('sub');
    const [editingPerimeter, setEditingPerimeter] = useState(false);
    const [activeRegionIndex, setActiveRegionIndex] = useState(0);
    const {
        aiStatus,
        alarmConfig,
        alarmInfo,
        lastAlarmEvent,
        alerts,
        loading,
        error,
        refresh,
    } = useAiAlerts();
    const aiCapabilities = aiStatus?.capabilities ?? null;
    const {
        alarmDirty,
        alarmDurationOptions,
        alarmRule,
        clearSaveMessage,
        dirty,
        draft,
        intervalOptions,
        maxResultsOptions,
        restoreDraft,
        saveDraft,
        saveMsg,
        saving,
        sensitivity,
        sharedConfig,
        updateAlarmRuleWith,
        updateAllTasks,
        updateDraftWith,
        updateSensitivity,
        updateTaskEnabled,
    } = useAiAlertConfigForm({
        aiStatus,
        alarmConfig,
        refresh,
    });
    const perimeterTask = draftTaskByName(draft, 'perimeter_detection');
    const previewStream =
        editingPerimeter && perimeterTask ? perimeterTask.stream : stream;
    const { statuses, previewUrls } = useLiveView(previewStream);
    const activeStatus = statuses.find((item) => item.stream === previewStream);
    const summary = aiStatus?.summary ?? emptyStats();
    const supportedTaskSummary = useMemo(() => {
        if (!aiStatus) {
            return { backendLabel: '读取中', enabledTaskTotal: 0 };
        }
        const enabledTasks = aiStatus.tasks.filter(
            (task) =>
                task.config.enabled &&
                isAiTaskAvailable(task.config.task, aiStatus.capabilities),
        );
        return {
            backendLabel:
                !aiStatus.summary.enabled || enabledTasks.length === 0
                    ? '未运行'
                    : enabledTasks.every((task) => task.stats.backend_available)
                      ? '可用'
                      : '异常',
            enabledTaskTotal: enabledTasks.length,
        };
    }, [aiStatus]);

    const frame = useMemo(
        () => parseResolution(activeStatus?.resolution),
        [activeStatus?.resolution],
    );
    const alertSizes = useMemo(() => alertSizesByTask(alerts), [alerts]);
    const perimeterRegions = perimeterTask?.perimeter_regions ?? [];
    const activeRegion =
        activeRegionIndex >= 0 && activeRegionIndex < perimeterRegions.length
            ? perimeterRegions[activeRegionIndex]
            : null;
    const orderedTaskStatuses = useMemo(
        () => kTaskOrder.map((task) => taskByName(aiStatus?.tasks ?? [], task)),
        [aiStatus],
    );

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
        clearSaveMessage();
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
        clearSaveMessage();
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

    const perimeterOverlay = (
        <AiPerimeterOverlay
            activeRegionIndex={activeRegionIndex}
            editing={editingPerimeter}
            frame={frame}
            regions={perimeterRegions}
            onDrawStart={beginDraw}
            onDrawMove={updateDraw}
            onSelectRegion={selectRegion}
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
                <div className="aiStatus-note error-note">{error}</div>
            ) : null}

            <div className="ai-console-layout">
                <main className="ai-console-main">
                    <AiPreviewPanel
                        activeResolution={activeStatus?.resolution}
                        aiStatus={aiStatus}
                        alarmInfo={alarmInfo}
                        error={error}
                        lastAlarmEvent={lastAlarmEvent}
                        perimeterOverlay={perimeterOverlay}
                        previewStream={previewStream}
                        previewUrls={previewUrls}
                        statuses={statuses}
                        summary={summary}
                        supportedTaskSummary={supportedTaskSummary}
                        onSnapshot={captureSnapshot}
                        onStreamChange={handleStreamChange}
                    />

                    <AiEventWorkbench
                        activeRegion={activeRegion}
                        activeRegionIndex={activeRegionIndex}
                        aiCapabilities={aiCapabilities}
                        alarmDirty={alarmDirty}
                        alarmDurationOptions={alarmDurationOptions}
                        alarmRule={alarmRule}
                        alertSizes={alertSizes}
                        clearRegions={clearRegions}
                        deleteRegion={deleteRegion}
                        dirty={dirty}
                        draft={draft}
                        editingPerimeter={editingPerimeter}
                        intervalOptions={intervalOptions}
                        maxResultsOptions={maxResultsOptions}
                        perimeterRegions={perimeterRegions}
                        restoreDraft={restoreDraft}
                        saveDraft={saveDraft}
                        saveMsg={saveMsg}
                        saving={saving}
                        selectRegion={selectRegion}
                        sensitivity={sensitivity}
                        sharedConfig={sharedConfig}
                        togglePerimeterEdit={togglePerimeterEdit}
                        updateAlarmRuleWith={updateAlarmRuleWith}
                        updateAllTasks={updateAllTasks}
                        updateSensitivity={updateSensitivity}
                        updateTaskEnabled={updateTaskEnabled}
                        addRegion={addRegion}
                        orderedTaskStatuses={orderedTaskStatuses}
                    />
                </main>

                <AiSnapshotRail alerts={alerts} capabilities={aiCapabilities} />
            </div>
        </div>
    );
}

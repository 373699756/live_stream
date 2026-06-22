import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import type {
    AiDetection,
    AiInferenceResult,
    AiTaskName,
    AiTaskStatus,
    AiStatus,
    StreamName,
} from '../api/types';
import { isAiTaskAvailable } from '../features/ai-alerts/aiAlertTasks';

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

interface DisplayDetection extends AiDetection {
    task: AiTaskName;
}

interface DisplayResult extends Omit<AiInferenceResult, 'detections'> {
    detections: DisplayDetection[];
}

const kDetectionHoldMs = 2500;

const streamLabel = (stream: StreamName) =>
    stream === 'main' ? '主码流' : '子码流';

function percent(value: number) {
    return `${Math.round(value * 100)}%`;
}

function parseResolution(resolution: string | undefined) {
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

function detectionStyle(detection: DisplayDetection) {
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

function taskClassName(task: AiTaskName) {
    switch (task) {
        case 'object_detection':
            return 'object';
        case 'perimeter_detection':
            return 'perimeter';
        case 'motion_classification':
            return 'motion';
        case 'occlusion_detection':
            return 'occlusion';
    }
}

function taskShortLabel(task: AiTaskName) {
    switch (task) {
        case 'object_detection':
            return '目标';
        case 'perimeter_detection':
            return '周界';
        case 'motion_classification':
            return '移动';
        case 'occlusion_detection':
            return '遮挡';
    }
}

function taskHasUsableResult(
    task: AiTaskStatus,
    stream: StreamName,
    status: AiStatus | null,
) {
    return (
        task.config.enabled &&
        isAiTaskAvailable(task.config.task, status?.capabilities) &&
        task.stats.enabled &&
        task.stats.backend_available &&
        task.last_result.success &&
        task.last_result.stream === stream &&
        task.last_result.detections.length > 0
    );
}

function resultForStream(
    status: AiStatus | null,
    stream: StreamName,
): DisplayResult | null {
    if (!status?.enabled) {
        return null;
    }
    const detections: DisplayDetection[] = [];
    let latestSequence = 0;
    let latestPtsUs = 0;
    for (const task of status.tasks ?? []) {
        if (!taskHasUsableResult(task, stream, status)) {
            continue;
        }
        detections.push(
            ...task.last_result.detections.map((detection) => ({
                ...detection,
                task: task.config.task,
            })),
        );
        latestSequence = Math.max(latestSequence, task.last_result.sequence);
        latestPtsUs = Math.max(latestPtsUs, task.last_result.pts_us);
    }
    if (detections.length === 0) {
        return null;
    }
    return {
        success: true,
        stream,
        sequence: latestSequence,
        pts_us: latestPtsUs,
        detections,
    };
}

export function AiDetectionOverlay({
    frameResolution,
    status,
    stream,
    error = '',
}: AiDetectionOverlayProps) {
    const overlayRef = useRef<HTMLDivElement | null>(null);
    const [surfaceSize, setSurfaceSize] = useState<SurfaceSize>({
        width: 0,
        height: 0,
    });
    const [heldResult, setHeldResult] = useState<{
        expiresAtMs: number;
        result: DisplayResult;
    } | null>(null);
    const result = resultForStream(status, stream);
    const enabledTasks =
        status?.tasks?.filter(
            (task) =>
                task.config.enabled &&
                isAiTaskAvailable(task.config.task, status.capabilities),
        ) ?? [];
    const hasRunnableTask = enabledTasks.some(
        (task) => task.stats.backend_available,
    );
    const aiReady = !error && Boolean(status?.enabled) && hasRunnableTask;
    const resultHasDetections =
        aiReady &&
        Boolean(result?.success) &&
        Boolean(result?.detections.length);
    const displayResult =
        resultHasDetections && result
            ? result
            : aiReady
              ? heldResult?.result
              : null;
    const detections =
        aiReady && displayResult?.success ? displayResult.detections : [];
    const resultCount =
        aiReady && result?.success ? result.detections.length : 0;
    const frame = parseResolution(frameResolution);
    const contentStyle = contentAreaStyle(frame, surfaceSize);

    useEffect(() => {
        if (!aiReady) {
            setHeldResult(null);
            return;
        }
        if (!result?.success || result.detections.length === 0) {
            return;
        }
        setHeldResult({
            expiresAtMs: Date.now() + kDetectionHoldMs,
            result,
        });
    }, [aiReady, result]);

    useEffect(() => {
        if (!heldResult) {
            return undefined;
        }
        const delayMs = Math.max(0, heldResult.expiresAtMs - Date.now());
        const timer = window.setTimeout(() => {
            setHeldResult((current) =>
                current?.expiresAtMs === heldResult.expiresAtMs
                    ? null
                    : current,
            );
        }, delayMs);
        return () => window.clearTimeout(timer);
    }, [heldResult]);

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
    } else if (status && !status.enabled) {
        statusText = '未启用';
    } else if (status?.enabled && !hasRunnableTask) {
        statusText = '后端不可用';
    } else if (detections.length > 0) {
        statusText = `${detections.length} 个目标`;
    } else if (aiReady && result?.success) {
        statusText = `${resultCount} 个目标`;
    } else if (status?.enabled) {
        const otherStream = stream === 'main' ? 'sub' : 'main';
        const hasOtherStreamResult = (status.tasks ?? []).some((task) =>
            taskHasUsableResult(task, otherStream, status),
        );
        statusText = hasOtherStreamResult
            ? `${streamLabel(otherStream)}有结果`
            : '无有效结果';
    }

    return (
        <div className="ai-preview-overlay" ref={overlayRef} aria-hidden="true">
            <div className="ai-detection-layer" style={contentStyle}>
                {detections.map((detection, index) => (
                    <div
                        className={[
                            'ai-detection-box',
                            `ai-detection-box-${taskClassName(detection.task)}`,
                            detection.y <= 0.08
                                ? 'near-top'
                                : '',
                        ]
                            .filter(Boolean)
                            .join(' ')}
                        key={`${displayResult?.sequence || 0}-${index}`}
                        style={detectionStyle(detection)}
                    >
                        <span>
                            {taskShortLabel(detection.task)} /{' '}
                            {detection.label || 'target'}{' '}
                            {percent(detection.confidence)}
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

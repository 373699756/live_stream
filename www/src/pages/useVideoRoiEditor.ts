import { useEffect, useState } from 'react';
import type {
    MediaCapabilities,
    StreamName,
    VideoConfig,
    VideoStreamConfig,
} from '../api/types';
import type {
    VideoRegionDrag,
    VideoRegionPoint,
} from '../components/VideoRegionDrawLayer';
import {
    clampNumber,
    clampRoiRegion,
    parseResolutionSize,
    roiRegionFromPoints,
    roiRegionToRect,
} from './videoRoiGeometry';

interface RoiDrawTarget {
    stream: StreamName;
    regionIndex: number;
}

interface UseVideoRoiEditorOptions {
    activeStreamName: StreamName;
    config: VideoConfig | null;
    capabilities: MediaCapabilities | null;
    updateStream: (name: StreamName, stream: VideoStreamConfig) => void;
}

export function useVideoRoiEditor({
    activeStreamName,
    config,
    capabilities,
    updateStream,
}: UseVideoRoiEditorOptions) {
    const [activeRegionByStream, setActiveRegionByStream] = useState<
        Record<StreamName, number>
    >({ main: 0, sub: 0 });
    const [drawTarget, setDrawTarget] = useState<RoiDrawTarget | null>(null);

    useEffect(() => {
        if (!config) {
            return;
        }
        setActiveRegionByStream((current) => {
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

    const activeStream = config?.streams[activeStreamName] ?? null;
    const activeRegions = activeStream?.roi?.regions ?? [];
    const activeCapabilities = capabilities?.streams[activeStreamName] ?? null;
    const activeRegionIndex =
        drawTarget?.stream === activeStreamName &&
        drawTarget.regionIndex === activeRegions.length
            ? drawTarget.regionIndex
            : activeRegions.length > 0
              ? clampNumber(
                    activeRegionByStream[activeStreamName] ?? 0,
                    0,
                    activeRegions.length - 1,
                )
              : 0;
    const supported =
        activeStream !== null &&
        activeCapabilities !== null &&
        activeCapabilities.available !== false &&
        Boolean(activeCapabilities.roi_supported) &&
        (activeStream.codec === 'h264' || activeStream.codec === 'h265');
    const frame = parseResolutionSize(activeStream?.resolution ?? '');
    const drawing =
        drawTarget?.stream === activeStreamName &&
        drawTarget.regionIndex === activeRegionIndex &&
        supported;

    const selectRegion = (index: number) => {
        setActiveRegionByStream((current) => ({
            ...current,
            [activeStreamName]: Math.max(0, Math.round(index)),
        }));
    };

    const updateRegionFromPreview = (
        index: number,
        region: ReturnType<typeof roiRegionFromPoints>,
    ) => {
        if (!supported || !activeStream) {
            return;
        }
        const maxRegions = activeCapabilities?.max_roi_regions || 0;
        if (
            index > activeRegions.length ||
            (index === activeRegions.length && activeRegions.length >= maxRegions)
        ) {
            return;
        }
        const nextRegions = [...activeRegions];
        nextRegions[index] = clampRoiRegion(region, activeStream);
        updateStream(activeStreamName, {
            ...activeStream,
            roi: {
                enabled: activeStream.roi?.enabled ?? false,
                regions: nextRegions,
            },
        });
        selectRegion(index);
    };

    const startDraw = (index: number) => {
        if (!supported) {
            return;
        }
        const maxRegions = activeCapabilities?.max_roi_regions || 0;
        if (
            index > activeRegions.length ||
            (index === activeRegions.length && activeRegions.length >= maxRegions)
        ) {
            return;
        }
        selectRegion(index);
        setDrawTarget({ stream: activeStreamName, regionIndex: index });
    };

    const cancelDraw = () => {
        setDrawTarget(null);
    };

    const reset = () => {
        setActiveRegionByStream({ main: 0, sub: 0 });
        setDrawTarget(null);
    };

    const handleDrawStart = (point: VideoRegionPoint): VideoRegionDrag => {
        const regionIndex =
            activeRegionIndex >= 0 && activeRegionIndex < activeRegions.length
                ? activeRegionIndex
                : activeRegions.length;
        updateRegionFromPreview(
            regionIndex,
            roiRegionFromPoints(
                frame,
                activeRegions[regionIndex],
                point,
                point,
            ),
        );
        return { regionIndex, start: point };
    };

    const handleDrawMove = (drag: VideoRegionDrag, point: VideoRegionPoint) => {
        updateRegionFromPreview(
            drag.regionIndex,
            roiRegionFromPoints(
                frame,
                activeRegions[drag.regionIndex],
                drag.start,
                point,
            ),
        );
    };

    const handleDrawEnd = (drag: VideoRegionDrag) => {
        selectRegion(drag.regionIndex);
        setDrawTarget(null);
    };

    const regionItems = activeRegions.map((region, index) => {
        const className = [
            'roi-region-rect',
            index === activeRegionIndex ? 'active' : '',
            activeStream?.roi?.enabled && region.enabled ? '' : 'disabled',
        ]
            .filter(Boolean)
            .join(' ');
        return {
            className,
            key: `${index}-${region.x}-${region.y}-${region.width}-${region.height}`,
            rect: roiRegionToRect(region, frame),
            content: index + 1,
        };
    });

    return {
        activeRegionIndex,
        cancelDraw,
        drawing,
        frame,
        handleDrawEnd,
        handleDrawMove,
        handleDrawStart,
        regionItems,
        reset,
        selectRegion,
        startDraw,
        supported,
    };
}

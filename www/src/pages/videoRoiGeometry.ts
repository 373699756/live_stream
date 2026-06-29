import type {
    VideoRoiRegion,
    VideoStreamConfig,
} from '../api/types';
import {
    clampUnit,
    type VideoRegionPoint,
    type VideoRegionRect,
} from '../components/VideoRegionDrawLayer';

export function parseResolutionSize(resolution: string) {
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

export function clampNumber(value: number, min: number, max: number) {
    if (!Number.isFinite(value)) return min;
    return Math.min(Math.max(Math.round(value), min), max);
}

export function clampRoiRegion(
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

export function roiRegionFromPoints(
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

export function roiRegionToRect(
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

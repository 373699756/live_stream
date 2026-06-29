import type { AiPerimeterRegion } from '../../api/types';
import type {
    VideoRegionPoint,
    VideoRegionRect,
} from '../../components/VideoRegionDrawLayer';
import { clampUnit } from './aiConfigDraft';

export interface FrameSize {
    width: number;
    height: number;
}

const kMinimumRegionSize = 0.01;

export function clampRegionStart(value: number) {
    return Math.min(1 - kMinimumRegionSize, clampUnit(value));
}

export function parseResolution(resolution: string | undefined): FrameSize {
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

export function normalizeRegion(
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

export function regionFromPoints(
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

export function replaceRegionAt(
    regions: AiPerimeterRegion[],
    index: number,
    nextRegion: AiPerimeterRegion,
) {
    const nextRegions = [...regions];
    nextRegions[index] = nextRegion;
    return nextRegions.map(normalizeRegion);
}

export function regionToRect(region: AiPerimeterRegion): VideoRegionRect {
    return {
        x: region.x,
        y: region.y,
        width: region.width,
        height: region.height,
    };
}

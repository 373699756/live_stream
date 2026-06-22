import {
    useCallback,
    useLayoutEffect,
    useRef,
    useState,
    type CSSProperties,
    type PointerEvent,
    type ReactNode,
} from 'react';

interface VideoFrameSize {
    width: number;
    height: number;
}

interface VideoSurfaceRect {
    left: number;
    top: number;
    width: number;
    height: number;
}

export interface VideoRegionPoint {
    x: number;
    y: number;
}

export interface VideoRegionRect {
    x: number;
    y: number;
    width: number;
    height: number;
}

export interface VideoRegionDrag {
    regionIndex: number;
    start: VideoRegionPoint;
}

interface VideoRegionItem {
    className: string;
    key: string;
    rect: VideoRegionRect;
    content?: ReactNode;
    style?: CSSProperties;
}

interface VideoRegionDrawLayerProps {
    className: string;
    drawingClassName?: string;
    frame: VideoFrameSize;
    drawing?: boolean;
    disabled?: boolean;
    items: VideoRegionItem[];
    videoAreaClassName?: string;
    showVideoArea?: boolean;
    onDrawStart?: (point: VideoRegionPoint) => VideoRegionDrag | null;
    onDrawMove?: (
        drag: VideoRegionDrag,
        point: VideoRegionPoint,
    ) => void;
    onDrawEnd?: (drag: VideoRegionDrag) => void;
}

export const clampUnit = (value: number) => {
    if (!Number.isFinite(value)) {
        return 0;
    }
    return Math.min(1, Math.max(0, value));
};

function contentAreaForVideoSurface(
    frame: VideoFrameSize,
    surface: { width: number; height: number },
): VideoSurfaceRect | null {
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

function regionRectToSurfaceStyle(
    rect: VideoRegionRect,
    videoArea: VideoSurfaceRect,
): CSSProperties {
    return {
        left: videoArea.left + rect.x * videoArea.width,
        top: videoArea.top + rect.y * videoArea.height,
        width: rect.width * videoArea.width,
        height: rect.height * videoArea.height,
    };
}

function videoAreaToSurfaceStyle(
    videoArea: VideoSurfaceRect,
): CSSProperties {
    return {
        left: videoArea.left,
        top: videoArea.top,
        width: videoArea.width,
        height: videoArea.height,
    };
}

export function VideoRegionDrawLayer({
    className,
    drawingClassName = 'drawing',
    frame,
    drawing = false,
    disabled = false,
    items,
    videoAreaClassName,
    showVideoArea = false,
    onDrawStart,
    onDrawMove,
    onDrawEnd,
}: VideoRegionDrawLayerProps) {
    const [drag, setDrag] = useState<VideoRegionDrag | null>(null);
    const [surfaceSize, setSurfaceSize] = useState({ width: 0, height: 0 });
    const drawRef = useRef<HTMLDivElement | null>(null);
    const [drawElement, setDrawElement] = useState<HTMLDivElement | null>(null);
    const setDrawLayerRef = useCallback((node: HTMLDivElement | null) => {
        drawRef.current = node;
        setDrawElement(node);
    }, []);

    useLayoutEffect(() => {
        if (!drawElement) {
            return undefined;
        }
        const updateSurfaceSize = () => {
            const rect = drawElement.getBoundingClientRect();
            setSurfaceSize({ width: rect.width, height: rect.height });
        };
        updateSurfaceSize();
        if (typeof ResizeObserver === 'undefined') {
            window.addEventListener('resize', updateSurfaceSize);
            return () =>
                window.removeEventListener('resize', updateSurfaceSize);
        }
        const observer = new ResizeObserver(updateSurfaceSize);
        observer.observe(drawElement);
        return () => observer.disconnect();
    }, [drawElement]);

    const videoArea =
        surfaceSize.width > 0 && surfaceSize.height > 0
            ? contentAreaForVideoSurface(frame, surfaceSize)
            : null;

    const pointerToRegionPoint = (event: PointerEvent<HTMLDivElement>) => {
        const surface = drawRef.current?.getBoundingClientRect();
        if (!surface) {
            return null;
        }
        const contentArea = contentAreaForVideoSurface(frame, {
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

    const beginDraw = (event: PointerEvent<HTMLDivElement>) => {
        if (disabled || !drawing || event.button !== 0 || !onDrawStart) {
            return;
        }
        const point = pointerToRegionPoint(event);
        if (!point) {
            return;
        }
        const nextDrag = onDrawStart(point);
        if (!nextDrag) {
            return;
        }
        setDrag(nextDrag);
        event.currentTarget.setPointerCapture(event.pointerId);
        event.preventDefault();
    };

    const updateDraw = (event: PointerEvent<HTMLDivElement>) => {
        if (!drag || !onDrawMove) {
            return;
        }
        const point = pointerToRegionPoint(event);
        if (!point) {
            return;
        }
        onDrawMove(drag, point);
        event.preventDefault();
    };

    const finishDraw = (event: PointerEvent<HTMLDivElement>) => {
        if (!drag) {
            return;
        }
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
            event.currentTarget.releasePointerCapture(event.pointerId);
        }
        onDrawEnd?.(drag);
        setDrag(null);
    };

    const layerClassName = [
        className,
        drawing && !disabled ? drawingClassName : '',
    ]
        .filter(Boolean)
        .join(' ');

    return (
        <div
            ref={setDrawLayerRef}
            className={layerClassName}
            onPointerDown={beginDraw}
            onPointerMove={updateDraw}
            onPointerUp={finishDraw}
            onPointerCancel={finishDraw}
        >
            {videoArea && showVideoArea && videoAreaClassName ? (
                <div
                    className={videoAreaClassName}
                    style={videoAreaToSurfaceStyle(videoArea)}
                />
            ) : null}
            {videoArea
                ? items.map((item) => (
                      <div
                          className={item.className}
                          key={item.key}
                          style={{
                              ...regionRectToSurfaceStyle(
                                  item.rect,
                                  videoArea,
                              ),
                              ...item.style,
                          }}
                      >
                          {item.content}
                      </div>
                  ))
                : null}
        </div>
    );
}

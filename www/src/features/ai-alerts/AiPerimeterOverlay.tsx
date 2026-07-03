import type { AiPerimeterRegion } from '../../api/types';
import {
    VideoRegionDrawLayer,
    type VideoRegionDrag,
    type VideoRegionPoint,
} from '../../components/VideoRegionDrawLayer';
import type { FrameSize } from './aiPerimeterRegions';
import { regionToRect } from './aiPerimeterRegions';

interface AiPerimeterOverlayProps {
    activeRegionIndex: number;
    editing: boolean;
    fit?: 'contain' | 'cover';
    frame: FrameSize;
    regions: AiPerimeterRegion[];
    onDrawMove: (drag: VideoRegionDrag, point: VideoRegionPoint) => void;
    onDrawStart: (point: VideoRegionPoint) => VideoRegionDrag | null;
    onSelectRegion: (index: number) => void;
}

export function AiPerimeterOverlay({
    activeRegionIndex,
    editing,
    fit = 'contain',
    frame,
    regions,
    onDrawMove,
    onDrawStart,
    onSelectRegion,
}: AiPerimeterOverlayProps) {
    return (
        <VideoRegionDrawLayer
            className="ai-perimeter-draw-layer"
            drawing={editing}
            drawingClassName="editing"
            fit={fit}
            frame={frame}
            items={regions.map((region, index) => {
                const regionClassName = [
                    'ai-perimeter-region',
                    activeRegionIndex === index ? 'active' : '',
                    editing ? 'selectable' : '',
                ]
                    .filter(Boolean)
                    .join(' ');
                return {
                    className: regionClassName,
                    key: `${region.name}-${index}`,
                    rect: regionToRect(region),
                    content: (
                        <span
                            role={editing ? 'button' : undefined}
                            tabIndex={editing ? 0 : -1}
                            onKeyDown={(event) => {
                                if (
                                    editing &&
                                    (event.key === 'Enter' ||
                                        event.key === ' ')
                                ) {
                                    event.preventDefault();
                                    onSelectRegion(index);
                                }
                            }}
                            onPointerDown={(event) => {
                                if (!editing) {
                                    return;
                                }
                                event.stopPropagation();
                                onSelectRegion(index);
                            }}
                        >
                            {index + 1}
                        </span>
                    ),
                };
            })}
            showGrid={editing}
            showVideoArea={editing}
            videoAreaClassName="ai-perimeter-video-area"
            onDrawStart={onDrawStart}
            onDrawMove={onDrawMove}
        />
    );
}

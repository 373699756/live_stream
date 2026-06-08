import type { RefObject } from 'react';

export type PreviewLayerMediaKind = 'video' | 'mjpeg';

export interface PreviewMediaLayerRefs {
    imageRef: RefObject<HTMLImageElement | null>;
    mediaKind: PreviewLayerMediaKind;
    videoRef: RefObject<HTMLVideoElement | null>;
}

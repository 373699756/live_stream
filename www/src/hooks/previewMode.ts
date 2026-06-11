import type { MediaPreviewUrls, MediaStreamInfo } from '../api/types';

export type PreviewMode = 'webrtc' | 'hls' | 'flv' | 'mjpeg';

export const previewModeLabels: Record<PreviewMode, string> = {
    webrtc: 'WebRTC',
    hls: 'HLS',
    flv: 'HTTP-FLV',
    mjpeg: 'MJPEG',
};

export interface PreviewReadiness {
    flvModeEnabled: boolean;
    flvPreviewReady: boolean;
    flvReady: boolean;
    flvSupported: boolean;
    hlsLaunchable: boolean;
    hlsModeEnabled: boolean;
    hlsPreviewReady: boolean;
    hlsReady: boolean;
    hlsSupported: boolean;
    mjpegModeEnabled: boolean;
    mjpegPreviewReady: boolean;
    mjpegReady: boolean;
    mjpegSupported: boolean;
    nextAutoMode: PreviewMode | null;
    nextReadyMode: PreviewMode | null;
    selectedModeEnabled: boolean;
    streamRunning: boolean;
    webrtcEnabled: boolean;
    webrtcModeEnabled: boolean;
    webrtcPreviewReady: boolean;
    webrtcReady: boolean;
    webrtcSupported: boolean;
}

export function buildPreviewReadiness(
    active: MediaStreamInfo | undefined,
    mode: PreviewMode,
    previewUrls: MediaPreviewUrls | null,
): PreviewReadiness {
    const hlsReady = active?.hls_ready ?? false;
    const flvReady = active?.http_flv_ready ?? false;
    const mjpegReady = active?.mjpeg_ready ?? false;
    const webrtcReady = active?.webrtc_ready ?? false;
    const hlsSupported = Boolean(active?.hls_supported && previewUrls?.hls);
    const flvSupported = Boolean(
        active?.http_flv_supported && previewUrls?.http_flv,
    );
    const mjpegSupported = Boolean(
        active?.mjpeg_supported && previewUrls?.mjpeg,
    );
    const webrtcSupported = Boolean(active?.webrtc_supported);
    const webrtcEnabled = webrtcSupported;
    const streamRunning = active?.running ?? false;
    const webrtcModeEnabled = webrtcEnabled && streamRunning;
    const hlsLaunchable = hlsSupported && streamRunning;
    const hlsModeEnabled = hlsLaunchable;
    const flvModeEnabled = flvSupported && streamRunning;
    const mjpegModeEnabled = mjpegSupported && streamRunning;
    const webrtcPreviewReady = webrtcModeEnabled && webrtcReady;
    const hlsPreviewReady = hlsLaunchable && hlsReady;
    const flvPreviewReady = flvModeEnabled && flvReady;
    const mjpegPreviewReady = mjpegModeEnabled && mjpegReady;
    const selectedModeEnabled =
        (mode === 'webrtc' && webrtcPreviewReady) ||
        (mode === 'hls' && hlsLaunchable) ||
        (mode === 'flv' && flvPreviewReady) ||
        (mode === 'mjpeg' && mjpegPreviewReady);
    const nextReadyMode = webrtcPreviewReady
        ? 'webrtc'
        : flvPreviewReady
          ? 'flv'
          : mjpegPreviewReady
            ? 'mjpeg'
            : hlsPreviewReady
              ? 'hls'
              : null;
    const nextAutoMode = nextReadyMode;

    return {
        flvModeEnabled,
        flvPreviewReady,
        flvReady,
        flvSupported,
        hlsLaunchable,
        hlsModeEnabled,
        hlsPreviewReady,
        hlsReady,
        hlsSupported,
        mjpegModeEnabled,
        mjpegPreviewReady,
        mjpegReady,
        mjpegSupported,
        nextAutoMode,
        nextReadyMode,
        selectedModeEnabled,
        streamRunning,
        webrtcEnabled,
        webrtcModeEnabled,
        webrtcPreviewReady,
        webrtcReady,
        webrtcSupported,
    };
}

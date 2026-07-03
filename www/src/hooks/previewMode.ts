import type { MediaPreviewUrls, MediaStreamInfo } from '../api/types';

export type PreviewMode = 'webrtc' | 'hls' | 'flv' | 'mjpeg';

export const previewModeLabels: Record<PreviewMode, string> = {
    webrtc: 'WebRTC',
    hls: 'HLS',
    flv: 'HTTP-FLV',
    mjpeg: 'MJPEG',
};

let hevcPlayable: boolean | null = null;
const genericHevcCodecs = [
    'hvc1.1.6.L93.B0',
    'hev1.1.6.L93.B0',
];

function mediaSourceCanPlayMp4Codec(codec: string) {
    return (
        typeof MediaSource !== 'undefined' &&
        Boolean(MediaSource.isTypeSupported?.(
            `video/mp4; codecs="${codec}"`,
        ))
    );
}

function browserCanPlayHevc(codec?: string) {
    if (codec) {
        return mediaSourceCanPlayMp4Codec(codec);
    }
    if (hevcPlayable !== null) {
        return hevcPlayable;
    }
    hevcPlayable = genericHevcCodecs.some(mediaSourceCanPlayMp4Codec);
    return hevcPlayable;
}

function browserCanPreviewHlsCodec(codec?: string, hlsCodec?: string) {
    if (codec === 'h265') {
        return browserCanPlayHevc(hlsCodec);
    }
    return codec === 'h264';
}

function browserCanPreviewFlvCodec(codec?: string) {
    return codec === 'h264';
}

function browserCanPreviewWebrtcCodec(codec?: string) {
    if (codec === 'h264') {
        return true;
    }
    if (codec !== 'h265') {
        return false;
    }
    const receiverCodecs =
        typeof RTCRtpReceiver === 'undefined'
            ? []
            : (RTCRtpReceiver.getCapabilities?.('video')?.codecs ?? []);
    const senderCodecs =
        typeof RTCRtpSender === 'undefined'
            ? []
            : (RTCRtpSender.getCapabilities?.('video')?.codecs ?? []);
    return [...receiverCodecs, ...senderCodecs].some((candidate) => {
        const mimeType = candidate.mimeType.toLowerCase();
        return mimeType === 'video/h265' || mimeType === 'video/hevc';
    });
}

export interface PreviewReadiness {
    flvModeEnabled: boolean;
    flvPreviewReady: boolean;
    flvReady: boolean;
    flvSupported: boolean;
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
    const hlsSupported = Boolean(
        active?.hls_supported &&
            previewUrls?.hls &&
            browserCanPreviewHlsCodec(active?.codec, active?.hls_codec),
    );
    const flvSupported = Boolean(
        active?.http_flv_supported &&
            previewUrls?.http_flv &&
            browserCanPreviewFlvCodec(active?.codec),
    );
    const mjpegSupported = Boolean(
        active?.mjpeg_supported && previewUrls?.mjpeg,
    );
    const webrtcSupported = Boolean(
        active?.webrtc_supported &&
            browserCanPreviewWebrtcCodec(active?.codec),
    );
    const webrtcEnabled = webrtcSupported;
    const streamRunning = active?.running ?? false;
    const webrtcModeEnabled = webrtcEnabled && streamRunning;
    const hlsModeEnabled = hlsSupported && streamRunning;
    const flvModeEnabled = flvSupported && streamRunning;
    const mjpegModeEnabled = mjpegSupported && streamRunning;
    const webrtcPreviewReady = webrtcModeEnabled && webrtcReady;
    const hlsPreviewReady = hlsModeEnabled && hlsReady;
    const flvPreviewReady = flvModeEnabled && flvReady;
    const mjpegPreviewReady = mjpegModeEnabled && mjpegReady;
    const selectedModeEnabled =
        (mode === 'webrtc' && webrtcPreviewReady) ||
        (mode === 'hls' && hlsModeEnabled) ||
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

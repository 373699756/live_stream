import type { MediaStreamInfo, StreamName } from '../api/types';

export const previewStreams: StreamName[] = ['main', 'sub'];

const streamLabels: Record<StreamName, string> = {
    main: '主码流',
    sub: '子码流',
};

export function streamLabel(stream: string) {
    return stream === 'main' || stream === 'sub'
        ? streamLabels[stream]
        : stream || '--';
}

export function isPreviewStream(stream: string): stream is StreamName {
    return stream === 'main' || stream === 'sub';
}

export function findStreamInfo(
    streamInfos: MediaStreamInfo[],
    stream: StreamName,
) {
    return (
        streamInfos.find((streamInfo) => streamInfo.stream === stream) ?? null
    );
}

import type {
    MediaPreviewUrls,
    MediaSessionInfo,
    MediaStreamInfo,
} from '../api/types';
import {
    findStreamInfo,
    previewStreams,
    streamLabel,
} from './streamInfoNames';

interface ProtocolRow {
    activeSize: number;
    label: string;
    protocol: string;
    ready: string;
    url: string;
}

interface StreamProtocolPanelProps {
    streamInfos: MediaStreamInfo[];
    urlsByStream: Record<string, MediaPreviewUrls>;
    sessions: MediaSessionInfo[];
}

function readyText(value: boolean | undefined) {
    return value ? 'ready' : 'not ready';
}

function protocolReady(streamInfo: MediaStreamInfo, protocol: string) {
    if (protocol === 'HLS') {
        return [
            readyText(streamInfo.hls_ready),
            streamInfo.hls_supported ? 'supported' : 'unsupported',
        ].join(' / ');
    }
    if (protocol === 'HTTP-FLV') {
        return [
            readyText(streamInfo.http_flv_ready),
            streamInfo.http_flv_supported ? 'supported' : 'unsupported',
        ].join(' / ');
    }
    if (protocol === 'MJPEG') {
        return [
            readyText(streamInfo.mjpeg_ready),
            streamInfo.mjpeg_supported ? 'supported' : 'unsupported',
        ].join(' / ');
    }
    if (protocol === 'WebRTC') {
        return [
            readyText(streamInfo.webrtc_ready),
            streamInfo.webrtc_supported ? 'supported' : 'unsupported',
        ].join(' / ');
    }
    if (protocol === 'RTSP') {
        return streamInfo.track_ready === true
            ? 'track ready'
            : 'track not ready';
    }
    return streamInfo.running === true ? 'available' : 'not running';
}

function protocolActiveSize(
    sessions: MediaSessionInfo[],
    stream: string,
    protocol: string,
) {
    return sessions.filter(
        (session) =>
            session.stream === stream && session.protocol === protocol,
    ).length;
}

function protocolRows(
    streamInfo: MediaStreamInfo,
    urls: MediaPreviewUrls | undefined,
    sessions: MediaSessionInfo[],
): ProtocolRow[] {
    return [
        { label: 'RTSP', url: urls?.rtsp || '', protocol: 'rtsp' },
        { label: 'HLS', url: urls?.hls || '', protocol: 'hls' },
        { label: 'HTTP-FLV', url: urls?.http_flv || '', protocol: 'http_flv' },
        { label: 'MJPEG', url: urls?.mjpeg || '', protocol: 'mjpeg' },
        {
            label: 'WebRTC WHEP',
            url: urls?.webrtc_whep || '',
            protocol: 'webrtc',
        },
        { label: 'Snapshot', url: urls?.snapshot || '', protocol: 'snapshot' },
    ].map((row) => ({
        ...row,
        activeSize: protocolActiveSize(
            sessions,
            streamInfo.stream,
            row.protocol,
        ),
        ready: protocolReady(
            streamInfo,
            row.label === 'WebRTC WHEP' ? 'WebRTC' : row.label,
        ),
    }));
}

export function StreamProtocolPanel({
    streamInfos,
    urlsByStream,
    sessions,
}: StreamProtocolPanelProps) {
    return (
        <section className="panel wide-panel stream-protocol-panel">
            <div className="page-heading">
                <div>
                    <h2>访问地址与协议状态</h2>
                    <p>后端生成的访问地址、协议 ready 和活动会话数</p>
                </div>
            </div>
            <div className="stream-protocol-grid">
                {previewStreams.map((stream) => {
                    const streamInfo = findStreamInfo(streamInfos, stream);
                    if (!streamInfo) {
                        return (
                            <div
                                className="stream-protocol-group"
                                key={stream}
                            >
                                <h3>{streamLabel(stream)}</h3>
                                <div className="empty-state">运行态不可用</div>
                            </div>
                        );
                    }
                    return (
                        <div className="stream-protocol-group" key={stream}>
                            <h3>{streamLabel(stream)}</h3>
                            <div className="stream-protocol-table">
                                {protocolRows(
                                    streamInfo,
                                    urlsByStream[stream],
                                    sessions,
                                ).map((row) => (
                                    <div
                                        className="stream-protocol-row"
                                        key={row.label}
                                    >
                                        <span>{row.label}</span>
                                        <code>{row.url || 'unavailable'}</code>
                                        <em>{row.ready}</em>
                                        <strong>{row.activeSize}</strong>
                                    </div>
                                ))}
                            </div>
                        </div>
                    );
                })}
            </div>
        </section>
    );
}

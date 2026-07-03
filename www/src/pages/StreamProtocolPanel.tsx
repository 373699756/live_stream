import { useState } from 'react';
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
    return value ? '就绪' : '未就绪';
}

function protocolReady(streamInfo: MediaStreamInfo, protocol: string) {
    if (protocol === 'HLS') {
        return streamInfo.hls_supported
            ? readyText(streamInfo.hls_ready)
            : '不支持';
    }
    if (protocol === 'HTTP-FLV') {
        return streamInfo.http_flv_supported
            ? readyText(streamInfo.http_flv_ready)
            : '不支持';
    }
    if (protocol === 'MJPEG') {
        return streamInfo.mjpeg_supported
            ? readyText(streamInfo.mjpeg_ready)
            : '不支持';
    }
    if (protocol === 'WebRTC') {
        return streamInfo.webrtc_supported
            ? readyText(streamInfo.webrtc_ready)
            : '不支持';
    }
    if (protocol === 'RTSP') {
        return streamInfo.track_ready === true ? '轨道就绪' : '轨道未就绪';
    }
    return streamInfo.running === true ? '可用' : '未运行';
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
    const [copiedKey, setCopiedKey] = useState('');

    const copyUrl = (url: string) => {
        if (!url || !navigator.clipboard) {
            return;
        }
        void navigator.clipboard.writeText(url);
    };

    return (
        <section className="panel wide-panel stream-protocol-panel">
            <div className="page-heading">
                <div>
                    <h2>访问地址与协议状态</h2>
                    <p>后端生成的访问地址、协议就绪状态和活动会话数</p>
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
                                        <code>{row.url || '地址不可用'}</code>
                                        <em
                                            className={
                                                row.ready.includes('不支持') ||
                                                row.ready.includes('未')
                                                    ? 'protocol-state muted'
                                                    : 'protocol-state ready'
                                            }
                                        >
                                            {row.ready}
                                        </em>
                                        <strong>{row.activeSize}</strong>
                                        <button
                                            type="button"
                                            disabled={!row.url}
                                            title={
                                                row.url
                                                    ? '复制访问地址'
                                                    : '地址不可用'
                                            }
                                            onClick={() => {
                                                copyUrl(row.url);
                                                setCopiedKey(
                                                    `${stream}-${row.label}`,
                                                );
                                                window.setTimeout(
                                                    () => setCopiedKey(''),
                                                    1200,
                                                );
                                            }}
                                        >
                                            {copiedKey ===
                                            `${stream}-${row.label}`
                                                ? '已复制'
                                                : '复制'}
                                        </button>
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

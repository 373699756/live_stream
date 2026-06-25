import type {
    MediaPreviewUrls,
    MediaSessionInfo,
    MediaSessionsResponse,
    MediaStreamInfo,
    StreamName,
} from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { previewValueText } from '../components/previewDisplay';
import { useMediaStreamsInfo } from '../hooks/useMediaStreamsInfo';

const streams: StreamName[] = ['main', 'sub'];
const streamLabels: Record<StreamName, string> = {
    main: '主码流',
    sub: '子码流',
};
const streamInfoTimeoutMs = 3000;

interface ProtocolRow {
    activeSize: number;
    label: string;
    protocol: string;
    ready: string;
    url: string;
}

function streamLabel(stream: string) {
    return stream === 'main' || stream === 'sub'
        ? streamLabels[stream]
        : stream || '--';
}

function validStream(stream: string): stream is StreamName {
    return stream === 'main' || stream === 'sub';
}

function readyText(value: boolean) {
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
        return streamInfo.track_ready ? 'track ready' : 'track not ready';
    }
    return streamInfo.running ? 'available' : 'not running';
}

function sessionSize(
    sessions: MediaSessionInfo[],
    stream: StreamName,
    protocol?: string,
) {
    return sessions.filter(
        (session) =>
            session.stream === stream &&
            (!protocol || session.protocol === protocol),
    ).length;
}

function summarySize(
    sessionSummary: Omit<MediaSessionsResponse, 'items'>,
    protocol: string,
) {
    if (protocol === 'rtsp') {
        return sessionSummary.rtsp_active_sessions ?? 0;
    }
    if (protocol === 'http_flv') {
        return sessionSummary.http_flv_active_clients ?? 0;
    }
    if (protocol === 'mjpeg') {
        return sessionSummary.mjpeg_active_clients ?? 0;
    }
    if (protocol === 'webrtc') {
        return sessionSummary.webrtc_active_peers ?? 0;
    }
    return 0;
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
        activeSize: sessionSize(sessions, streamInfo.stream, row.protocol),
        ready: protocolReady(
            streamInfo,
            row.label === 'WebRTC WHEP' ? 'WebRTC' : row.label,
        ),
    }));
}

function infoForStream(streamInfos: MediaStreamInfo[], stream: StreamName) {
    return (
        streamInfos.find((streamInfo) => streamInfo.stream === stream) ?? null
    );
}

function sessionGroupTitle(protocol: string) {
    switch (protocol) {
        case 'rtsp':
            return 'RTSP';
        case 'http_flv':
            return 'HTTP-FLV';
        case 'mjpeg':
            return 'MJPEG';
        case 'webrtc':
            return 'WebRTC';
        default:
            return protocol || '--';
    }
}

function sessionPeer(session: MediaSessionInfo) {
    return (
        session.client_ip ||
        session.remote_address ||
        session.peer_id ||
        session.client_id ||
        '--'
    );
}

function sessionTransport(session: MediaSessionInfo) {
    if (session.transport) {
        return session.transport;
    }
    if (session.ice_selected) {
        return 'ice selected';
    }
    return '--';
}

function sessionState(session: MediaSessionInfo) {
    return session.state || session.stream_state || 'unknown';
}

function groupedSessions(sessions: MediaSessionInfo[]) {
    return ['rtsp', 'http_flv', 'mjpeg', 'webrtc'].map((protocol) => ({
        protocol,
        sessions: sessions.filter((session) => session.protocol === protocol),
    }));
}

function totalSummarySessions(
    sessionSummary: Omit<MediaSessionsResponse, 'items'>,
) {
    return (
        (sessionSummary.rtsp_active_sessions ?? 0) +
        (sessionSummary.http_flv_active_clients ?? 0) +
        (sessionSummary.mjpeg_active_clients ?? 0) +
        (sessionSummary.webrtc_active_peers ?? 0)
    );
}

function booleanState(value: boolean | undefined) {
    return value ? 'ready' : 'not ready';
}

function webrtcPortRange(sessionSummary: Omit<MediaSessionsResponse, 'items'>) {
    const base = sessionSummary.webrtc_local_port_base ?? 0;
    const maxPeers = sessionSummary.webrtc_max_peers ?? 0;
    if (base <= 0) {
        return '--';
    }
    const end = base + Math.max(maxPeers - 1, 0);
    return end > base ? `${base}-${end}` : String(base);
}

export function StreamInfoPage() {
    const { statuses, urlsByStream, sessions, sessionSummary, error } =
        useMediaStreamsInfo({
            previewStreams: streams,
            includeSessions: true,
            refreshIntervalMs: 5000,
            refreshPreviewUrlsOnInterval: true,
            statusTimeoutMs: streamInfoTimeoutMs,
            previewUrlTimeoutMs: streamInfoTimeoutMs,
            sessionTimeoutMs: streamInfoTimeoutMs,
            statusErrorMessage: '媒体运行态加载失败',
            previewUrlErrorMessage: '媒体访问地址加载失败',
            sessionErrorMessage: '媒体会话加载失败',
        });
    const safeStreamInfos = statuses.filter((streamInfo) =>
        validStream(streamInfo.stream),
    );
    const safeSessions = sessions.filter((session) =>
        validStream(session.stream),
    );
    const sessionGroups = groupedSessions(safeSessions);
    const totalActiveSessions = totalSummarySessions(sessionSummary);

    return (
        <div className="page-grid stream-info-grid">
            <div className="page-heading stream-info-heading">
                <div>
                    <h2>码流信息</h2>
                    <p>媒体访问地址、码流运行状态和客户端会话诊断</p>
                </div>
            </div>

            {error ? (
                <div className="status-note error-note">{error}</div>
            ) : null}

            <section className="panel wide-panel stream-info-panel">
                <div className="page-heading">
                    <div>
                        <h2>运行总览</h2>
                        <p>主/子码流运行态、subscription/client 和缓存状态</p>
                    </div>
                </div>
                <div className="stream-info-cards">
                    {streams.map((stream) => {
                        const streamInfo = infoForStream(
                            safeStreamInfos,
                            stream,
                        );
                        if (!streamInfo) {
                            return (
                                <article
                                    className="stream-info-card unavailable"
                                    key={stream}
                                >
                                    <div>
                                        <h3>{streamLabel(stream)}</h3>
                                        <StatusBadge
                                            state="error"
                                            label="运行态不可用"
                                        />
                                    </div>
                                    <span>后端未返回该码流运行信息</span>
                                </article>
                            );
                        }
                        return (
                            <article className="stream-info-card" key={stream}>
                                <div>
                                    <h3>{streamLabel(stream)}</h3>
                                    <StatusBadge
                                        state={
                                            streamInfo.running
                                                ? 'running'
                                                : 'pending'
                                        }
                                        label={
                                            streamInfo.running
                                                ? '运行中'
                                                : '未运行'
                                        }
                                    />
                                </div>
                                <dl>
                                    <div>
                                        <dt>编码</dt>
                                        <dd>
                                            {previewValueText(streamInfo.codec)}
                                        </dd>
                                    </div>
                                    <div>
                                        <dt>分辨率</dt>
                                        <dd>
                                            {previewValueText(
                                                streamInfo.resolution,
                                                '--',
                                            )}
                                        </dd>
                                    </div>
                                    <div>
                                        <dt>帧率</dt>
                                        <dd>
                                            {previewValueText(
                                                streamInfo.fps,
                                                '--',
                                            )}{' '}
                                            fps
                                        </dd>
                                    </div>
                                    <div>
                                        <dt>码率</dt>
                                        <dd>
                                            {previewValueText(
                                                streamInfo.bitrate_kbps,
                                                '--',
                                            )}{' '}
                                            kbps
                                        </dd>
                                    </div>
                                    <div>
                                        <dt>读者/客户端</dt>
                                        <dd>
                                            {streamInfo.active_subscriptions} /{' '}
                                            {streamInfo.preview_clients}
                                        </dd>
                                    </div>
                                    <div>
                                        <dt>缓存</dt>
                                        <dd>
                                            {streamInfo.cached_frames} 帧 /{' '}
                                            {streamInfo.cached_bytes} B
                                        </dd>
                                    </div>
                                    <div>
                                        <dt>最近 DTS</dt>
                                        <dd>
                                            {previewValueText(
                                                streamInfo.last_dts,
                                                '--',
                                            )}
                                        </dd>
                                    </div>
                                </dl>
                            </article>
                        );
                    })}
                </div>
            </section>

            <section className="panel wide-panel stream-protocol-panel">
                <div className="page-heading">
                    <div>
                        <h2>访问地址与协议状态</h2>
                        <p>后端生成的访问地址、协议 ready 和活动会话数</p>
                    </div>
                </div>
                <div className="stream-protocol-grid">
                    {streams.map((stream) => {
                        const streamInfo = infoForStream(
                            safeStreamInfos,
                            stream,
                        );
                        if (!streamInfo) {
                            return (
                                <div
                                    className="stream-protocol-group"
                                    key={stream}
                                >
                                    <h3>{streamLabel(stream)}</h3>
                                    <div className="empty-state">
                                        运行态不可用
                                    </div>
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
                                        safeSessions,
                                    ).map((row) => (
                                        <div
                                            className="stream-protocol-row"
                                            key={row.label}
                                        >
                                            <span>{row.label}</span>
                                            <code>
                                                {row.url || 'unavailable'}
                                            </code>
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

            <section className="panel wide-panel stream-webrtc-panel">
                <div className="page-heading">
                    <div>
                        <h2>WebRTC 外网诊断</h2>
                        <p>
                            公网直连依赖 HTTP/HTTPS 信令可达和 WebRTC UDP
                            端口可达
                        </p>
                    </div>
                </div>
                <div className="stream-webrtc-info">
                    <div>
                        <span>服务</span>
                        <strong>
                            {sessionSummary.webrtc_enabled
                                ? 'enabled'
                                : 'disabled'}
                        </strong>
                    </div>
                    <div>
                        <span>对外地址</span>
                        <strong>
                            {sessionSummary.webrtc_public_ip || '--'}
                        </strong>
                    </div>
                    <div>
                        <span>UDP 端口</span>
                        <strong>{webrtcPortRange(sessionSummary)}</strong>
                    </div>
                    <div>
                        <span>ICE Servers</span>
                        <strong>
                            {sessionSummary.webrtc_ice_servers ?? 0}
                        </strong>
                    </div>
                    <div>
                        <span>Signaling</span>
                        <strong>
                            {booleanState(
                                sessionSummary.webrtc_signaling_ready,
                            )}
                        </strong>
                    </div>
                    <div>
                        <span>ICE</span>
                        <strong>
                            {booleanState(sessionSummary.webrtc_ice_ready)}
                        </strong>
                    </div>
                    <div>
                        <span>DTLS</span>
                        <strong>
                            {booleanState(sessionSummary.webrtc_dtls_ready)}
                        </strong>
                    </div>
                    <div>
                        <span>SRTP</span>
                        <strong>
                            {booleanState(sessionSummary.webrtc_srtp_ready)}
                        </strong>
                    </div>
                    <div>
                        <span>Selected ICE</span>
                        <strong>
                            {sessionSummary.webrtc_selected_ice_pairs ?? 0}
                        </strong>
                    </div>
                </div>
            </section>

            <section className="panel wide-panel stream-session-panel">
                <div className="page-heading">
                    <div>
                        <h2>会话诊断</h2>
                        <p>RTSP、HTTP-FLV、MJPEG 和 WebRTC 活动连接</p>
                    </div>
                    <span className="stream-session-total">
                        活动 <strong>{totalActiveSessions}</strong>
                    </span>
                </div>
                <div className="stream-session-groups">
                    {sessionGroups.map((group) => {
                        const summaryActive = summarySize(
                            sessionSummary,
                            group.protocol,
                        );
                        const missingDetails =
                            group.protocol === 'rtsp' &&
                            summaryActive > 0 &&
                            group.sessions.length === 0;
                        return (
                            <div
                                className="stream-session-group"
                                key={group.protocol}
                            >
                                <div className="stream-session-group-heading">
                                    <h3>{sessionGroupTitle(group.protocol)}</h3>
                                    <span>
                                        {summaryActive || group.sessions.length}
                                    </span>
                                </div>
                                {missingDetails ? (
                                    <div className="stream-session-empty">
                                        RTSP 控制会话存在，详情暂不可用
                                    </div>
                                ) : null}
                                {!missingDetails &&
                                group.sessions.length === 0 ? (
                                    <div className="stream-session-empty">
                                        无活动会话
                                    </div>
                                ) : null}
                                {group.sessions.map((session, index) => (
                                    <div
                                        className="stream-session-row"
                                        key={
                                            session.session_id ||
                                            `${session.protocol}-${index}`
                                        }
                                    >
                                        <span>
                                            {streamLabel(session.stream)} /{' '}
                                            {sessionPeer(session)}
                                        </span>
                                        <strong>{sessionState(session)}</strong>
                                        <em>
                                            pending {session.pending_bytes ?? 0}{' '}
                                            / {sessionTransport(session)}
                                        </em>
                                        {session.close_reason ? (
                                            <small>
                                                {session.close_reason}
                                            </small>
                                        ) : null}
                                    </div>
                                ))}
                            </div>
                        );
                    })}
                </div>
            </section>
        </div>
    );
}

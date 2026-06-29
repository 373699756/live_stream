import type { MediaSessionInfo, MediaSessionsResponse } from '../api/types';
import { streamLabel } from './streamInfoNames';

type MediaSessionSummary = Omit<MediaSessionsResponse, 'items'>;

interface StreamSessionGroup {
    protocol: string;
    sessions: MediaSessionInfo[];
}

interface StreamSessionPanelProps {
    sessions: MediaSessionInfo[];
    sessionSummary: MediaSessionSummary;
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

function sessionStatusText(session: MediaSessionInfo) {
    return session.state || session.stream_state || 'unknown';
}

function sessionGroups(sessions: MediaSessionInfo[]): StreamSessionGroup[] {
    return ['rtsp', 'http_flv', 'mjpeg', 'webrtc'].map((protocol) => ({
        protocol,
        sessions: sessions.filter((session) => session.protocol === protocol),
    }));
}

function summaryActiveSize(
    sessionSummary: MediaSessionSummary,
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

function totalActiveSessions(sessionSummary: MediaSessionSummary) {
    return (
        (sessionSummary.rtsp_active_sessions ?? 0) +
        (sessionSummary.http_flv_active_clients ?? 0) +
        (sessionSummary.mjpeg_active_clients ?? 0) +
        (sessionSummary.webrtc_active_peers ?? 0)
    );
}

export function StreamSessionPanel({
    sessions,
    sessionSummary,
}: StreamSessionPanelProps) {
    const groups = sessionGroups(sessions);

    return (
        <section className="panel wide-panel stream-session-panel">
            <div className="page-heading">
                <div>
                    <h2>会话诊断</h2>
                    <p>RTSP、HTTP-FLV、MJPEG 和 WebRTC 活动连接</p>
                </div>
                <span className="stream-session-total">
                    活动 <strong>{totalActiveSessions(sessionSummary)}</strong>
                </span>
            </div>
            <div className="stream-session-groups">
                {groups.map((group) => {
                    const summaryActive = summaryActiveSize(
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
                            {!missingDetails && group.sessions.length === 0 ? (
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
                                    <strong>
                                        {sessionStatusText(session)}
                                    </strong>
                                    <em>
                                        pending {session.pending_bytes ?? 0} /{' '}
                                        {sessionTransport(session)}
                                    </em>
                                    {session.close_reason ? (
                                        <small>{session.close_reason}</small>
                                    ) : null}
                                </div>
                            ))}
                        </div>
                    );
                })}
            </div>
        </section>
    );
}

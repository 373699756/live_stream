import { useMediaStreamsInfo } from '../hooks/useMediaStreamsInfo';
import { StreamOverviewPanel } from './StreamOverviewPanel';
import { StreamProtocolPanel } from './StreamProtocolPanel';
import { StreamSessionPanel } from './StreamSessionPanel';
import { StreamWebrtcPanel } from './StreamWebrtcPanel';
import { isPreviewStream, previewStreams } from './streamInfoNames';

const streamInfoTimeoutMs = 3000;

export function StreamInfoPage() {
    const { statuses, urlsByStream, sessions, sessionSummary, error } =
        useMediaStreamsInfo({
            previewStreams,
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
    const streamInfos = statuses.filter((streamInfo) =>
        isPreviewStream(streamInfo.stream),
    );
    const activeSessions = sessions.filter((session) =>
        isPreviewStream(session.stream),
    );

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

            <StreamOverviewPanel streamInfos={streamInfos} />
            <StreamProtocolPanel
                streamInfos={streamInfos}
                urlsByStream={urlsByStream}
                sessions={activeSessions}
            />
            <StreamWebrtcPanel sessionSummary={sessionSummary} />
            <StreamSessionPanel
                sessions={activeSessions}
                sessionSummary={sessionSummary}
            />
        </div>
    );
}

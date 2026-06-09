import type {
  MediaPlaybackUrls,
  MediaSessionInfo,
  MediaStreamRuntime,
  StreamName,
} from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { previewValueText } from '../components/previewDisplay';
import { useMediaRuntime } from '../hooks/useMediaRuntime';

const streams: StreamName[] = ['main', 'sub'];
const streamLabels: Record<StreamName, string> = {
  main: '主码流',
  sub: '子码流',
};
const runtimeTimeoutMs = 3000;

function streamLabel(stream: string) {
  return stream === 'main' || stream === 'sub' ? streamLabels[stream] : stream || '--';
}

function validStream(stream: string): stream is StreamName {
  return stream === 'main' || stream === 'sub';
}

function readyText(value: boolean) {
  return value ? 'ready' : 'not ready';
}

function protocolReady(runtime: MediaStreamRuntime, protocol: string) {
  if (protocol === 'HLS') {
    return [
      readyText(runtime.hls_ready),
      runtime.hls_supported ? 'supported' : 'unsupported',
    ].join(' / ');
  }
  if (protocol === 'HTTP-FLV') {
    return [
      readyText(runtime.http_flv_ready),
      runtime.http_flv_supported ? 'supported' : 'unsupported',
    ].join(' / ');
  }
  if (protocol === 'MJPEG') {
    return [
      readyText(runtime.mjpeg_ready),
      runtime.mjpeg_supported ? 'supported' : 'unsupported',
    ].join(' / ');
  }
  if (protocol === 'WebRTC') {
    return [
      readyText(runtime.webrtc_ready),
      runtime.webrtc_supported ? 'supported' : 'unsupported',
    ].join(' / ');
  }
  if (protocol === 'RTSP') {
    return runtime.track_ready ? 'track ready' : 'track not ready';
  }
  return runtime.running ? 'available' : 'not running';
}

function sessionCount(
  sessions: MediaSessionInfo[],
  stream: StreamName,
  protocol?: string,
) {
  return sessions.filter((session) => (
    session.stream === stream &&
    (!protocol || session.protocol === protocol)
  )).length;
}

function protocolRows(
  runtime: MediaStreamRuntime,
  urls: MediaPlaybackUrls | undefined,
  sessions: MediaSessionInfo[],
) {
  return [
    { label: 'RTSP', url: urls?.rtsp || '', protocol: 'rtsp' },
    { label: 'HLS', url: urls?.hls || '', protocol: 'hls' },
    { label: 'HTTP-FLV', url: urls?.http_flv || '', protocol: 'http_flv' },
    { label: 'MJPEG', url: urls?.mjpeg || '', protocol: 'mjpeg' },
    { label: 'WebRTC WHEP', url: urls?.webrtc_whep || '', protocol: 'webrtc' },
    { label: 'Snapshot', url: urls?.snapshot || '', protocol: 'snapshot' },
  ].map((row) => ({
    ...row,
    ready: protocolReady(
      runtime,
      row.label === 'WebRTC WHEP' ? 'WebRTC' : row.label,
    ),
    sessions: sessionCount(sessions, runtime.stream, row.protocol),
  }));
}

export function StreamInfoPage() {
  const {
    statuses,
    urlsByStream,
    sessions,
    error,
  } = useMediaRuntime({
    playbackStreams: streams,
    includeSessions: true,
    refreshIntervalMs: 5000,
    refreshPlaybackUrlsOnInterval: true,
    statusTimeoutMs: runtimeTimeoutMs,
    playbackUrlTimeoutMs: runtimeTimeoutMs,
    sessionTimeoutMs: runtimeTimeoutMs,
    statusErrorMessage: '媒体运行态加载失败',
    playbackUrlErrorMessage: '媒体访问地址加载失败',
    sessionErrorMessage: '媒体会话加载失败',
  });
  const safeRuntimes = statuses.filter((runtime) => validStream(runtime.stream));
  const safeSessions = sessions.filter((session) => validStream(session.stream));

  return (
    <div className="page-grid stream-info-grid">
      <div className="page-heading stream-info-heading">
        <div>
          <h2>码流信息</h2>
          <p>媒体访问地址、码流运行状态和客户端会话诊断</p>
        </div>
      </div>

      <section className="panel">
        <div className="page-heading">
          <div>
            <h2>访问地址</h2>
            <p>后端生成的 RTSP、HLS、HTTP-FLV、MJPEG、WebRTC 和抓图地址</p>
          </div>
        </div>
        {error && <div className="status-note error-note">{error}</div>}
        <div className="address-table">
          {streams.map((stream) => {
            const runtime = safeRuntimes.find((item) => item.stream === stream);
            if (!runtime) {
              return (
                <div key={stream}>
                  <strong>{streamLabel(stream)}</strong>
                  <span>运行态不可用</span>
                </div>
              );
            }
            return (
              <div key={stream}>
                <strong>
                  {streamLabel(stream)} / {runtime.running ? '运行中' : '未运行'}
                </strong>
                <span>
                  {previewValueText(runtime.codec)} /{' '}
                  {previewValueText(runtime.resolution, '--')} /{' '}
                  {previewValueText(runtime.fps, '--')}fps
                </span>
                {protocolRows(runtime, urlsByStream[stream], safeSessions).map((row) => (
                  <code key={row.label}>
                    {row.label}: {row.url || 'unavailable'} [{row.ready}, sessions {row.sessions}]
                  </code>
                ))}
              </div>
            );
          })}
        </div>
      </section>

      <section className="panel">
        <div className="page-heading">
          <div>
            <h2>码流运行状态</h2>
            <p>主/子码流 ready、reader、缓存和协议可用性</p>
          </div>
        </div>
        <div className="info-table">
          {safeRuntimes.length === 0 ? (
            <div>
              <span>码流运行态不可用</span>
              <strong>--</strong>
            </div>
          ) : safeRuntimes.map((runtime) => (
            <div key={runtime.stream}>
              <span>
                {streamLabel(runtime.stream)} / readers {runtime.reader_count} /{' '}
                clients {runtime.client_count}
              </span>
              <strong>
                <StatusBadge state={runtime.running ? 'running' : 'pending'} />
                {' '}
                cache {runtime.cached_frames} frames / {runtime.cached_bytes} bytes
              </strong>
            </div>
          ))}
        </div>
      </section>

      <section className="panel">
        <div className="page-heading">
          <div>
            <h2>会话诊断</h2>
            <p>媒体 reader、RTSP、HTTP-FLV、MJPEG 和 WebRTC 会话连接状态</p>
          </div>
        </div>
        <div className="info-table">
          {safeSessions.length === 0 ? (
            <div>
              <span>当前无活动会话</span>
              <strong>0</strong>
            </div>
          ) : safeSessions.map((session, index) => (
            <div key={session.session_id || `${session.protocol}-${session.stream}-${index}`}>
              <span>
                {session.protocol || '--'} / {streamLabel(session.stream)} /{' '}
                {session.client_ip || '--'}
              </span>
              <strong>
                {session.state || 'unknown'} / pending {session.pending_bytes ?? 0}
                {session.close_reason ? ` / ${session.close_reason}` : ''}
              </strong>
            </div>
          ))}
        </div>
      </section>
    </div>
  );
}

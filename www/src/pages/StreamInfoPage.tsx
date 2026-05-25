import {
  flvStreamUrl,
  hlsPlaylistUrl,
  mjpegStreamUrl,
  snapshotUrl,
} from '../api/client';
import type { RtspConfig, StreamName } from '../api/types';
import { useLiveView } from '../hooks/useLiveView';

function rtspAddress(config: RtspConfig | null, stream: StreamName) {
  const host = window.location.hostname || 'device';
  const port = config?.port || 554;
  const portText = port === 554 ? '' : `:${port}`;
  const path = config?.paths?.[stream] || `/live/${stream}`;
  const credential = config?.auth_required ? 'admin:<password>@' : '';
  return `rtsp://${credential}${host}${portText}${path}`;
}

const streamLabels: Record<StreamName, string> = {
  main: '主码流',
  sub: '子码流',
};

export function StreamInfoPage() {
  const { rtspConfig, error } = useLiveView();

  return (
    <div className="page-grid stream-info-grid">
      <section className="panel">
        <div className="page-heading">
          <div>
            <h2>访问地址</h2>
            <p>RTSP、HLS、HTTP-FLV、MJPEG 和抓图接口地址</p>
          </div>
        </div>
        {error && <div className="status-note error-note">{error}</div>}
        {rtspConfig?.auth_required && (
          <div className="status-note">
            RTSP 已启用鉴权，地址中的 &lt;password&gt; 替换为登录密码
          </div>
        )}
        <div className="address-table">
          {(['main', 'sub'] as StreamName[]).map((stream) => (
            <div key={stream}>
              <strong>{streamLabels[stream]} RTSP</strong>
              <code>{rtspAddress(rtspConfig, stream)}</code>
            </div>
          ))}
          <div>
            <strong>主码流 HLS</strong>
            <code>{hlsPlaylistUrl('main')}</code>
          </div>
          <div>
            <strong>子码流 HLS</strong>
            <code>{hlsPlaylistUrl('sub')}</code>
          </div>
          <div>
            <strong>主码流 HTTP-FLV</strong>
            <code>{flvStreamUrl('main')}</code>
          </div>
          <div>
            <strong>子码流 HTTP-FLV</strong>
            <code>{flvStreamUrl('sub')}</code>
          </div>
          <div>
            <strong>主码流 MJPEG</strong>
            <code>{mjpegStreamUrl('main')}</code>
          </div>
          <div>
            <strong>子码流 MJPEG</strong>
            <code>{mjpegStreamUrl('sub')}</code>
          </div>
          <div>
            <strong>主码流抓图</strong>
            <code>{snapshotUrl('main')}</code>
          </div>
          <div>
            <strong>子码流抓图</strong>
            <code>{snapshotUrl('sub')}</code>
          </div>
        </div>
      </section>
    </div>
  );
}

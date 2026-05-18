import { useState } from 'react';
import { useLiveView } from '../hooks/useLiveView';
import type { RtspConfig, StreamName } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { VideoPreview } from '../components/VideoPreview';

function rtspAddress(config: RtspConfig | null, stream: StreamName) {
  const host = window.location.hostname || 'device';
  const port = config?.port || 554;
  const portText = port === 554 ? '' : `:${port}`;
  const path = config?.paths?.[stream] || `/live/${stream}`;
  return `rtsp://${host}${portText}${path}`;
}

export function LiveViewPage() {
  const [stream, setStream] = useState<StreamName>('sub');
  const { statuses, rtspConfig } = useLiveView();

  const runningStreams = statuses.filter((item) => item.state === 'running');

  return (
    <div className="page-grid live-grid">
      <VideoPreview stream={stream} statuses={statuses} onStreamChange={setStream} />
      <aside className="side-panel">
        <div className="panel-title">码流状态</div>
        {statuses.map((item) => (
          <div className="kv-card" key={item.stream}>
            <div>
              <strong>{item.stream === 'main' ? '主码流' : '子码流'}</strong>
              <span>{item.codec} / {item.resolution}</span>
            </div>
            <StatusBadge state={item.state === 'running' ? 'running' : 'pending'} />
          </div>
        ))}
        <div className="panel-title">访问地址</div>
        <div className="address-list">
          {runningStreams.map((item) => (
            <code key={item.stream}>{rtspAddress(rtspConfig, item.stream)}</code>
          ))}
          <code>/api/hls/main/index.m3u8</code>
          <code>/api/hls/sub/index.m3u8</code>
          <code>/api/flv/main.flv</code>
          <code>/api/flv/sub.flv</code>
          <code>/api/snapshot/main.jpg</code>
          <code>/api/snapshot/sub.jpg</code>
        </div>
      </aside>
    </div>
  );
}

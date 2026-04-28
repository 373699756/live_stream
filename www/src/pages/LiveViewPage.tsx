import { useEffect, useState } from 'react';
import { api } from '../api/client';
import type { StreamName, StreamStatus } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { VideoPreview } from '../components/VideoPreview';

export function LiveViewPage() {
  const [stream, setStream] = useState<StreamName>('main');
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);

  useEffect(() => {
    void api.getStreamStatus().then(setStatuses);
  }, []);

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
            <StatusBadge state={item.state === 'running' ? 'running' : 'error'} />
          </div>
        ))}
        <div className="panel-title">访问地址</div>
        <div className="address-list">
          <code>rtsp://device/live/main</code>
          <code>rtsp://device/live/sub</code>
          <code>/api/snapshot/{stream}.jpg</code>
        </div>
      </aside>
    </div>
  );
}

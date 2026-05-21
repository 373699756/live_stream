import { useState } from 'react';
import { snapshotUrl } from '../api/client';
import { useLiveView } from '../hooks/useLiveView';
import type { StreamName } from '../api/types';
import { VideoPreview } from '../components/VideoPreview';

export function LiveViewPage() {
  const [stream, setStream] = useState<StreamName>('main');
  const { statuses } = useLiveView();

  const captureSnapshot = (nextStream: StreamName) => {
    window.open(snapshotUrl(nextStream, Date.now()), '_blank', 'noopener,noreferrer');
  };

  return (
    <div className="page-grid live-grid">
      <VideoPreview
        stream={stream}
        statuses={statuses}
        onStreamChange={setStream}
        onSnapshot={captureSnapshot}
      />
    </div>
  );
}

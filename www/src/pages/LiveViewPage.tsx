import { useState } from 'react';
import { snapshotUrl } from '../api/snapshot';
import { useLiveView } from '../hooks/useLiveView';
import type { StreamName } from '../api/types';
import { AiDetectionOverlay } from '../components/AiDetectionOverlay';
import { VideoPreview } from '../components/VideoPreview';
import { useAiStatus } from '../hooks/useAiStatus';

export function LiveViewPage() {
  const [stream, setStream] = useState<StreamName>('main');
  const { statuses } = useLiveView();
  const { status: aiStatus, error: aiError } = useAiStatus();
  const activeStatus = statuses.find((status) => status.stream === stream);

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
        surfaceOverlay={
          <AiDetectionOverlay
            frameResolution={activeStatus?.resolution}
            status={aiStatus}
            stream={stream}
            error={aiError}
          />
        }
      />
    </div>
  );
}

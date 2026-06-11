import { useState } from 'react';
import { useLiveView } from '../hooks/useLiveView';
import type { StreamName } from '../api/types';
import { AiDetectionOverlay } from '../components/AiDetectionOverlay';
import { VideoPreview } from '../components/VideoPreview';
import { useAiStatus } from '../hooks/useAiStatus';
import { useWebrtcConfig } from '../hooks/useWebrtcConfig';

export function LiveViewPage() {
    const [stream, setStream] = useState<StreamName>('main');
    const { statuses, previewUrls } = useLiveView(stream);
    const { status: aiStatus, error: aiError } = useAiStatus();
    const { config: webrtcConfig } = useWebrtcConfig();
    const activeStatus = statuses.find((status) => status.stream === stream);

    const captureSnapshot = (nextStream: StreamName) => {
        const snapshot =
            previewUrls?.stream === nextStream ? previewUrls.snapshot : '';
        if (!snapshot) {
            return;
        }
        const separator = snapshot.includes('?') ? '&' : '?';
        window.open(
            `${snapshot}${separator}t=${Date.now()}`,
            '_blank',
            'noopener,noreferrer',
        );
    };

    return (
        <div className="page-grid live-grid">
            <VideoPreview
                stream={stream}
                statuses={statuses}
                previewUrls={previewUrls}
                onStreamChange={setStream}
                onSnapshot={captureSnapshot}
                webrtcConfig={webrtcConfig}
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

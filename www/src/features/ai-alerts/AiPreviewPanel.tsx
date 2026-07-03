import type { ReactNode } from 'react';
import type {
    AiStatus,
    StreamName,
} from '../../api/types';
import { AiDetectionOverlay } from '../../components/AiDetectionOverlay';
import { VideoPreview } from '../../components/VideoPreview';
import type { useLiveView } from '../../hooks/useLiveView';

interface AiPreviewPanelProps {
    activeResolution?: string;
    aiStatus: AiStatus | null;
    error: string;
    fit?: 'contain' | 'cover';
    perimeterOverlay: ReactNode;
    previewStream: StreamName;
    previewUrls: ReturnType<typeof useLiveView>['previewUrls'];
    statuses: ReturnType<typeof useLiveView>['statuses'];
    onSnapshot: (stream: StreamName) => void;
    onStreamChange: (stream: StreamName) => void;
}

export function AiPreviewPanel({
    activeResolution,
    aiStatus,
    error,
    fit = 'cover',
    perimeterOverlay,
    previewStream,
    previewUrls,
    statuses,
    onSnapshot,
    onStreamChange,
}: AiPreviewPanelProps) {
    return (
        <>
            <VideoPreview
                stream={previewStream}
                statuses={statuses}
                previewUrls={previewUrls}
                onStreamChange={onStreamChange}
                onSnapshot={onSnapshot}
                fit={fit}
                surfaceOverlay={
                    <>
                        <AiDetectionOverlay
                            frameResolution={activeResolution}
                            fit={fit}
                            status={aiStatus}
                            stream={previewStream}
                            error={error}
                        />
                        {perimeterOverlay}
                    </>
                }
            />
        </>
    );
}

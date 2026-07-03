import type { ReactNode } from 'react';
import type {
    AiStats,
    AiStatus,
    AlarmInfoResponse,
    StreamName,
} from '../../api/types';
import { AiDetectionOverlay } from '../../components/AiDetectionOverlay';
import { VideoPreview } from '../../components/VideoPreview';
import type { useLiveView } from '../../hooks/useLiveView';
import { latestAlarmTimeText } from './aiAlertFormat';
import { numberText } from './aiConfigDraft';

interface EnabledTaskSummary {
    backendLabel: string;
    enabledTaskTotal: number;
}

interface AiPreviewPanelProps {
    activeResolution?: string;
    aiStatus: AiStatus | null;
    alarmInfo: AlarmInfoResponse | null;
    error: string;
    fit?: 'contain' | 'cover';
    lastAlarmEvent: Parameters<typeof latestAlarmTimeText>[1];
    perimeterOverlay: ReactNode;
    previewStream: StreamName;
    previewUrls: ReturnType<typeof useLiveView>['previewUrls'];
    statuses: ReturnType<typeof useLiveView>['statuses'];
    summary: AiStats;
    supportedTaskSummary: EnabledTaskSummary;
    onSnapshot: (stream: StreamName) => void;
    onStreamChange: (stream: StreamName) => void;
}

export function AiPreviewPanel({
    activeResolution,
    aiStatus,
    alarmInfo,
    error,
    fit = 'cover',
    lastAlarmEvent,
    perimeterOverlay,
    previewStream,
    previewUrls,
    statuses,
    summary,
    supportedTaskSummary,
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

            <section className="ai-status-compact">
                <div className="ai-status-kpis">
                    <div>
                        <span>事件</span>
                        <strong>
                            {supportedTaskSummary.enabledTaskTotal} 启用
                        </strong>
                    </div>
                    <div>
                        <span>后端</span>
                        <strong>{supportedTaskSummary.backendLabel}</strong>
                    </div>
                    <div>
                        <span>有效结果</span>
                        <strong>{numberText(summary.active_results)}</strong>
                    </div>
                    <div>
                        <span>最近耗时</span>
                        <strong>
                            {numberText(summary.last_inference_time_ms)} ms
                        </strong>
                    </div>
                    <div>
                        <span>最近报警</span>
                        <strong>
                            {latestAlarmTimeText(alarmInfo, lastAlarmEvent)}
                        </strong>
                    </div>
                </div>
            </section>
        </>
    );
}

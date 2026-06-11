import { useEffect, useState } from 'react';
import { mockMediaCapabilities } from '../api/mockVideo';
import { getMediaCapabilities } from '../api/video';
import type { MediaCapabilities, StreamName } from '../api/types';
import { useMediaStreamsInfo } from './useMediaStreamsInfo';

const statusTimeoutMs = 1800;
const defaultRefreshIntervalMs = 3000;

export function usePreviewMetadata(
    selectedStream?: StreamName,
    refreshIntervalMs = defaultRefreshIntervalMs,
) {
    const [capabilities, setCapabilities] = useState<MediaCapabilities>(
        mockMediaCapabilities,
    );
    const { statuses, previewUrls, refreshStatuses } = useMediaStreamsInfo({
        selectedStream,
        refreshIntervalMs,
        statusTimeoutMs,
        previewUrlTimeoutMs: statusTimeoutMs,
    });

    useEffect(() => {
        let mounted = true;
        void getMediaCapabilities({ timeoutMs: statusTimeoutMs })
            .then((nextCapabilities) => {
                if (mounted) {
                    setCapabilities(nextCapabilities);
                }
            })
            .catch(() => {
                if (mounted) {
                    setCapabilities(mockMediaCapabilities);
                }
            });
        return () => {
            mounted = false;
        };
    }, []);

    return { capabilities, statuses, previewUrls, refreshStatuses };
}

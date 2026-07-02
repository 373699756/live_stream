import { useEffect, useState } from 'react';
import { getMediaCapabilities } from '../api/video';
import type { MediaCapabilities, StreamName } from '../api/types';
import { useMediaStreamsInfo } from './useMediaStreamsInfo';

const statusTimeoutMs = 1800;
const defaultRefreshIntervalMs = 3000;

export function usePreviewMetadata(
    selectedStream?: StreamName,
    refreshIntervalMs = defaultRefreshIntervalMs,
) {
    const [capabilities, setCapabilities] =
        useState<MediaCapabilities | null>(null);
    const [capabilitiesError, setCapabilitiesError] = useState('');
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
                    setCapabilitiesError('');
                }
            })
            .catch((err: unknown) => {
                if (mounted) {
                    setCapabilitiesError(
                        err instanceof Error ? err.message : '加载媒体能力失败',
                    );
                }
            });
        return () => {
            mounted = false;
        };
    }, []);

    return {
        capabilities,
        capabilitiesError,
        statuses,
        previewUrls,
        refreshStatuses,
    };
}

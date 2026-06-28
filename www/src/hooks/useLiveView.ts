import type { StreamName } from '../api/types';
import { useMediaStreamsInfo } from './useMediaStreamsInfo';

const LIVE_VIEW_CONFIG_TIMEOUT_MS = 3000;
const LIVE_VIEW_STATUS_TIMEOUT_MS = 1800;
const LIVE_VIEW_FAST_REFRESH_INTERVAL_MS = 2000;
const LIVE_VIEW_REFRESH_INTERVAL_MS = 12000;
const LIVE_VIEW_FAST_REFRESH_LIMIT = 4;

export function useLiveView(selectedStream?: StreamName) {
    return useMediaStreamsInfo({
        selectedStream,
        statusTimeoutMs: LIVE_VIEW_STATUS_TIMEOUT_MS,
        previewUrlTimeoutMs: LIVE_VIEW_CONFIG_TIMEOUT_MS,
        fastRefreshIntervalMs: LIVE_VIEW_FAST_REFRESH_INTERVAL_MS,
        fastRefreshLimit: LIVE_VIEW_FAST_REFRESH_LIMIT,
        refreshIntervalMs: LIVE_VIEW_REFRESH_INTERVAL_MS,
        subscribeEvents: true,
    });
}

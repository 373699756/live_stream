import type { StreamName } from '../api/types';
import { useMediaStreamsInfo } from './useMediaStreamsInfo';

const configTimeoutMs = 3000;
const statusTimeoutMs = 1800;
const fastRefreshIntervalMs = 2000;
const steadyRefreshIntervalMs = 12000;
const fastRefreshCount = 4;

export function useLiveView(selectedStream?: StreamName) {
    return useMediaStreamsInfo({
        selectedStream,
        statusTimeoutMs,
        previewUrlTimeoutMs: configTimeoutMs,
        fastRefreshIntervalMs,
        fastRefreshCount,
        refreshIntervalMs: steadyRefreshIntervalMs,
        subscribeEvents: true,
    });
}

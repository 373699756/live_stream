import type { StreamName } from '../api/types';
import { useMediaRuntime } from './useMediaRuntime';

const configTimeoutMs = 3000;
const statusTimeoutMs = 1800;
const fastRefreshIntervalMs = 2000;
const steadyRefreshIntervalMs = 12000;
const fastRefreshCount = 4;

export function useLiveView(selectedStream?: StreamName) {
  return useMediaRuntime({
    selectedStream,
    statusTimeoutMs,
    playbackUrlTimeoutMs: configTimeoutMs,
    fastRefreshIntervalMs,
    fastRefreshCount,
    refreshIntervalMs: steadyRefreshIntervalMs,
    subscribeEvents: true,
  });
}

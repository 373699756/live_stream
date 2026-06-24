import { useEffect, useState } from 'react';
import { getSystemInfo } from '../api/system';
import type { SystemInfo } from '../api/types';

const pollIntervalMs = 2000;
const statusTimeoutMs = 1800;

function errorMessage(error: unknown, fallback: string) {
    return error instanceof Error ? error.message : fallback;
}

export function useSystemInfo() {
    const [status, setStatus] = useState<SystemInfo | null>(null);
    const [refreshError, setRefreshError] = useState('');

    useEffect(() => {
        let mounted = true;
        let timer = 0;
        const load = async () => {
            const startedAt = Date.now();
            try {
                const nextStatus = await getSystemInfo({
                    timeoutMs: statusTimeoutMs,
                });
                if (mounted) {
                    setStatus(nextStatus);
                    setRefreshError('');
                }
            } catch (err: unknown) {
                if (mounted) {
                    setRefreshError(errorMessage(err, '系统状态刷新失败'));
                }
            } finally {
                if (mounted) {
                    const elapsedMs = Date.now() - startedAt;
                    timer = window.setTimeout(
                        load,
                        Math.max(0, pollIntervalMs - elapsedMs),
                    );
                }
            }
        };
        void load();
        return () => {
            mounted = false;
            window.clearTimeout(timer);
        };
    }, []);

    return {
        status,
        refreshError,
    };
}

import { useEffect, useState } from 'react';
import { getSystemInfo } from '../api/system';
import type { SystemInfo } from '../api/types';

const SYSTEM_INFO_REFRESH_INTERVAL_MS = 2000;
const SYSTEM_INFO_REQUEST_TIMEOUT_MS = 1800;

function errorMsg(error: unknown, fallback: string) {
    return error instanceof Error ? error.message : fallback;
}

export function useSystemInfo() {
    const [systemInfo, setSystemInfo] = useState<SystemInfo | null>(null);
    const [refreshError, setRefreshError] = useState('');

    useEffect(() => {
        let mounted = true;
        let timer = 0;
        const load = async () => {
            const startedAt = Date.now();
            try {
                const nextSystemInfo = await getSystemInfo({
                    timeoutMs: SYSTEM_INFO_REQUEST_TIMEOUT_MS,
                });
                if (mounted) {
                    setSystemInfo(nextSystemInfo);
                    setRefreshError('');
                }
            } catch (err: unknown) {
                if (mounted) {
                    setRefreshError(errorMsg(err, '系统状态刷新失败'));
                }
            } finally {
                if (mounted) {
                    const elapsedMs = Date.now() - startedAt;
                    timer = window.setTimeout(
                        load,
                        Math.max(
                            0,
                            SYSTEM_INFO_REFRESH_INTERVAL_MS - elapsedMs,
                        ),
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
        systemInfo,
        refreshError,
    };
}

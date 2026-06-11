/**
 * useVideoConfig — fetch the editable video config first, then refresh media
 * capabilities and stream status as non-blocking preview metadata.
 */

import { useEffect, useState } from 'react';
import { getVideoConfig } from '../api/video';
import type { StreamName, VideoConfig } from '../api/types';
import { usePreviewMetadata } from './usePreviewMetadata';

const configTimeoutMs = 5000;

export function useVideoConfig(selectedStream?: StreamName) {
    const [config, setConfig] = useState<VideoConfig | null>(null);
    const { capabilities, statuses, previewUrls, refreshStatuses } =
        usePreviewMetadata(selectedStream);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState('');

    const reloadConfig = () =>
        getVideoConfig({ timeoutMs: configTimeoutMs })
            .then((nextConfig) => {
                setConfig(nextConfig);
                setError('');
                return nextConfig;
            })
            .catch((err: unknown) => {
                setError(
                    err instanceof Error ? err.message : '加载视频配置失败',
                );
                throw err;
            });

    useEffect(() => {
        let mounted = true;
        setLoading(true);
        void getVideoConfig({ timeoutMs: configTimeoutMs })
            .then((nextConfig) => {
                if (!mounted) return;
                if (nextConfig !== null) {
                    setConfig(nextConfig);
                }
                setError('');
            })
            .catch((err: unknown) => {
                if (mounted) {
                    setError(
                        err instanceof Error ? err.message : '加载视频配置失败',
                    );
                }
            })
            .finally(() => {
                if (mounted) {
                    setLoading(false);
                }
            });
        return () => {
            mounted = false;
        };
    }, []);

    return {
        config,
        setConfig,
        capabilities,
        statuses,
        previewUrls,
        reloadConfig,
        refreshStatuses,
        loading,
        error,
    };
}

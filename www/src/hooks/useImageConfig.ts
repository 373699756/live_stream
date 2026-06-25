/**
 * useImageConfig — fetch image config first; preview capabilities and stream
 * status are loaded independently so the form stays responsive.
 */

import { useEffect, useState } from 'react';
import { getImageConfig, getImageInfo, saveImageConfig } from '../api/image';
import type { ImageConfig, ImageInfo, StreamName } from '../api/types';
import { cloneDefaultConfig } from '../api/configDefaults';
import { mockImageConfig, mockImageInfo } from '../api/mockImage';
import { usePreviewMetadata } from './usePreviewMetadata';

const configTimeoutMs = 5000;
const infoTimeoutMs = 1800;

export function useImageConfig(selectedStream?: StreamName) {
    const [config, setConfig] = useState<ImageConfig | null>(null);
    const { capabilities, statuses, previewUrls } =
        usePreviewMetadata(selectedStream);
    const [imageInfo, setImageInfo] = useState<ImageInfo>(mockImageInfo);
    const [savedMsg, setSavedMsg] = useState('');
    const [loading, setLoading] = useState(true);
    const [saving, setSaving] = useState(false);
    const [error, setError] = useState('');

    useEffect(() => {
        let mounted = true;
        setLoading(true);
        void getImageConfig({ timeoutMs: configTimeoutMs })
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
                        err instanceof Error ? err.message : '加载图像配置失败',
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

    useEffect(() => {
        let mounted = true;
        const refreshStrategy = () => {
            void getImageInfo({ timeoutMs: infoTimeoutMs })
                .then((nextInfo) => {
                    if (mounted) {
                        setImageInfo(nextInfo);
                    }
                })
                .catch(() => {
                    if (mounted) {
                        setImageInfo(mockImageInfo);
                    }
                });
        };
        refreshStrategy();
        const timer = window.setInterval(refreshStrategy, 2000);
        return () => {
            mounted = false;
            window.clearInterval(timer);
        };
    }, []);

    const save = async (nextConfig = config) => {
        if (nextConfig === null) return;
        setSaving(true);
        setError('');
        try {
            await saveImageConfig(nextConfig);
            setConfig(nextConfig);
            setSavedMsg('已提交保存');
        } catch (err: unknown) {
            const msg = err instanceof Error ? err.message : '保存失败';
            setSavedMsg(`保存失败：${msg}`);
            setError(msg);
        } finally {
            setSaving(false);
        }
    };

    const reset = () => {
        setConfig(cloneDefaultConfig(mockImageConfig));
        setSavedMsg('已恢复默认值，保存后生效');
    };

    return {
        config,
        setConfig,
        capabilities,
        statuses,
        previewUrls,
        imageInfo,
        save,
        reset,
        savedMsg,
        loading,
        saving,
        error,
    };
}

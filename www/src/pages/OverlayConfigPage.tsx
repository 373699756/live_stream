import { useState } from 'react';
import type {
    OverlayConfig,
    PrivacyMaskConfig,
    StreamName,
} from '../api/types';
import { resolutionValue } from '../api/resolution';
import { mockOverlayConfig } from '../api/mockOverlay';
import { FormField } from '../components/FormField';
import { VideoPreview } from '../components/VideoPreview';
import { useOverlayConfig } from '../hooks/useOverlayConfig';
import { useVideoConfig } from '../hooks/useVideoConfig';
import { useOverlayMaskEditor } from './OverlayMaskEditor';

const parseResolution = (resolution: string) => {
    const [width, height] = resolution.split('x').map((value) => Number(value));
    if (
        !Number.isFinite(width) ||
        !Number.isFinite(height) ||
        width <= 0 ||
        height <= 0
    ) {
        return { width: 1, height: 1 };
    }
    return { width, height };
};

function cloneMask(mask: PrivacyMaskConfig): PrivacyMaskConfig {
    return { ...mask };
}

function defaultMask(stream: StreamName): PrivacyMaskConfig {
    const defaults = mockOverlayConfig.privacy_masks[stream];
    return cloneMask(defaults[0]);
}

function normalizedMasks(
    masks: PrivacyMaskConfig[] | undefined,
    stream: StreamName,
): PrivacyMaskConfig[] {
    const defaults = mockOverlayConfig.privacy_masks[stream];
    return defaults.map((mask, index) => ({
        ...mask,
        ...(masks?.[index] ?? {}),
    }));
}

function normalizeOverlayConfig(config: OverlayConfig): OverlayConfig {
    return {
        ...config,
        privacy_masks: {
            main: normalizedMasks(config.privacy_masks?.main, 'main'),
            sub: normalizedMasks(config.privacy_masks?.sub, 'sub'),
        },
    };
}

function clampSlot(slot: number, masks: PrivacyMaskConfig[]) {
    return Math.min(Math.max(slot, 0), Math.max(masks.length - 1, 0));
}

function updateMask(
    config: OverlayConfig,
    stream: StreamName,
    slot: number,
    patch: Partial<PrivacyMaskConfig>,
): OverlayConfig {
    const masks = normalizedMasks(config.privacy_masks?.[stream], stream).map(
        (item, index) => (index === slot ? { ...item, ...patch } : item),
    );
    return {
        ...config,
        privacy_masks: {
            ...config.privacy_masks,
            [stream]: masks,
        },
    };
}

export function OverlayConfigPage() {
    const [activeStream, setActiveStream] = useState<StreamName>('sub');
    const [activeSlot, setActiveSlot] = useState(0);
    const { config, setConfig, save, reset, savedMsg, loading, saving, error } =
        useOverlayConfig();
    const {
        config: videoConfig,
        capabilities,
        capabilitiesError,
        statuses,
        previewUrls,
        loading: videoLoading,
    } = useVideoConfig(activeStream);

    const draftConfig = config
        ? normalizeOverlayConfig(config)
        : mockOverlayConfig;
    const streamConfig = videoConfig?.streams[activeStream];
    const capability = capabilities?.streams[activeStream] ?? null;
    const fallbackResolution = capability?.resolutions[0]
        ? resolutionValue(capability.resolutions[0])
        : activeStream === 'main'
          ? '1920x1080'
          : '640x360';
    const frame = parseResolution(
        streamConfig?.resolution || fallbackResolution,
    );
    const activeMasks = draftConfig.privacy_masks[activeStream];
    const safeActiveSlot = clampSlot(activeSlot, activeMasks);
    const activeMask = activeMasks[safeActiveSlot] ?? defaultMask(activeStream);
    const previewStatuses = statuses.map((status) => ({
        ...status,
        resolution:
            videoConfig?.streams[status.stream]?.resolution ||
            status.resolution,
        fps: videoConfig?.streams[status.stream]?.fps || status.fps,
        bitrate_kbps:
            videoConfig?.streams[status.stream]?.bitrate_kbps ||
            status.bitrate_kbps,
    }));

    const setMask = (slot: number, patch: Partial<PrivacyMaskConfig>) => {
        setConfig(updateMask(draftConfig, activeStream, slot, patch));
    };

    const maskEditor = useOverlayMaskEditor({
        activeMask,
        activeMasks,
        activeSlot: safeActiveSlot,
        activeStream,
        frame,
        onClearCurrent: () => setMask(safeActiveSlot, { enabled: false }),
        onMaskPatch: setMask,
        onSlotChange: (slot) => setActiveSlot(clampSlot(slot, activeMasks)),
        onStreamChange: setActiveStream,
        reset,
        save,
        savedMsg,
        saving,
        error,
    });

    if (loading || videoLoading) {
        return <div className="panel">加载 Overlay 配置...</div>;
    }
    if (!config) {
        return (
            <div className="panel">
                Overlay 配置加载失败：{error || '无可用配置'}
            </div>
        );
    }

    return (
        <div className="config-preview-layout overlay-config-layout">
            <section className="panel settings-column">
                <div className="page-heading">
                    <div>
                        <h2>视频叠加</h2>
                        <p>文字叠加与隐私遮挡由设备端区域管线统一应用。</p>
                    </div>
                </div>
                <div className="form-grid">
                    <FormField label="启用文字叠加">
                        <input
                            type="checkbox"
                            checked={draftConfig.enabled}
                            onChange={(e) =>
                                setConfig({
                                    ...draftConfig,
                                    enabled: e.target.checked,
                                })
                            }
                        />
                    </FormField>
                    <FormField label="时间水印">
                        <input
                            type="checkbox"
                            checked={draftConfig.items.timestamp.enabled}
                            onChange={(e) =>
                                setConfig({
                                    ...draftConfig,
                                    items: {
                                        ...draftConfig.items,
                                        timestamp: {
                                            ...draftConfig.items.timestamp,
                                            enabled: e.target.checked,
                                        },
                                    },
                                })
                            }
                        />
                    </FormField>
                    <FormField label="时间格式">
                        <input
                            value={draftConfig.items.timestamp.format}
                            onChange={(e) =>
                                setConfig({
                                    ...draftConfig,
                                    items: {
                                        ...draftConfig.items,
                                        timestamp: {
                                            ...draftConfig.items.timestamp,
                                            format: e.target.value,
                                        },
                                    },
                                })
                            }
                        />
                    </FormField>
                    <FormField label="设备名称">
                        <input
                            value={draftConfig.items.device_name.text}
                            onChange={(e) =>
                                setConfig({
                                    ...draftConfig,
                                    items: {
                                        ...draftConfig.items,
                                        device_name: {
                                            ...draftConfig.items.device_name,
                                            text: e.target.value,
                                        },
                                    },
                                })
                            }
                        />
                    </FormField>
                    <FormField label="字体大小">
                        <input
                            type="number"
                            value={draftConfig.font_size}
                            onChange={(e) =>
                                setConfig({
                                    ...draftConfig,
                                    font_size: Number(e.target.value),
                                })
                            }
                        />
                    </FormField>
                    <FormField label="字体颜色">
                        <input
                            type="color"
                            value={draftConfig.font_color}
                            onChange={(e) =>
                                setConfig({
                                    ...draftConfig,
                                    font_color: e.target.value,
                                })
                            }
                        />
                    </FormField>
                    <FormField label="背景">
                        <input
                            type="checkbox"
                            checked={draftConfig.background}
                            onChange={(e) =>
                                setConfig({
                                    ...draftConfig,
                                    background: e.target.checked,
                                })
                            }
                        />
                    </FormField>
                </div>
                {maskEditor.controls}
                {capabilitiesError && (
                    <div className="status-note error-note">
                        {capabilitiesError}
                    </div>
                )}
            </section>

            <div className="overlay-preview-stack">
                <VideoPreview
                    stream={activeStream}
                    statuses={previewStatuses}
                    previewUrls={previewUrls}
                    onStreamChange={setActiveStream}
                    surfaceOverlay={maskEditor.drawLayer}
                />
            </div>
        </div>
    );
}

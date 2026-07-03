import { useCallback, useEffect, useRef } from 'react';
import type {
    MediaPreviewUrls,
    MediaStreamInfo,
    StreamName,
    WebrtcConfig,
} from '../api/types';
import {
    buildPreviewReadiness,
    previewModeLabels,
    type PreviewMode,
} from './previewMode';
import { usePreviewLiveSession } from './usePreviewLiveSession';

export { previewModeLabels, type PreviewMode } from './previewMode';

interface UsePreviewPlayerOptions {
    active?: MediaStreamInfo;
    enabled: boolean;
    mode: PreviewMode;
    previewUrls: MediaPreviewUrls | null;
    setMode: (mode: PreviewMode) => void;
    stream: StreamName;
    webrtcConfig: WebrtcConfig | null;
}

export function usePreviewPlayer({
    active,
    enabled,
    mode,
    previewUrls,
    setMode,
    stream,
    webrtcConfig,
}: UsePreviewPlayerOptions) {
    const modeSelectionRef = useRef<'auto' | 'manual'>('auto');
    const autoModeSelected = modeSelectionRef.current === 'auto';
    const onAutoModeFallback = useCallback(() => {
        // 自动降级后恢复自动选择，后续就绪状态变化仍可继续切到更优协议。
        modeSelectionRef.current = 'auto';
    }, []);
    const modeState = buildPreviewReadiness(active, mode, previewUrls);
    const {
        connected,
        decodedSize,
        displaySize,
        mediaLayers,
        previewState,
        retainedFrameVisible,
        restartPreview,
        visibleLayer,
    } = usePreviewLiveSession({
        active,
        enabled,
        mode,
        modeState,
        onAutoModeFallback,
        autoModeSelected,
        previewUrls,
        setMode,
        stream,
        webrtcConfig,
    });

    const switchMode = useCallback(
        (nextMode: PreviewMode) => {
            if (nextMode === mode) {
                return;
            }
            // 用户手动点协议后尊重手动选择；只有所选协议不可用时才回到自动模式。
            modeSelectionRef.current = 'manual';
            restartPreview('正在切换预览链路');
            setMode(nextMode);
        },
        [mode, restartPreview, setMode],
    );

    useEffect(() => {
        if (!enabled) {
            return;
        }

        if (modeSelectionRef.current === 'manual') {
            if (!modeState.selectedModeEnabled) {
                // 手动选择的协议失效时不能卡死在不可用模式，回到自动优先级。
                modeSelectionRef.current = 'auto';
                restartPreview(`${previewModeLabels[mode]} 暂不可用`);
                if (modeState.nextAutoMode && modeState.nextAutoMode !== mode) {
                    setMode(modeState.nextAutoMode);
                }
            }
            return;
        }

        if (modeState.nextAutoMode && modeState.nextAutoMode !== mode) {
            // 自动模式始终跟随后端 ready 状态，只在真正可播的链路之间切换。
            restartPreview('正在切换预览链路');
            setMode(modeState.nextAutoMode);
        }
    }, [
        enabled,
        mode,
        modeState.nextAutoMode,
        modeState.selectedModeEnabled,
        restartPreview,
        setMode,
    ]);

    return {
        connected,
        decodedSize,
        displaySize,
        flvPreviewEnabled: modeState.flvPreviewReady,
        flvSupported: modeState.flvSupported,
        hlsModeEnabled: modeState.hlsModeEnabled,
        hlsSupported: modeState.hlsSupported,
        mediaLayers,
        mjpegPreviewEnabled: modeState.mjpegPreviewReady,
        mjpegSupported: modeState.mjpegSupported,
        previewState,
        retainedFrameVisible,
        restartPreview,
        streamRunning: modeState.streamRunning,
        switchMode,
        visibleLayer,
        webrtcEnabled: modeState.webrtcEnabled,
        webrtcPreviewEnabled: modeState.webrtcPreviewReady,
        webrtcSupported: modeState.webrtcSupported,
    };
}

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { MediaPreviewUrls, StreamName, WebrtcConfig } from '../api/types';
import type {
    PreviewLayerMediaKind,
    PreviewMediaLayerRefs,
} from '../components/previewSurfaceTypes';
import {
    clearPreviewImage,
    clearPreviewVideo,
    closePreviewWebrtcSession,
    releasePreviewLayerSession,
    type PreviewLayerSession,
} from './previewLayerSession';
import {
    previewModeLabels,
    type PreviewMode,
    type PreviewReadiness,
} from './previewMode';
import {
    startFlvPreview,
    startHlsPreview,
    startMjpegPreview,
} from './previewLiveProtocols';
import type { FlvPlayer, HlsPlayer } from './previewPlayerModules';
import type { PreviewSessionControls } from './previewSession';
import { startWebrtcPreview } from './webrtcPreviewSession';

interface UsePreviewLiveSessionOptions {
    autoModeSelected: boolean;
    enabled: boolean;
    mode: PreviewMode;
    modeState: PreviewReadiness;
    onAutoModeFallback: () => void;
    previewUrls: MediaPreviewUrls | null;
    setMode: (mode: PreviewMode) => void;
    stream: StreamName;
    webrtcConfig: WebrtcConfig | null;
}

export function usePreviewLiveSession({
    autoModeSelected,
    enabled,
    mode,
    modeState,
    onAutoModeFallback,
    previewUrls,
    setMode,
    stream,
    webrtcConfig,
}: UsePreviewLiveSessionOptions) {
    const [previewState, setPreviewState] = useState('等待 WebRTC 视频流');
    const [connected, setConnected] = useState(false);
    const [decodedSize, setDecodedSize] = useState('');
    const [displaySize, setDisplaySize] = useState('');
    // 目标链路启动期间允许继续显示上一帧，但不能把旧画面当成新链路已连接。
    const [retainedFrameVisible, setRetainedFrameVisible] = useState(false);
    const [visibleLayer, setVisibleLayer] = useState(0);
    const [layerMediaKinds, setLayerMediaKinds] = useState<
        PreviewLayerMediaKind[]
    >(['video', 'video']);
    const layerAImageRef = useRef<HTMLImageElement | null>(null);
    const layerAVideoRef = useRef<HTMLVideoElement | null>(null);
    const layerBImageRef = useRef<HTMLImageElement | null>(null);
    const layerBVideoRef = useRef<HTMLVideoElement | null>(null);
    const sessionRef = useRef(0);
    const visibleLayerRef = useRef(0);
    const activeSessionRef = useRef<PreviewLayerSession | null>(null);
    const retiringSessionsRef = useRef<PreviewLayerSession[]>([]);
    const peerRef = useRef<RTCPeerConnection | null>(null);
    const peerIdRef = useRef('');
    const hlsRef = useRef<HlsPlayer | null>(null);
    const flvRef = useRef<FlvPlayer | null>(null);
    // 固定维护两个媒体层，切流时先在隐藏层启动目标链路，出帧后再切换可见层。
    // 这样能减少黑屏，但也要求连接状态只跟目标会话绑定，不能跟可见旧层绑定。
    const mediaLayers = useMemo<PreviewMediaLayerRefs[]>(
        () => [
            {
                imageRef: layerAImageRef,
                mediaKind: layerMediaKinds[0],
                videoRef: layerAVideoRef,
            },
            {
                imageRef: layerBImageRef,
                mediaKind: layerMediaKinds[1],
                videoRef: layerBVideoRef,
            },
        ],
        [layerMediaKinds],
    );

    const {
        flvModeEnabled,
        flvPreviewReady,
        flvReady,
        hlsModeEnabled,
        hlsReady,
        mjpegPreviewReady,
        mjpegModeEnabled,
        mjpegReady,
        nextReadyMode,
        webrtcEnabled,
        webrtcReady,
    } = modeState;

    const releaseSession = useCallback(
        (session: PreviewLayerSession | null) => {
            releasePreviewLayerSession(session, {
                flvRef,
                hlsRef,
                peerIdRef,
                peerRef,
            });
        },
        [],
    );

    const releaseRetiredSessions = useCallback(() => {
        const retiringSessions = retiringSessionsRef.current;
        retiringSessionsRef.current = [];
        // 已退役层只在新层成功显示后释放，避免切换期间把最后一帧提前清掉。
        for (const session of retiringSessions) {
            releaseSession(session);
        }
    }, [releaseSession]);

    const releaseAllSessions = useCallback(() => {
        const activeSession = activeSessionRef.current;
        activeSessionRef.current = null;
        releaseSession(activeSession);
        releaseRetiredSessions();
    }, [releaseRetiredSessions, releaseSession]);

    const releaseRetainedSessionsBeforeWebrtc = useCallback(() => {
        if (retiringSessionsRef.current.length === 0) {
            return;
        }
        // 后端默认只允许一个 WebRTC 对端，切回 WebRTC 前必须先释放保留层旧连接。
        releaseRetiredSessions();
        setRetainedFrameVisible(false);
    }, [releaseRetiredSessions]);

    const hasVisibleRetiredSession = useCallback(
        () =>
            retiringSessionsRef.current.some(
                (session) =>
                    session.promoted &&
                    session.layerIndex === visibleLayerRef.current,
            ),
        [],
    );

    const restartPreview = useCallback(
        (msg: string) => {
            sessionRef.current += 1;
            setConnected(false);
            // 重启目标链路时先保留旧画面作为过渡，同时让状态浮层继续显示新目标状态。
            setRetainedFrameVisible(
                Boolean(activeSessionRef.current?.promoted) ||
                    hasVisibleRetiredSession(),
            );
            setPreviewState(msg);
        },
        [hasVisibleRetiredSession],
    );

    useEffect(
        () => () => {
            releaseAllSessions();
            clearPreviewVideo(layerAVideoRef.current);
            clearPreviewVideo(layerBVideoRef.current);
            clearPreviewImage(layerAImageRef.current);
            clearPreviewImage(layerBImageRef.current);
        },
        [releaseAllSessions],
    );

    useEffect(() => {
        if (!enabled) {
            sessionRef.current += 1;
            releaseAllSessions();
            clearPreviewVideo(layerAVideoRef.current);
            clearPreviewVideo(layerBVideoRef.current);
            clearPreviewImage(layerAImageRef.current);
            clearPreviewImage(layerBImageRef.current);
            setConnected(false);
            setRetainedFrameVisible(false);
            setDecodedSize('');
            setDisplaySize('');
            setPreviewState('预览已暂停');
            return;
        }

        const sessionId = sessionRef.current + 1;
        sessionRef.current = sessionId;
        // 总是在不可见层准备新目标。只有出帧提升流程才能把它切到前台。
        const layerIndex = visibleLayerRef.current === 0 ? 1 : 0;
        const videoRef = layerIndex === 0 ? layerAVideoRef : layerBVideoRef;
        const imageRef = layerIndex === 0 ? layerAImageRef : layerBImageRef;
        const video = videoRef.current;
        const image = imageRef.current;
        const mediaKind: PreviewLayerMediaKind =
            mode === 'mjpeg' ? 'mjpeg' : 'video';
        const controller = new AbortController();
        const session: PreviewLayerSession = {
            controller,
            flv: null,
            hls: null,
            image,
            layerIndex,
            peer: null,
            peerId: '',
            promoted: false,
            startupTimer: 0,
            video,
        };
        const previousSession = activeSessionRef.current;
        if (previousSession) {
            if (previousSession.promoted) {
                // 已显示的旧会话可能仍在承载画面，等新目标会话出帧后再释放。
                retiringSessionsRef.current.push(previousSession);
            } else {
                // 未出帧的旧目标没有可保留价值，直接释放，避免后台请求继续跑。
                releaseSession(previousSession);
            }
        }
        activeSessionRef.current = session;
        clearPreviewVideo(video);
        clearPreviewImage(image);
        setLayerMediaKinds((currentKinds) => {
            if (currentKinds[layerIndex] === mediaKind) {
                return currentKinds;
            }
            const nextKinds = [...currentKinds];
            nextKinds[layerIndex] = mediaKind;
            return nextKinds;
        });
        setConnected(false);
        // 启动新目标时连接状态必须归零；旧画面只通过保留帧标记表示。
        setRetainedFrameVisible(
            Boolean(previousSession?.promoted) || hasVisibleRetiredSession(),
        );
        setDecodedSize('');
        setDisplaySize('');

        let sessionConnected = false;
        // 所有异步回调都必须经过这个检查，防止旧会话的延迟事件污染当前状态。
        const isCurrentSession = () =>
            !session.controller.signal.aborted &&
            activeSessionRef.current === session &&
            sessionRef.current === sessionId;
        const promoteSession = () => {
            // 只有新目标会话出帧后才算已连接，保留旧画面不能改变连接状态。
            if (!isCurrentSession()) {
                return;
            }
            sessionConnected = true;
            if (session.startupTimer !== 0) {
                window.clearTimeout(session.startupTimer);
                session.startupTimer = 0;
            }
            if (!session.promoted) {
                session.promoted = true;
                visibleLayerRef.current = session.layerIndex;
                setVisibleLayer(session.layerIndex);
                setRetainedFrameVisible(false);
                window.requestAnimationFrame(() => {
                    releaseRetiredSessions();
                });
            }
            setConnected(true);
        };
        const setSessionConnected = (value: boolean) => {
            if (value) {
                promoteSession();
                return;
            }
            sessionConnected = false;
            if (isCurrentSession()) {
                // 失败/断开时保留旧画面可以继续显示，但状态必须明确变为未连接。
                setConnected(false);
                setRetainedFrameVisible(hasVisibleRetiredSession());
            }
        };
        const setSessionPreviewState = (value: string) => {
            if (isCurrentSession()) {
                setPreviewState(value);
            }
        };
        const updateDisplaySize = () => {
            const element = mediaKind === 'mjpeg' ? image : video;
            if (!element) {
                return;
            }
            const rect = element.getBoundingClientRect();
            if (rect.width > 0 && rect.height > 0) {
                setDisplaySize(
                    `${Math.round(rect.width)}x${Math.round(rect.height)}`,
                );
            }
        };
        const controls: PreviewSessionControls = {
            isCurrentSession,
            sessionSignal: controller.signal,
            setConnected: setSessionConnected,
            setDecodedSize,
            setPreviewState: setSessionPreviewState,
            updateDisplaySize,
        };
        const closeWebrtcSession = () => {
            closePreviewWebrtcSession(session, { peerIdRef, peerRef });
        };

        if (mode === 'mjpeg') {
            // MJPEG 使用图片元素，不能和 HLS/FLV/WebRTC 共用视频层。
            if (!image) {
                setSessionPreviewState('MJPEG 预览器不可用');
                return;
            }
            startMjpegPreview({
                controls,
                image,
                mjpegUrl: previewUrls?.mjpeg || '',
                mjpegModeEnabled,
                mjpegReady,
                sessionId,
            });
            return;
        }

        if (!video) {
            setSessionPreviewState('视频预览器不可用');
            return;
        }

        video.onloadeddata = () => {
            promoteSession();
            setSessionPreviewState('视频已连接');
        };
        video.onloadedmetadata = () => {
            if (
                isCurrentSession() &&
                video.videoWidth > 0 &&
                video.videoHeight > 0
            ) {
                setDecodedSize(`${video.videoWidth}x${video.videoHeight}`);
                updateDisplaySize();
            }
        };
        video.onplaying = () => {
            promoteSession();
            setSessionPreviewState('视频已连接');
            updateDisplaySize();
        };
        video.onerror = () => {
            if (!isCurrentSession()) {
                // 旧视频元素在清理 src/load 时也可能补发 error，不能让旧事件触发当前会话降级。
                return;
            }
            setSessionConnected(false);
            if (mode === 'hls') {
                if (
                    autoModeSelected &&
                    nextReadyMode &&
                    nextReadyMode !== 'hls'
                ) {
                    onAutoModeFallback();
                    restartPreview(
                        `HLS 播放失败，切换 ${previewModeLabels[nextReadyMode]}`,
                    );
                    setMode(nextReadyMode);
                    return;
                }
                setSessionPreviewState('HLS 播放失败');
            } else if (mode === 'flv') {
                setSessionPreviewState('HTTP-FLV 播放失败');
            }
        };

        if (mode === 'webrtc') {
            // WebRTC 的对端连接生命周期由当前层会话承载，切流时统一关闭。
            releaseRetainedSessionsBeforeWebrtc();
            startWebrtcPreview({
                controls,
                fallback: {
                    autoModeSelected,
                    flvPreviewReady,
                    isSessionConnected: () => sessionConnected,
                    onAutoModeFallback,
                    restartPreview,
                    setMode,
                },
                peerState: {
                    closeSession: closeWebrtcSession,
                    peerRef,
                    setPeer: (peer) => {
                        session.peer = peer;
                        peerRef.current = peer;
                    },
                    setPeerId: (peerId) => {
                        session.peerId = peerId;
                        peerIdRef.current = peerId;
                    },
                    setStartupTimer: (timer) => {
                        // WebRTC timer 在后端 peer 创建后才注册，因此必须回写到当前层会话。
                        if (session.startupTimer !== 0) {
                            window.clearTimeout(session.startupTimer);
                        }
                        session.startupTimer = timer;
                    },
                    videoRef,
                },
                stream,
                webrtc: {
                    enabled: webrtcEnabled,
                    iceServers: webrtcConfig?.ice_servers ?? [],
                    ready: webrtcReady,
                },
            });
            return;
        }

        if (
            (mode === 'hls' && !hlsModeEnabled) ||
            (mode === 'flv' && !flvModeEnabled)
        ) {
            setSessionPreviewState(
                mode === 'hls' ? 'HLS 码流不可用' : 'HTTP-FLV 码流不可用',
            );
            return;
        }

        if (mode === 'hls') {
            // HLS 允许先拉播放列表触发后端生成，但必须等真实出帧后才切到前台。
            session.startupTimer = startHlsPreview({
                autoFallback: {
                    autoModeSelected,
                    isSessionConnected: () => sessionConnected,
                    nextReadyMode,
                    onAutoModeFallback,
                    restartPreview,
                    setMode,
                },
                controls,
                fallbackReady: {
                    flvPreviewReady,
                    mjpegPreviewReady,
                },
                hlsReady,
                hlsRef,
                hlsUrl: previewUrls?.hls || '',
                sessionId,
                setHlsPlayer: (player) => {
                    session.hls = player;
                    hlsRef.current = player;
                },
                video,
            });
            return;
        }

        // HTTP-FLV 走视频元素和媒体源扩展，播放器实例挂在会话上才能精确销毁。
        startFlvPreview({
            controls,
            flvReady,
            flvRef,
            flvUrl: previewUrls?.http_flv || '',
            sessionId,
            setFlvPlayer: (player) => {
                session.flv = player;
                flvRef.current = player;
            },
            video,
        });
    }, [
        autoModeSelected,
        enabled,
        flvModeEnabled,
        flvPreviewReady,
        flvReady,
        hlsModeEnabled,
        hlsReady,
        mjpegPreviewReady,
        mjpegModeEnabled,
        mjpegReady,
        mode,
        nextReadyMode,
        onAutoModeFallback,
        previewUrls,
        releaseAllSessions,
        releaseRetainedSessionsBeforeWebrtc,
        releaseRetiredSessions,
        releaseSession,
        restartPreview,
        hasVisibleRetiredSession,
        setMode,
        stream,
        webrtcConfig,
        webrtcEnabled,
        webrtcReady,
    ]);

    return {
        connected,
        decodedSize,
        displaySize,
        mediaLayers,
        previewState,
        retainedFrameVisible,
        restartPreview,
        visibleLayer,
    };
}

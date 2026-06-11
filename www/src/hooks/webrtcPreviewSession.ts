import {
    closeWebrtcPeer,
    createWebrtcPeer,
    sendWebrtcCandidate,
    sendWebrtcOffer,
} from '../api/stream';
import type { StreamName, WebrtcConfig } from '../api/types';
import type { PreviewMode } from './previewMode';
import {
    isAbortError,
    type CurrentRef,
    type PreviewSessionControls,
} from './previewSession';

interface WebrtcPreviewConfig {
    enabled: boolean;
    iceServers: WebrtcConfig['ice_servers'];
    ready: boolean;
}

interface WebrtcPreviewFallback {
    autoModeSelected: boolean;
    flvPreviewReady: boolean;
    isSessionConnected: () => boolean;
    onAutoModeFallback: () => void;
    restartPreview: (message: string) => void;
    setMode: (mode: PreviewMode) => void;
}

interface WebrtcPreviewPeerState {
    closeSession: () => void;
    peerRef: CurrentRef<RTCPeerConnection | null>;
    setPeer: (peer: RTCPeerConnection) => void;
    setPeerId: (peerId: string) => void;
    setStartupTimer: (timer: number) => void;
    videoRef: CurrentRef<HTMLVideoElement | null>;
}

interface StartWebrtcPreviewOptions {
    controls: PreviewSessionControls;
    fallback: WebrtcPreviewFallback;
    peerState: WebrtcPreviewPeerState;
    stream: StreamName;
    webrtc: WebrtcPreviewConfig;
}

function rtcIceServers(
    iceServers: WebrtcConfig['ice_servers'],
): RTCIceServer[] {
    return iceServers
        .filter((server) => server.url.trim())
        .map((server) => ({
            credential: server.credential || undefined,
            urls: server.url.trim(),
            username: server.username || undefined,
        }));
}

export function startWebrtcPreview({
    controls,
    fallback,
    peerState,
    stream,
    webrtc,
}: StartWebrtcPreviewOptions) {
    if (!webrtc.enabled) {
        controls.setPreviewState('WebRTC 未启用');
        return;
    }
    if (!webrtc.ready) {
        controls.setPreviewState('WebRTC 暂未就绪');
        return;
    }
    const fallbackFromWebrtcFailure = (message: string) => {
        controls.setConnected(false);
        peerState.closeSession();
        if (fallback.autoModeSelected && fallback.flvPreviewReady) {
            fallback.onAutoModeFallback();
            fallback.restartPreview(`${message}，切换 HTTP-FLV`);
            fallback.setMode('flv');
            return;
        }
        controls.setPreviewState(message);
    };

    controls.setPreviewState('正在创建 WebRTC peer');
    void (async () => {
        try {
            const peer = await createWebrtcPeer(stream, {
                signal: controls.sessionSignal,
            });
            if (!peer.peer_id || !controls.isCurrentSession()) {
                if (peer.peer_id) {
                    void closeWebrtcPeer(peer.peer_id);
                }
                if (controls.isCurrentSession()) {
                    fallbackFromWebrtcFailure('WebRTC peer 创建失败');
                }
                return;
            }

            peerState.setPeerId(peer.peer_id);
            const pc = new RTCPeerConnection({
                bundlePolicy: 'max-bundle',
                iceServers: rtcIceServers(webrtc.iceServers),
                rtcpMuxPolicy: 'require',
            });
            peerState.setPeer(pc);

            // peer 创建成功后才开始计算 WebRTC 启动超时，避免把旧 peer 关闭耗时误判为拉流失败。
            const startupTimer = window.setTimeout(() => {
                if (
                    !controls.isCurrentSession() ||
                    fallback.isSessionConnected()
                ) {
                    return;
                }
                peerState.setStartupTimer(0);
                fallbackFromWebrtcFailure('WebRTC 连接超时');
            }, 2200);
            peerState.setStartupTimer(startupTimer);

            pc.addTransceiver('video', { direction: 'recvonly' });
            pc.ontrack = (event) => {
                if (
                    !controls.isCurrentSession() ||
                    peerState.peerRef.current !== pc ||
                    event.track.kind !== 'video'
                ) {
                    return;
                }
                const mediaStream =
                    event.streams[0] || new MediaStream([event.track]);
                if (peerState.videoRef.current) {
                    peerState.videoRef.current.srcObject = mediaStream;
                    void peerState.videoRef.current.play().catch(() => {});
                    controls.setPreviewState('正在拉取 WebRTC 码流');
                }
            };
            pc.onicecandidate = (event) => {
                if (
                    controls.isCurrentSession() &&
                    peerState.peerRef.current === pc &&
                    event.candidate
                ) {
                    void sendWebrtcCandidate(
                        peer.peer_id,
                        event.candidate.toJSON(),
                        {
                            signal: controls.sessionSignal,
                        },
                    );
                }
            };
            pc.onconnectionstatechange = () => {
                if (
                    !controls.isCurrentSession() ||
                    peerState.peerRef.current !== pc
                ) {
                    return;
                }
                if (pc.connectionState === 'connected') {
                    controls.setPreviewState('WebRTC 已连接');
                } else if (
                    pc.connectionState === 'failed' ||
                    pc.connectionState === 'disconnected' ||
                    pc.connectionState === 'closed'
                ) {
                    fallbackFromWebrtcFailure(
                        pc.connectionState === 'failed'
                            ? 'WebRTC 连接失败'
                            : 'WebRTC 已断开',
                    );
                } else {
                    controls.setPreviewState(`WebRTC ${pc.connectionState}`);
                }
            };
            pc.oniceconnectionstatechange = () => {
                if (
                    !controls.isCurrentSession() ||
                    peerState.peerRef.current !== pc
                ) {
                    return;
                }
                if (
                    pc.iceConnectionState === 'failed' ||
                    pc.iceConnectionState === 'disconnected' ||
                    pc.iceConnectionState === 'closed'
                ) {
                    fallbackFromWebrtcFailure('ICE 连接失败');
                }
            };

            controls.setPreviewState('等待 WebRTC 视频流');
            const offer = await pc.createOffer();
            if (
                !controls.isCurrentSession() ||
                peerState.peerRef.current !== pc
            ) {
                void closeWebrtcPeer(peer.peer_id);
                return;
            }
            await pc.setLocalDescription(offer);
            if (
                !controls.isCurrentSession() ||
                peerState.peerRef.current !== pc
            ) {
                void closeWebrtcPeer(peer.peer_id);
                return;
            }
            const answer = await sendWebrtcOffer(
                peer.peer_id,
                offer.sdp || '',
                {
                    signal: controls.sessionSignal,
                },
            );
            if (
                !answer.sdp ||
                !controls.isCurrentSession() ||
                peerState.peerRef.current !== pc
            ) {
                void closeWebrtcPeer(peer.peer_id);
                if (controls.isCurrentSession()) {
                    fallbackFromWebrtcFailure('WebRTC 应答无效');
                }
                return;
            }
            await pc.setRemoteDescription({ type: 'answer', sdp: answer.sdp });
            if (
                !controls.isCurrentSession() ||
                peerState.peerRef.current !== pc
            ) {
                void closeWebrtcPeer(peer.peer_id);
            }
        } catch (error: unknown) {
            if (isAbortError(error)) {
                return;
            }
            if (controls.isCurrentSession()) {
                fallbackFromWebrtcFailure(
                    error instanceof Error ? error.message : 'WebRTC 连接失败',
                );
            }
        }
    })();
}

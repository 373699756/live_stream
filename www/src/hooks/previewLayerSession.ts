import { closeWebrtcPeer } from '../api/stream';
import {
    destroyFlv,
    destroyHls,
    type FlvPlayer,
    type HlsPlayer,
} from './previewPlayerModules';
import type { CurrentRef } from './previewSession';

export interface PreviewLayerSession {
    controller: AbortController;
    flv: FlvPlayer | null;
    hls: HlsPlayer | null;
    image: HTMLImageElement | null;
    layerIndex: number;
    peer: RTCPeerConnection | null;
    peerId: string;
    promoted: boolean;
    startupTimer: number;
    video: HTMLVideoElement | null;
}

// 这些引用由播放钩子持有，释放会话时只清理仍指向当前资源的全局引用。
interface PreviewPeerRefs {
    peerIdRef: CurrentRef<string>;
    peerRef: CurrentRef<RTCPeerConnection | null>;
}

interface PreviewPlayerRefs extends PreviewPeerRefs {
    flvRef: CurrentRef<FlvPlayer | null>;
    hlsRef: CurrentRef<HlsPlayer | null>;
}

export function stopVideoTracks(video: HTMLMediaElement | null) {
    if (video?.srcObject instanceof MediaStream) {
        for (const track of video.srcObject.getTracks()) {
            track.stop();
        }
    }
}

export function clearPreviewImage(image: HTMLImageElement | null) {
    if (!image) {
        return;
    }
    image.removeAttribute('src');
    image.onload = null;
    image.onerror = null;
}

export function clearPreviewVideo(video: HTMLVideoElement | null) {
    if (!video) {
        return;
    }
    // 清理媒体对象前先停轨道，否则 WebRTC 切换后浏览器仍可能保留解码资源。
    video.pause();
    stopVideoTracks(video);
    video.srcObject = null;
    video.removeAttribute('src');
    video.load();
    video.onloadeddata = null;
    video.onloadedmetadata = null;
    video.onplaying = null;
    video.onerror = null;
}

export function closePreviewWebrtcSession(
    session: PreviewLayerSession,
    refs: PreviewPeerRefs,
) {
    // WebRTC 同时占用浏览器资源和后端对端会话，切协议时必须一起关闭。
    if (session.peer) {
        session.peer.onicecandidate = null;
        session.peer.ontrack = null;
        session.peer.onconnectionstatechange = null;
        session.peer.oniceconnectionstatechange = null;
        session.peer.close();
        if (refs.peerRef.current === session.peer) {
            refs.peerRef.current = null;
        }
        session.peer = null;
    }
    if (session.peerId) {
        void closeWebrtcPeer(session.peerId);
        if (refs.peerIdRef.current === session.peerId) {
            refs.peerIdRef.current = '';
        }
        session.peerId = '';
    }
    stopVideoTracks(session.video);
    if (session.video) {
        session.video.srcObject = null;
    }
}

export function releasePreviewLayerSession(
    session: PreviewLayerSession | null,
    refs: PreviewPlayerRefs,
) {
    if (!session) {
        return;
    }
    // 先释放本层绑定的协议资源，再清页面节点，避免延迟事件回写旧目标状态。
    session.controller.abort();
    if (session.startupTimer !== 0) {
        window.clearTimeout(session.startupTimer);
        session.startupTimer = 0;
    }
    // HLS/FLV 播放器内部会持有媒体源和事件监听，必须显式销毁。
    destroyHls(session.hls);
    destroyFlv(session.flv);
    closePreviewWebrtcSession(session, refs);
    if (refs.hlsRef.current === session.hls) {
        refs.hlsRef.current = null;
    }
    if (refs.flvRef.current === session.flv) {
        refs.flvRef.current = null;
    }
    session.hls = null;
    session.flv = null;
    clearPreviewVideo(session.video);
    clearPreviewImage(session.image);
}

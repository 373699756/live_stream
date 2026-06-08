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
  session.controller.abort();
  if (session.startupTimer !== 0) {
    window.clearTimeout(session.startupTimer);
    session.startupTimer = 0;
  }
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

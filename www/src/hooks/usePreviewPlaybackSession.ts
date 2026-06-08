import {
  type RefObject,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';
import { closeWebrtcPeer } from '../api/stream';
import type { MediaPlaybackUrls, StreamName } from '../api/types';
import type { PreviewMode, PreviewModeState } from './previewMode';
import {
  startFlvPreview,
  startHlsPreview,
  startMjpegPreview,
} from './previewPlaybackProtocols';
import {
  destroyFlv,
  destroyHls,
  type FlvPlayer,
  type HlsPlayer,
} from './previewPlayerModules';
import type { PreviewSessionControls } from './previewSession';
import { startWebrtcPreview } from './webrtcPreviewSession';

interface UsePreviewPlaybackSessionOptions {
  autoModeSelected: boolean;
  enabled: boolean;
  mode: PreviewMode;
  modeState: PreviewModeState;
  onAutoModeFallback: () => void;
  playbackUrls: MediaPlaybackUrls | null;
  setMode: (mode: PreviewMode) => void;
  stream: StreamName;
}

export type PreviewLayerMediaKind = 'video' | 'mjpeg';

export interface PreviewMediaLayerRefs {
  imageRef: RefObject<HTMLImageElement | null>;
  mediaKind: PreviewLayerMediaKind;
  videoRef: RefObject<HTMLVideoElement | null>;
}

interface PreviewLayerSession {
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

function stopVideoTracks(video: HTMLMediaElement | null) {
  if (video?.srcObject instanceof MediaStream) {
    for (const track of video.srcObject.getTracks()) {
      track.stop();
    }
  }
}

function clearImageElement(image: HTMLImageElement | null) {
  if (!image) {
    return;
  }
  image.removeAttribute('src');
  image.onload = null;
  image.onerror = null;
}

function clearVideoElement(video: HTMLVideoElement | null) {
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

export function usePreviewPlaybackSession({
  autoModeSelected,
  enabled,
  mode,
  modeState,
  onAutoModeFallback,
  playbackUrls,
  setMode,
  stream,
}: UsePreviewPlaybackSessionOptions) {
  const [previewState, setPreviewState] = useState('等待 WebRTC 视频流');
  const [connected, setConnected] = useState(false);
  const [decodedSize, setDecodedSize] = useState('');
  const [displaySize, setDisplaySize] = useState('');
  const [visibleLayer, setVisibleLayer] = useState(0);
  const [layerMediaKinds, setLayerMediaKinds] =
    useState<PreviewLayerMediaKind[]>(['video', 'video']);
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
  const mediaLayers = useMemo<PreviewMediaLayerRefs[]>(() => [
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
  ], [layerMediaKinds]);

  const {
    flvModeEnabled,
    flvPlaybackReady,
    flvReady,
    hlsModeEnabled,
    hlsReady,
    mjpegPlaybackReady,
    mjpegModeEnabled,
    mjpegReady,
    nextReadyMode,
    webrtcEnabled,
    webrtcReady,
  } = modeState;

  const releaseSession = useCallback((session: PreviewLayerSession | null) => {
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
    if (session.peer) {
      session.peer.onicecandidate = null;
      session.peer.ontrack = null;
      session.peer.onconnectionstatechange = null;
      session.peer.oniceconnectionstatechange = null;
      session.peer.close();
      if (peerRef.current === session.peer) {
        peerRef.current = null;
      }
    }
    if (session.peerId) {
      void closeWebrtcPeer(session.peerId);
      if (peerIdRef.current === session.peerId) {
        peerIdRef.current = '';
      }
    }
    if (hlsRef.current === session.hls) {
      hlsRef.current = null;
    }
    if (flvRef.current === session.flv) {
      flvRef.current = null;
    }
    clearVideoElement(session.video);
    clearImageElement(session.image);
  }, []);

  const releaseRetiredSessions = useCallback(() => {
    const retiringSessions = retiringSessionsRef.current;
    retiringSessionsRef.current = [];
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

  const hasVisibleRetiredSession = useCallback(() => (
    retiringSessionsRef.current.some((session) => (
      session.promoted && session.layerIndex === visibleLayerRef.current
    ))
  ), []);

  const restartPreview = useCallback((message: string) => {
    sessionRef.current += 1;
    if (!activeSessionRef.current?.promoted && !hasVisibleRetiredSession()) {
      setConnected(false);
    }
    setPreviewState(message);
  }, [hasVisibleRetiredSession]);

  useEffect(() => () => {
    releaseAllSessions();
    clearVideoElement(layerAVideoRef.current);
    clearVideoElement(layerBVideoRef.current);
    clearImageElement(layerAImageRef.current);
    clearImageElement(layerBImageRef.current);
  }, [releaseAllSessions]);

  useEffect(() => {
    if (!enabled) {
      sessionRef.current += 1;
      releaseAllSessions();
      clearVideoElement(layerAVideoRef.current);
      clearVideoElement(layerBVideoRef.current);
      clearImageElement(layerAImageRef.current);
      clearImageElement(layerBImageRef.current);
      setConnected(false);
      setDecodedSize('');
      setDisplaySize('');
      setPreviewState('预览已暂停');
      return;
    }

    const sessionId = sessionRef.current + 1;
    sessionRef.current = sessionId;
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
        retiringSessionsRef.current.push(previousSession);
      } else {
        releaseSession(previousSession);
      }
    }
    activeSessionRef.current = session;
    clearVideoElement(video);
    clearImageElement(image);
    setLayerMediaKinds((currentKinds) => {
      if (currentKinds[layerIndex] === mediaKind) {
        return currentKinds;
      }
      const nextKinds = [...currentKinds];
      nextKinds[layerIndex] = mediaKind;
      return nextKinds;
    });
    setConnected(
      previousSession?.promoted || hasVisibleRetiredSession() ? true : false,
    );
    setDecodedSize('');
    setDisplaySize('');

    let sessionConnected = false;
    const isCurrentSession = () =>
      !session.controller.signal.aborted &&
      activeSessionRef.current === session &&
      sessionRef.current === sessionId;
    const promoteSession = () => {
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
        setConnected(hasVisibleRetiredSession() ? true : false);
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
        setDisplaySize(`${Math.round(rect.width)}x${Math.round(rect.height)}`);
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
      if (session.peer) {
        session.peer.onicecandidate = null;
        session.peer.ontrack = null;
        session.peer.onconnectionstatechange = null;
        session.peer.oniceconnectionstatechange = null;
        session.peer.close();
        if (peerRef.current === session.peer) {
          peerRef.current = null;
        }
        session.peer = null;
      }
      if (session.peerId) {
        void closeWebrtcPeer(session.peerId);
        if (peerIdRef.current === session.peerId) {
          peerIdRef.current = '';
        }
        session.peerId = '';
      }
      stopVideoTracks(video);
      if (video) {
        video.srcObject = null;
      }
    };

    if (mode === 'mjpeg') {
      if (!image) {
        setSessionPreviewState('MJPEG 预览器不可用');
        return;
      }
      startMjpegPreview({
        controls,
        image,
        mjpegUrl: playbackUrls?.mjpeg || '',
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
      if (isCurrentSession() && video.videoWidth > 0 && video.videoHeight > 0) {
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
      setSessionConnected(false);
      if (mode === 'hls') {
        setSessionPreviewState('HLS 播放失败');
      } else if (mode === 'flv') {
        setSessionPreviewState('HTTP-FLV 播放失败');
      }
    };

    if (mode === 'webrtc') {
      session.startupTimer = startWebrtcPreview({
        controls,
        fallback: {
          flvPlaybackReady,
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
          videoRef,
        },
        stream,
        webrtc: {
          enabled: webrtcEnabled,
          ready: webrtcReady,
        },
      });
      return;
    }

    if ((mode === 'hls' && !hlsModeEnabled) ||
        (mode === 'flv' && !flvModeEnabled)) {
      setSessionPreviewState(
        mode === 'hls' ? 'HLS 码流不可用' : 'HTTP-FLV 码流不可用',
      );
      return;
    }

    if (mode === 'hls') {
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
          flvPlaybackReady,
          mjpegPlaybackReady,
        },
        hlsReady,
        hlsRef,
        hlsUrl: playbackUrls?.hls || '',
        setHlsPlayer: (player) => {
          session.hls = player;
          hlsRef.current = player;
        },
        video,
      });
      return;
    }

    startFlvPreview({
      controls,
      flvReady,
      flvRef,
      flvUrl: playbackUrls?.http_flv || '',
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
    flvPlaybackReady,
    flvReady,
    hlsModeEnabled,
    hlsReady,
    mjpegPlaybackReady,
    mjpegModeEnabled,
    mjpegReady,
    mode,
    nextReadyMode,
    onAutoModeFallback,
    playbackUrls,
    releaseAllSessions,
    releaseRetiredSessions,
    releaseSession,
    restartPreview,
    hasVisibleRetiredSession,
    setMode,
    stream,
    webrtcEnabled,
    webrtcReady,
  ]);

  return {
    connected,
    decodedSize,
    displaySize,
    mediaLayers,
    previewState,
    restartPreview,
    visibleLayer,
  };
}

import { useCallback, useEffect, useRef, useState } from 'react';
import { closeWebrtcPeer } from '../api/stream';
import type { StreamName, WebrtcConfig } from '../api/types';
import type { PreviewMode, PreviewModeState } from './previewMode';
import {
  startFlvPreview,
  startHlsPreview,
  startMjpegPreview,
  startWebrtcPreview,
  type PreviewSessionControls,
} from './previewPlaybackProtocols';
import {
  destroyFlv,
  destroyHls,
  type FlvPlayer,
  type HlsPlayer,
} from './previewPlayerModules';

interface UsePreviewPlaybackSessionOptions {
  enabled: boolean;
  mode: PreviewMode;
  modeState: PreviewModeState;
  onAutoModeFallback: () => void;
  setMode: (mode: PreviewMode) => void;
  stream: StreamName;
  webrtcConfig: WebrtcConfig | null;
  webrtcConfigError: string;
  webrtcConfigLoaded: boolean;
}

function stopVideoTracks(video: HTMLMediaElement | null) {
  if (video?.srcObject instanceof MediaStream) {
    for (const track of video.srcObject.getTracks()) {
      track.stop();
    }
  }
}

export function usePreviewPlaybackSession({
  enabled,
  mode,
  modeState,
  onAutoModeFallback,
  setMode,
  stream,
  webrtcConfig,
  webrtcConfigError,
  webrtcConfigLoaded,
}: UsePreviewPlaybackSessionOptions) {
  const [previewState, setPreviewState] = useState('等待 WebRTC 视频流');
  const [connected, setConnected] = useState(false);
  const [decodedSize, setDecodedSize] = useState('');
  const [displaySize, setDisplaySize] = useState('');
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const imageRef = useRef<HTMLImageElement | null>(null);
  const sessionRef = useRef(0);
  const peerRef = useRef<RTCPeerConnection | null>(null);
  const peerIdRef = useRef('');
  const hlsRef = useRef<HlsPlayer | null>(null);
  const flvRef = useRef<FlvPlayer | null>(null);

  const {
    flvModeEnabled,
    flvPlaybackReady,
    flvReady,
    hlsModeEnabled,
    hlsReady,
    mjpegModeEnabled,
    mjpegReady,
    webrtcEnabled,
    webrtcReady,
  } = modeState;
  const webrtcIceServerKey = (webrtcConfig?.ice_servers || [])
    .map((server) => [
      server.url,
      server.username || '',
      server.credential || '',
    ].join(','))
    .join('|');

  const restartPreview = useCallback((message: string) => {
    sessionRef.current += 1;
    setConnected(false);
    setPreviewState(message);
  }, []);

  useEffect(() => {
    const video = videoRef.current;
    const image = imageRef.current;
    const sessionId = sessionRef.current + 1;
    sessionRef.current = sessionId;
    let disposed = false;
    let sessionPeer: RTCPeerConnection | null = null;
    let sessionPeerId = '';
    let sessionHls: HlsPlayer | null = null;
    let sessionFlv: FlvPlayer | null = null;
    let sessionConnected = false;
    let startupTimer = 0;
    const controller = new AbortController();
    const sessionSignal = controller.signal;

    const isCurrentSession = () =>
      !disposed && !sessionSignal.aborted && sessionRef.current === sessionId;
    const setSessionConnected = (value: boolean) => {
      sessionConnected = value;
      if (value && startupTimer !== 0) {
        window.clearTimeout(startupTimer);
        startupTimer = 0;
      }
      if (isCurrentSession()) {
        setConnected(value);
      }
    };
    const setSessionPreviewState = (value: string) => {
      if (isCurrentSession()) {
        setPreviewState(value);
      }
    };
    const updateDisplaySize = () => {
      const element = mode === 'mjpeg' ? image : video;
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
      sessionSignal,
      setConnected: setSessionConnected,
      setDecodedSize,
      setPreviewState: setSessionPreviewState,
      updateDisplaySize,
    };
    const resetImageElement = () => {
      if (!image) {
        return;
      }
      image.removeAttribute('src');
      image.onload = null;
      image.onerror = null;
      setDecodedSize('');
      setDisplaySize('');
    };
    const resetVideoElement = () => {
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
      setDecodedSize('');
      setDisplaySize('');
    };
    const closePeer = (peer: RTCPeerConnection | null, peerId: string) => {
      if (peer) {
        peer.onicecandidate = null;
        peer.ontrack = null;
        peer.onconnectionstatechange = null;
        peer.oniceconnectionstatechange = null;
        peer.close();
        if (peerRef.current === peer) {
          peerRef.current = null;
        }
      }
      if (peerId) {
        void closeWebrtcPeer(peerId);
        if (peerIdRef.current === peerId) {
          peerIdRef.current = '';
        }
      }
    };
    const closeWebrtcSession = () => {
      closePeer(sessionPeer, sessionPeerId);
      stopVideoTracks(video);
      if (video) {
        video.srcObject = null;
      }
      sessionPeer = null;
      sessionPeerId = '';
    };
    const cleanupSession = () => {
      disposed = true;
      controller.abort();
      if (startupTimer !== 0) {
        window.clearTimeout(startupTimer);
      }
      destroyHls(sessionHls);
      destroyFlv(sessionFlv);
      closePeer(sessionPeer, sessionPeerId);
      if (hlsRef.current === sessionHls) {
        hlsRef.current = null;
      }
      if (flvRef.current === sessionFlv) {
        flvRef.current = null;
      }
      if (sessionRef.current === sessionId) {
        resetVideoElement();
        resetImageElement();
        setConnected(false);
      }
      sessionHls = null;
      sessionFlv = null;
      sessionPeer = null;
      sessionPeerId = '';
    };

    destroyHls(hlsRef.current);
    destroyFlv(flvRef.current);
    closePeer(peerRef.current, peerIdRef.current);
    resetVideoElement();
    resetImageElement();
    setSessionConnected(false);

    if (!enabled) {
      setSessionPreviewState('预览已暂停');
      return cleanupSession;
    }
    if (!video) {
      if (mode === 'mjpeg' && image) {
        startMjpegPreview({
          controls,
          image,
          mjpegModeEnabled,
          mjpegReady,
          sessionId,
          stream,
        });
        return cleanupSession;
      }
      setSessionPreviewState('视频预览器不可用');
      return cleanupSession;
    }

    video.onloadeddata = () => {
      setSessionConnected(true);
      setSessionPreviewState('视频已连接');
    };
    video.onloadedmetadata = () => {
      if (isCurrentSession() && video.videoWidth > 0 && video.videoHeight > 0) {
        setDecodedSize(`${video.videoWidth}x${video.videoHeight}`);
        updateDisplaySize();
      }
    };
    video.onplaying = () => {
      setSessionConnected(true);
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
      startupTimer = startWebrtcPreview({
        closeWebrtcSession,
        controls,
        flvPlaybackReady,
        isSessionConnected: () => sessionConnected,
        onAutoModeFallback,
        peerRef,
        restartPreview,
        setMode,
        setPeer: (peer) => {
          sessionPeer = peer;
          peerRef.current = peer;
        },
        setPeerId: (peerId) => {
          sessionPeerId = peerId;
          peerIdRef.current = peerId;
        },
        stream,
        videoRef,
        webrtcConfig,
        webrtcConfigError,
        webrtcConfigLoaded,
        webrtcEnabled,
        webrtcReady,
      });

      return cleanupSession;
    }

    if ((mode === 'hls' && !hlsModeEnabled) ||
        (mode === 'flv' && !flvModeEnabled)) {
      setSessionPreviewState(
        mode === 'hls' ? 'HLS 码流不可用' : 'HTTP-FLV 码流不可用',
      );
      return cleanupSession;
    }

    if (mode === 'hls') {
      startHlsPreview({
        controls,
        hlsReady,
        hlsRef,
        setHlsPlayer: (player) => {
          sessionHls = player;
          hlsRef.current = player;
        },
        stream,
        video,
      });
      return cleanupSession;
    }

    startFlvPreview({
      controls,
      flvReady,
      flvRef,
      sessionId,
      setFlvPlayer: (player) => {
        sessionFlv = player;
        flvRef.current = player;
      },
      stream,
      video,
    });
    return cleanupSession;
  }, [
    enabled,
    flvModeEnabled,
    flvPlaybackReady,
    flvReady,
    hlsModeEnabled,
    hlsReady,
    mjpegModeEnabled,
    mjpegReady,
    mode,
    onAutoModeFallback,
    setMode,
    stream,
    webrtcConfig,
    webrtcConfigError,
    webrtcConfigLoaded,
    webrtcEnabled,
    webrtcIceServerKey,
    webrtcReady,
  ]);

  return {
    connected,
    decodedSize,
    displaySize,
    imageRef,
    previewState,
    restartPreview,
    videoRef,
  };
}

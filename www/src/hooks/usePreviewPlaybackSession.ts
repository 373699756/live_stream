import { useCallback, useEffect, useRef, useState } from 'react';
import { flvStreamUrl, hlsPlaylistUrl, mjpegStreamUrl } from '../api/client';
import {
  closeWebrtcPeer,
  createWebrtcPeer,
  sendWebrtcCandidate,
  sendWebrtcOffer,
} from '../api/stream';
import type { StreamName, WebrtcConfig } from '../api/types';
import type { PreviewMode, PreviewModeState } from './previewMode';
import {
  destroyFlv,
  destroyHls,
  loadLocalFlvModule,
  loadLocalHlsModule,
  PlayerModuleUnavailableError,
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

const webrtcStartupTimeoutMs = 3500;

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
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
        if (!mjpegModeEnabled) {
          setSessionPreviewState('MJPEG 码流不可用');
          return cleanupSession;
        }
        if (!mjpegReady) {
          setSessionPreviewState('正在等待 MJPEG 首帧');
          return cleanupSession;
        }
        image.onload = () => {
          if (isCurrentSession()) {
            setSessionConnected(true);
            setSessionPreviewState('MJPEG 已连接');
            if (image.naturalWidth > 0 && image.naturalHeight > 0) {
              setDecodedSize(`${image.naturalWidth}x${image.naturalHeight}`);
            }
            updateDisplaySize();
          }
        };
        image.onerror = () => {
          setSessionConnected(false);
          setSessionPreviewState('MJPEG 播放失败');
        };
        const baseMjpegUrl = mjpegStreamUrl(stream);
        const mjpegUrl =
          `${baseMjpegUrl}${baseMjpegUrl.includes('?') ? '&' : '?'}session=${sessionId}`;
        setSessionPreviewState('正在拉取 MJPEG 码流');
        image.src = mjpegUrl;
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
      if (!webrtcConfigLoaded) {
        setSessionPreviewState('正在读取 WebRTC 配置');
        return cleanupSession;
      }
      if (webrtcConfigError) {
        setSessionPreviewState(webrtcConfigError);
        return cleanupSession;
      }
      if (!webrtcEnabled) {
        setSessionPreviewState('WebRTC 未启用');
        return cleanupSession;
      }
      if (!webrtcReady) {
        setSessionPreviewState('WebRTC 暂未就绪');
        return cleanupSession;
      }
      setSessionPreviewState('等待 WebRTC 视频流');
      startupTimer = window.setTimeout(() => {
        if (!isCurrentSession() || sessionConnected) {
          return;
        }
        if (flvPlaybackReady) {
          onAutoModeFallback();
          restartPreview('WebRTC 连接超时，切换 HTTP-FLV');
          setMode('flv');
          return;
        }
        setSessionPreviewState('WebRTC 连接超时');
        closeWebrtcSession();
      }, webrtcStartupTimeoutMs);
      const pc = new RTCPeerConnection({
        bundlePolicy: 'max-bundle',
        rtcpMuxPolicy: 'require',
        iceServers: (webrtcConfig?.ice_servers || []).map((server) => ({
          urls: server.url,
          username: server.username,
          credential: server.credential,
        })),
      });
      sessionPeer = pc;
      peerRef.current = pc;
      pc.addTransceiver('video', { direction: 'recvonly' });
      pc.ontrack = (event) => {
        if (
          !isCurrentSession() ||
          peerRef.current !== pc ||
          event.track.kind !== 'video'
        ) {
          return;
        }
        const mediaStream =
          event.streams[0] || new MediaStream([event.track]);
        if (videoRef.current) {
          videoRef.current.srcObject = mediaStream;
          void videoRef.current.play().catch(() => {});
          setSessionConnected(true);
          setSessionPreviewState('视频已连接');
        }
      };
      pc.onicecandidate = (event) => {
        if (
          isCurrentSession() &&
          peerRef.current === pc &&
          event.candidate &&
          sessionPeerId
        ) {
          void sendWebrtcCandidate(sessionPeerId, event.candidate.toJSON(), {
            signal: sessionSignal,
          });
        }
      };
      pc.onconnectionstatechange = () => {
        if (!isCurrentSession() || peerRef.current !== pc) {
          return;
        }
        if (pc.connectionState === 'connected') {
          if (startupTimer !== 0) {
            window.clearTimeout(startupTimer);
            startupTimer = 0;
          }
          setSessionConnected(true);
          setSessionPreviewState('WebRTC 已连接');
        } else if (
          pc.connectionState === 'failed' ||
          pc.connectionState === 'disconnected' ||
          pc.connectionState === 'closed'
        ) {
          setSessionConnected(false);
          setSessionPreviewState(
            pc.connectionState === 'failed' ? 'WebRTC 连接失败' : 'WebRTC 已断开',
          );
          closeWebrtcSession();
        } else {
          setSessionPreviewState(`WebRTC ${pc.connectionState}`);
        }
      };
      pc.oniceconnectionstatechange = () => {
        if (!isCurrentSession() || peerRef.current !== pc) {
          return;
        }
        if (
          pc.iceConnectionState === 'failed' ||
          pc.iceConnectionState === 'disconnected' ||
          pc.iceConnectionState === 'closed'
        ) {
          setSessionConnected(false);
          setSessionPreviewState('ICE 连接失败');
          closeWebrtcSession();
        }
      };

      void (async () => {
        try {
          const peer = await createWebrtcPeer(stream, { signal: sessionSignal });
          if (!peer.peer_id || !isCurrentSession() || peerRef.current !== pc) {
            if (peer.peer_id) {
              void closeWebrtcPeer(peer.peer_id);
            }
            if (isCurrentSession()) {
              setSessionPreviewState('WebRTC 后端不可用');
              closeWebrtcSession();
            }
            return;
          }
          sessionPeerId = peer.peer_id;
          peerIdRef.current = peer.peer_id;
          const offer = await pc.createOffer();
          if (!isCurrentSession() || peerRef.current !== pc) {
            void closeWebrtcPeer(peer.peer_id);
            return;
          }
          await pc.setLocalDescription(offer);
          if (!isCurrentSession() || peerRef.current !== pc) {
            void closeWebrtcPeer(peer.peer_id);
            return;
          }
          const answer = await sendWebrtcOffer(peer.peer_id, offer.sdp || '', {
            signal: sessionSignal,
          });
          if (!answer.sdp || !isCurrentSession() || peerRef.current !== pc) {
            void closeWebrtcPeer(peer.peer_id);
            if (isCurrentSession()) {
              setSessionPreviewState('WebRTC 应答无效');
              closeWebrtcSession();
            }
            return;
          }
          await pc.setRemoteDescription({ type: 'answer', sdp: answer.sdp });
          if (!isCurrentSession() || peerRef.current !== pc) {
            void closeWebrtcPeer(peer.peer_id);
          }
        } catch (error: unknown) {
          if (isAbortError(error)) {
            return;
          }
          if (isCurrentSession()) {
            setSessionPreviewState('WebRTC 连接失败');
            closeWebrtcSession();
          }
        }
      })();

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
      const url = hlsPlaylistUrl(stream);
      if (!hlsReady) {
        setSessionPreviewState('正在启动 HLS 码流');
        void fetch(url, {
          cache: 'no-store',
          signal: sessionSignal,
        }).catch((error: unknown) => {
          if (!isAbortError(error) && isCurrentSession()) {
            setSessionPreviewState('HLS 启动失败');
          }
        });
        return cleanupSession;
      }
      setSessionPreviewState('等待 HLS 视频流');
      if (video.canPlayType('application/vnd.apple.mpegurl')) {
        video.src = url;
        void video.play().catch(() => {});
        setSessionPreviewState('正在拉取 HLS 码流');
        return cleanupSession;
      }
      void (async () => {
        try {
          const Hls = await loadLocalHlsModule();
          if (!isCurrentSession()) {
            return;
          }
          if (!Hls.isSupported?.()) {
            setSessionPreviewState('HLS 播放器不可用');
            return;
          }
          const player = new Hls({
            backBufferLength: 3,
            enableWorker: true,
            liveDurationInfinity: true,
            liveMaxLatencyDurationCount: 3,
            liveSyncDurationCount: 1,
            lowLatencyMode: true,
            maxLiveSyncPlaybackRate: 1.5,
          });
          sessionHls = player;
          hlsRef.current = player;
          const errorEvent = Hls.Events?.ERROR;
          if (errorEvent && player.on) {
            player.on(errorEvent, () => {
              if (isCurrentSession() && hlsRef.current === player) {
                setSessionPreviewState('HLS 播放失败');
              }
            });
          }
          player.attachMedia(video);
          player.loadSource(url);
          void video.play().catch(() => {});
          setSessionPreviewState('正在拉取 HLS 码流');
        } catch (error) {
          if (isCurrentSession()) {
            setSessionPreviewState(
              error instanceof PlayerModuleUnavailableError
                ? 'HLS 播放器初始化失败'
                : 'HLS 播放器脚本加载失败',
            );
          }
        }
      })();
      return cleanupSession;
    }

    if (!flvReady) {
      setSessionPreviewState('正在等待 HTTP-FLV 首帧');
      return cleanupSession;
    }
    setSessionPreviewState('等待 HTTP-FLV 视频流');
    void (async () => {
      try {
        const flvModule = await loadLocalFlvModule();
        if (!isCurrentSession()) {
          return;
        }
        const flvSupported = flvModule.isSupported?.() ?? true;
        const liveSupported =
          flvModule.getFeatureList?.().mseLiveFlvPlayback ?? flvSupported;
        if (!flvSupported || !liveSupported) {
          setSessionPreviewState('HTTP-FLV 播放器不可用');
          return;
        }
        const baseFlvUrl = flvStreamUrl(stream);
        const flvUrl =
          `${baseFlvUrl}${baseFlvUrl.includes('?') ? '&' : '?'}session=${sessionId}`;
        const player = flvModule.createPlayer({
          type: 'flv',
          isLive: true,
          url: flvUrl,
          hasAudio: false,
          hasVideo: true,
        }, {
          enableWorker: false,
          enableStashBuffer: false,
          stashInitialSize: 128,
          lazyLoad: false,
          deferLoadAfterSourceOpen: false,
          autoCleanupSourceBuffer: true,
          autoCleanupMaxBackwardDuration: 4,
          autoCleanupMinBackwardDuration: 1,
        });
        sessionFlv = player;
        flvRef.current = player;
        const errorEvent = flvModule.Events?.ERROR;
        if (errorEvent && player.on) {
          player.on(errorEvent, (...args: unknown[]) => {
            if (isCurrentSession() && flvRef.current === player) {
              const details = args
                .map((item) => (
                  typeof item === 'string'
                    ? item
                    : item instanceof Error
                      ? item.message
                      : JSON.stringify(item)
                ))
                .filter(Boolean)
                .join(' ');
              console.error('HTTP-FLV player error', ...args);
              setSessionPreviewState(
                details ? `HTTP-FLV 播放失败：${details}` : 'HTTP-FLV 播放失败',
              );
            }
          });
        }
        player.attachMediaElement(video);
        player.load();
        void player.play().catch(() => {});
        setSessionPreviewState('正在拉取 HTTP-FLV 码流');
      } catch (error) {
        if (isCurrentSession()) {
          setSessionPreviewState(
            error instanceof PlayerModuleUnavailableError
              ? 'HTTP-FLV 播放器初始化失败'
              : 'HTTP-FLV 播放器脚本加载失败',
          );
        }
      }
    })();
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

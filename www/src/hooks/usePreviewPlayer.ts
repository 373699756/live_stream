import { useEffect, useRef, useState } from 'react';
import { flvStreamUrl, hlsPlaylistUrl } from '../api/client';
import {
  closeWebrtcPeer,
  createWebrtcPeer,
  getWebrtcConfig,
  sendWebrtcCandidate,
  sendWebrtcOffer,
} from '../api/stream';
import type { StreamName, StreamStatus, WebrtcConfig } from '../api/types';

export type PreviewMode = 'webrtc' | 'hls' | 'flv';

export const previewModeLabels: Record<PreviewMode, string> = {
  webrtc: 'WebRTC',
  hls: 'HLS',
  flv: 'HTTP-FLV',
};

type HlsPlayer = InstanceType<NonNullable<Window['Hls']>>;
type HlsConstructor = NonNullable<Window['Hls']>;
type FlvModule = NonNullable<Window['mpegts']>;
type FlvPlayer = ReturnType<FlvModule['createPlayer']>;

class PlayerScriptLoadError extends Error {
  constructor(src: string) {
    super(`load failed: ${src}`);
    this.name = 'PlayerScriptLoadError';
  }
}

class PlayerModuleUnavailableError extends Error {
  constructor(name: string) {
    super(`${name} module unavailable`);
    this.name = 'PlayerModuleUnavailableError';
  }
}

interface UsePreviewPlayerOptions {
  active?: StreamStatus;
  enabled: boolean;
  mode: PreviewMode;
  setMode: (mode: PreviewMode) => void;
  stream: StreamName;
}

const scriptLoads = new Map<string, Promise<void>>();

function loadScriptOnce(src: string): Promise<void> {
  const existingLoad = scriptLoads.get(src);
  if (existingLoad) {
    return existingLoad;
  }
  const load = new Promise<void>((resolve, reject) => {
    const existing = document.querySelector<HTMLScriptElement>(
      `script[src="${src}"]`,
    );
    if (existing?.dataset.loaded === 'true') {
      resolve();
      return;
    }
    const script = existing || document.createElement('script');
    script.src = src;
    script.async = true;
    script.onload = () => {
      script.dataset.loaded = 'true';
      resolve();
    };
    script.onerror = () => {
      script.remove();
      reject(new PlayerScriptLoadError(src));
    };
    if (!existing) {
      document.head.appendChild(script);
    }
  });
  load.catch(() => {
    scriptLoads.delete(src);
  });
  scriptLoads.set(src, load);
  return load;
}

async function loadLocalHlsModule(): Promise<HlsConstructor> {
  if (window.Hls) {
    return window.Hls;
  }
  await loadScriptOnce('/vendor/hls.min.js');
  if (!window.Hls) {
    throw new PlayerModuleUnavailableError('HLS');
  }
  return window.Hls;
}

async function loadLocalFlvModule(): Promise<FlvModule> {
  const current = window.mpegts || window.flvjs;
  if (current) {
    return current;
  }
  await loadScriptOnce('/vendor/flv.min.js');
  const module = window.mpegts || window.flvjs;
  if (!module) {
    throw new PlayerModuleUnavailableError('HTTP-FLV');
  }
  return module;
}

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

function destroyHls(player: HlsPlayer | null) {
  if (!player) {
    return;
  }
  try {
    player.destroy();
  } catch {
    // Best-effort cleanup.
  }
}

function destroyFlv(player: FlvPlayer | null) {
  if (!player) {
    return;
  }
  try {
    player.unload?.();
    player.detachMediaElement?.();
    player.destroy();
  } catch {
    // Best-effort cleanup.
  }
}

export function usePreviewPlayer({
  active,
  enabled,
  mode,
  setMode,
  stream,
}: UsePreviewPlayerOptions) {
  const [webrtcConfig, setWebrtcConfig] = useState<WebrtcConfig | null>(null);
  const [webrtcConfigLoaded, setWebrtcConfigLoaded] = useState(false);
  const [previewState, setPreviewState] = useState('等待 WebRTC 视频流');
  const [connected, setConnected] = useState(false);
  const [decodedSize, setDecodedSize] = useState('');
  const [displaySize, setDisplaySize] = useState('');
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const sessionRef = useRef(0);
  const peerRef = useRef<RTCPeerConnection | null>(null);
  const peerIdRef = useRef('');
  const hlsRef = useRef<HlsPlayer | null>(null);
  const flvRef = useRef<FlvPlayer | null>(null);
  const modeSelectionRef = useRef<'auto' | 'manual'>('auto');

  const hlsReady = active?.hlsReady ?? false;
  const flvReady = active?.flvReady ?? false;
  const webrtcReady = active?.webrtcReady ?? false;
  const hlsSupported = active?.hlsSupported ?? false;
  const flvSupported = active?.flvSupported ?? false;
  const webrtcSupported = webrtcReady;
  const webrtcEnabled = Boolean(webrtcConfig?.enabled);
  const streamRunning = active?.state === 'running';
  const webrtcModeEnabled =
    webrtcConfigLoaded && webrtcEnabled && webrtcSupported;
  const hlsModeEnabled = hlsSupported && streamRunning;
  const flvModeEnabled = flvSupported && streamRunning;
  const webrtcPlaybackReady = webrtcModeEnabled && webrtcReady;
  const hlsPlaybackReady = hlsModeEnabled && hlsReady;
  const flvPlaybackReady = flvModeEnabled && flvReady;
  const webrtcIceServerKey = (webrtcConfig?.ice_servers || [])
    .map((server) => [
      server.url,
      server.username || '',
      server.credential || '',
    ].join(','))
    .join('|');

  const restartPreview = (message: string) => {
    sessionRef.current += 1;
    setConnected(false);
    setPreviewState(message);
  };

  const switchMode = (nextMode: PreviewMode) => {
    if (nextMode === mode) {
      return;
    }
    modeSelectionRef.current = 'manual';
    restartPreview('正在切换预览链路');
    setMode(nextMode);
  };

  useEffect(() => {
    let mounted = true;
    const controller = new AbortController();
    void getWebrtcConfig({ signal: controller.signal })
      .then((config) => {
        if (mounted) {
          setWebrtcConfig(config);
          setWebrtcConfigLoaded(true);
          if (!config.enabled) {
            setPreviewState('WebRTC 未启用');
          }
        }
      })
      .catch((error: unknown) => {
        if (isAbortError(error)) {
          return;
        }
        if (mounted) {
          setWebrtcConfig(null);
          setWebrtcConfigLoaded(true);
          setPreviewState('WebRTC 配置不可用');
        }
      });
    return () => {
      mounted = false;
      controller.abort();
    };
  }, []);

  useEffect(() => {
    if (!enabled || (mode === 'webrtc' && !webrtcConfigLoaded)) {
      return;
    }

    const selectedModeEnabled =
      (mode === 'webrtc' && webrtcModeEnabled) ||
      (mode === 'hls' && hlsModeEnabled) ||
      (mode === 'flv' && flvModeEnabled);
    const nextReadyMode =
      webrtcPlaybackReady ? 'webrtc' :
      flvPlaybackReady ? 'flv' :
      hlsPlaybackReady ? 'hls' :
      null;

    if (modeSelectionRef.current === 'manual') {
      if (!selectedModeEnabled) {
        modeSelectionRef.current = 'auto';
        restartPreview(`${previewModeLabels[mode]} 暂不可用`);
        if (nextReadyMode && nextReadyMode !== mode) {
          setMode(nextReadyMode);
        }
      }
      return;
    }

    if (nextReadyMode && nextReadyMode !== mode) {
      restartPreview('正在切换预览链路');
      setMode(nextReadyMode);
    }
  }, [
    enabled,
    flvModeEnabled,
    flvPlaybackReady,
    hlsModeEnabled,
    hlsPlaybackReady,
    mode,
    setMode,
    webrtcModeEnabled,
    webrtcPlaybackReady,
  ]);

  useEffect(() => {
    const video = videoRef.current;
    const sessionId = sessionRef.current + 1;
    sessionRef.current = sessionId;
    let disposed = false;
    let sessionPeer: RTCPeerConnection | null = null;
    let sessionPeerId = '';
    let sessionHls: HlsPlayer | null = null;
    let sessionFlv: FlvPlayer | null = null;
    const controller = new AbortController();
    const sessionSignal = controller.signal;

    const isCurrentSession = () =>
      !disposed && !sessionSignal.aborted && sessionRef.current === sessionId;
    const setSessionConnected = (value: boolean) => {
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
      if (!video) {
        return;
      }
      const rect = video.getBoundingClientRect();
      if (rect.width > 0 && rect.height > 0) {
        setDisplaySize(`${Math.round(rect.width)}x${Math.round(rect.height)}`);
      }
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
    setSessionConnected(false);

    if (!enabled) {
      setSessionPreviewState('预览已暂停');
      return cleanupSession;
    }
    if (!video) {
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
      if (!webrtcEnabled) {
        setSessionPreviewState('WebRTC 未启用');
        return cleanupSession;
      }
      if (!webrtcReady) {
        setSessionPreviewState('WebRTC 暂未就绪');
        return cleanupSession;
      }
      setSessionPreviewState('等待 WebRTC 视频流');
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
      if (!hlsReady) {
        setSessionPreviewState('正在等待 HLS 首帧');
        return cleanupSession;
      }
      setSessionPreviewState('等待 HLS 视频流');
      const url = hlsPlaylistUrl(stream);
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
          const player = new Hls({ enableWorker: true });
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
    setSessionPreviewState(
      '等待 HTTP-FLV 视频流',
    );
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
          autoCleanupMaxBackwardDuration: 8,
          autoCleanupMinBackwardDuration: 2,
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
    flvReady,
    hlsModeEnabled,
    hlsReady,
    mode,
    stream,
    webrtcConfig,
    webrtcConfigLoaded,
    webrtcEnabled,
    webrtcIceServerKey,
    webrtcReady,
  ]);

  return {
    connected,
    decodedSize,
    displaySize,
    flvPlaybackEnabled: flvPlaybackReady,
    flvSupported,
    hlsPlaybackEnabled: hlsPlaybackReady,
    hlsSupported,
    previewState,
    restartPreview,
    streamRunning,
    switchMode,
    videoRef,
    webrtcEnabled,
    webrtcPlaybackEnabled: webrtcPlaybackReady,
    webrtcSupported,
  };
}

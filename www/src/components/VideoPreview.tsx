import { useEffect, useRef, useState } from 'react';
import {
  closeWebrtcPeer,
  createWebrtcPeer,
  sendWebrtcCandidate,
  sendWebrtcOffer,
  getWebrtcConfig,
} from '../api/stream';
import {
  flvStreamUrl,
  hlsPlaylistUrl,
} from '../api/client';
import type { StreamName, StreamStatus, WebrtcConfig } from '../api/types';
import { StatusBadge } from './StatusBadge';

type PreviewMode = 'webrtc' | 'hls' | 'flv';

const previewModeLabels: Record<PreviewMode, string> = {
  webrtc: 'WebRTC',
  hls: 'HLS',
  flv: 'HTTP-FLV',
};

type HlsPlayer = {
  attachMedia: (media: HTMLMediaElement) => void;
  destroy: () => void;
  loadSource: (url: string) => void;
  on?: (event: string, listener: (...args: unknown[]) => void) => void;
};

type HlsConstructor = {
  new (config?: Record<string, unknown>): HlsPlayer;
  isSupported?: () => boolean;
  Events?: Record<string, string>;
};

type FlvPlayer = {
  attachMediaElement: (media: HTMLMediaElement) => void;
  destroy: () => void;
  detachMediaElement?: () => void;
  load: () => void;
  on?: (event: string, listener: (...args: unknown[]) => void) => void;
  play: () => Promise<void>;
  unload?: () => void;
};

type FlvModule = {
  Events?: Record<string, string>;
  createPlayer: (
    config: Record<string, unknown>,
    options?: Record<string, unknown>,
  ) => FlvPlayer;
  getFeatureList?: () => { mseLiveFlvPlayback?: boolean };
  isSupported?: () => boolean;
};

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

interface VideoPreviewProps {
  stream: StreamName;
  statuses: StreamStatus[];
  onStreamChange: (stream: StreamName) => void;
  enabled?: boolean;
  onSnapshot?: (stream: StreamName) => void;
}

const scriptLoads = new Map<string, Promise<void>>();

function loadScriptOnce(src: string): Promise<void> {
  const existingLoad = scriptLoads.get(src);
  if (existingLoad) {
    return existingLoad;
  }
  const load = new Promise<void>((resolve, reject) => {
    const existing = document.querySelector<HTMLScriptElement>(`script[src="${src}"]`);
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

async function loadLocalHlsModule(): Promise<HlsConstructor | undefined> {
  if (window.Hls) {
    return window.Hls as HlsConstructor;
  }
  await loadScriptOnce('/vendor/hls.min.js');
  const module = window.Hls as HlsConstructor | undefined;
  if (!module) {
    throw new PlayerModuleUnavailableError('HLS');
  }
  return module;
}

async function loadLocalFlvModule(): Promise<FlvModule | undefined> {
  const current = window.mpegts || window.flvjs;
  if (current) {
    return current as FlvModule;
  }
  await loadScriptOnce('/vendor/flv.min.js');
  const module = (window.mpegts || window.flvjs) as FlvModule | undefined;
  if (!module) {
    throw new PlayerModuleUnavailableError('HTTP-FLV');
  }
  return module;
}

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

export function VideoPreview({
  stream,
  statuses,
  onStreamChange,
  enabled = true,
  onSnapshot,
}: VideoPreviewProps) {
  const [mode, setMode] = useState<PreviewMode>('webrtc');
  const [webrtcConfig, setWebrtcConfig] = useState<WebrtcConfig | null>(null);
  const [webrtcConfigLoaded, setWebrtcConfigLoaded] = useState(false);
  const [previewState, setPreviewState] = useState('等待 WebRTC 视频流');
  const [connected, setConnected] = useState(false);
  const [decodedSize, setDecodedSize] = useState('');
  const [displaySize, setDisplaySize] = useState('');
  const surfaceRef = useRef<HTMLDivElement | null>(null);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const sessionRef = useRef(0);
  const peerRef = useRef<RTCPeerConnection | null>(null);
  const peerIdRef = useRef('');
  const hlsRef = useRef<HlsPlayer | null>(null);
  const flvRef = useRef<FlvPlayer | null>(null);
  const modeSelectionRef = useRef<'auto' | 'manual'>('auto');
  const readinessRef = useRef({
    flvPlaybackEnabled: false,
    hlsPlaybackEnabled: false,
    webrtcPlaybackEnabled: false,
    webrtcConfigLoaded: false,
    webrtcEnabled: false,
  });
  const active = statuses.find((item) => item.stream === stream);
  const activeCodec = (active?.codec || '').toLowerCase().replace(/[^a-z0-9]/g, '');
  const hlsReady = active?.hlsReady ?? false;
  const flvReady = active?.flvReady ?? false;
  const webrtcReady = active?.webrtcReady ?? false;
  const browserCodec = active?.browserCodec ?? (
    activeCodec === '' || activeCodec === 'h264' || activeCodec === 'h265'
  );
  const flvCodec = activeCodec === 'h264';
  const switchStream = (nextStream: StreamName) => {
    if (nextStream === stream) {
      return;
    }
    sessionRef.current += 1;
    setConnected(false);
    setPreviewState('正在切换码流');
    onStreamChange(nextStream);
  };
  const switchMode = (nextMode: PreviewMode) => {
    if (nextMode === mode) {
      return;
    }
    modeSelectionRef.current = 'manual';
    sessionRef.current += 1;
    setConnected(false);
    setPreviewState('正在切换预览链路');
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

  const webrtcEnabled = Boolean(webrtcConfig?.enabled);
  const webrtcPlaybackEnabled =
    webrtcEnabled && webrtcReady && activeCodec === 'h264';
  const streamRunning = active?.state === 'running';
  const hlsPlaybackEnabled = browserCodec && streamRunning;
  const flvPlaybackEnabled = flvCodec && streamRunning;
  const webrtcIceServerKey = (webrtcConfig?.ice_servers || [])
    .map((server) => [
      server.url,
      server.username || '',
      server.credential || '',
    ].join(','))
    .join('|');

  readinessRef.current = {
    flvPlaybackEnabled,
    hlsPlaybackEnabled,
    webrtcPlaybackEnabled,
    webrtcConfigLoaded,
    webrtcEnabled,
  };

  const isModeAvailable = (candidate: PreviewMode) => {
    if (candidate === 'webrtc') {
      return webrtcConfigLoaded && webrtcPlaybackEnabled;
    }
    if (candidate === 'hls') {
      return hlsPlaybackEnabled;
    }
    return flvPlaybackEnabled;
  };

  const chooseAvailableMode = () => {
    if (webrtcPlaybackEnabled) {
      return 'webrtc';
    }
    if (flvPlaybackEnabled) {
      return 'flv';
    }
    if (hlsPlaybackEnabled) {
      return 'hls';
    }
    return 'webrtc';
  };

  useEffect(() => {
    if (!enabled || (mode === 'webrtc' && !webrtcConfigLoaded)) {
      return;
    }

    const modeAvailable = isModeAvailable(mode);
    const nextMode = chooseAvailableMode();
    if (!modeAvailable) {
      if (nextMode !== mode) {
        modeSelectionRef.current = 'auto';
        sessionRef.current += 1;
        setConnected(false);
        setPreviewState(`${previewModeLabels[mode]} 暂不可用`);
        setMode(nextMode);
      }
      return;
    }

    if (modeSelectionRef.current === 'auto' && nextMode !== mode) {
      sessionRef.current += 1;
      setConnected(false);
      setPreviewState('正在切换预览链路');
      setMode(nextMode);
    }
  }, [
    mode,
    enabled,
    flvPlaybackEnabled,
    hlsPlaybackEnabled,
    webrtcConfigLoaded,
    webrtcPlaybackEnabled,
  ]);

  useEffect(() => {
    const video = videoRef.current;
    const sessionId = sessionRef.current + 1;
    sessionRef.current = sessionId;
    const readiness = readinessRef.current;
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
    const stopVideoTracks = (target: HTMLMediaElement | null) => {
      if (target?.srcObject instanceof MediaStream) {
        for (const track of target.srcObject.getTracks()) {
          track.stop();
        }
      }
    };
    const resetVideoElement = (target: HTMLMediaElement | null) => {
      if (!target) {
        return;
      }
      target.pause();
      stopVideoTracks(target);
      target.srcObject = null;
      target.removeAttribute('src');
      target.load();
      target.onloadeddata = null;
      target.onloadedmetadata = null;
      target.onplaying = null;
      target.onerror = null;
      setDecodedSize('');
      setDisplaySize('');
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
    const destroyHls = (player: HlsPlayer | null) => {
      if (!player) {
        return;
      }
      try {
        player.destroy();
      } catch {
        // Best-effort cleanup.
      }
      if (hlsRef.current === player) {
        hlsRef.current = null;
      }
    };
    const destroyFlv = (player: FlvPlayer | null) => {
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
      if (flvRef.current === player) {
        flvRef.current = null;
      }
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
    const stopPreviousLinks = () => {
      destroyHls(hlsRef.current);
      destroyFlv(flvRef.current);
      closePeer(peerRef.current, peerIdRef.current);
      resetVideoElement(video);
      setSessionConnected(false);
    };
    const cleanupSession = () => {
      disposed = true;
      controller.abort();
      destroyHls(sessionHls);
      destroyFlv(sessionFlv);
      closePeer(sessionPeer, sessionPeerId);
      if (sessionRef.current === sessionId) {
        resetVideoElement(video);
        setConnected(false);
      }
      sessionHls = null;
      sessionFlv = null;
      sessionPeer = null;
      sessionPeerId = '';
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

    stopPreviousLinks();

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
      if (!readiness.webrtcConfigLoaded) {
        setSessionPreviewState('正在读取 WebRTC 配置');
        return cleanupSession;
      }
      if (!readiness.webrtcEnabled) {
        setSessionPreviewState('WebRTC 未启用');
        return cleanupSession;
      }
      if (!readiness.webrtcPlaybackEnabled) {
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
        const media_stream =
          event.streams[0] || new MediaStream([event.track]);
        if (videoRef.current) {
          videoRef.current.srcObject = media_stream;
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

    if ((mode === 'hls' && !readiness.hlsPlaybackEnabled) ||
        (mode === 'flv' && !readiness.flvPlaybackEnabled)) {
      setSessionPreviewState(
        mode === 'hls' ? 'HLS 码流不可用' : 'HTTP-FLV 码流不可用',
      );
      return cleanupSession;
    }

    if (mode === 'hls') {
      setSessionPreviewState(hlsReady ? '等待 HLS 视频流' : '正在等待 HLS 首帧');
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
          if (!Hls || !Hls.isSupported?.()) {
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

    setSessionPreviewState(
      flvReady ? '等待 HTTP-FLV 视频流' : '正在等待 HTTP-FLV 首帧',
    );
    void (async () => {
      try {
        const flvModule = await loadLocalFlvModule();
        if (!isCurrentSession()) {
          return;
        }
        const flv_supported = flvModule?.isSupported?.() ?? true;
        const live_supported =
          flvModule?.getFeatureList?.().mseLiveFlvPlayback ?? flv_supported;
        if (!flvModule || !flv_supported || !live_supported) {
          setSessionPreviewState('HTTP-FLV 播放器不可用');
          return;
        }
        const baseFlvUrl = flvStreamUrl(stream);
        const flvUrl = `${baseFlvUrl}${baseFlvUrl.includes('?') ? '&' : '?'}session=${sessionId}`;
        const player = flvModule.createPlayer({
          type: 'flv',
          isLive: true,
          url: flvUrl,
          hasAudio: false,
          hasVideo: true,
        }, {
          enableWorker: false,
          enableStashBuffer: true,
          stashInitialSize: 384,
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
    mode,
    enabled,
    stream,
    activeCodec,
    flvReady,
    hlsReady,
    hlsPlaybackEnabled,
    flvPlaybackEnabled,
    webrtcConfigLoaded,
    webrtcEnabled,
    webrtcIceServerKey,
  ]);

  const toggleFullscreen = () => {
    const surface = surfaceRef.current;
    if (!surface) {
      return;
    }
    if (document.fullscreenElement === surface) {
      void document.exitFullscreen?.();
      return;
    }
    void surface.requestFullscreen?.();
  };

  const streamLabel = stream === 'main' ? '主码流' : '子码流';
  const previewDetail =
    mode === 'webrtc'
      ? `${streamLabel} / WebRTC`
      : mode === 'hls'
        ? `${streamLabel} / HLS`
        : mode === 'flv'
          ? `${streamLabel} / HTTP-FLV`
          : `${streamLabel} / WebRTC`;
  const protocolLabel = previewModeLabels[mode];
  const streamSummary = (name: StreamName) => {
    const item = statuses.find((status) => status.stream === name);
    const running = item?.state === 'running';
    return {
      label: name === 'main' ? '主码流' : '子码流',
      running,
      state: running ? '运行中' : '未运行',
      detail: `${item?.codec || 'H.264'} / ${item?.resolution || '--'} / ${item?.fps || 0}fps`,
    };
  };
  const mainSummary = streamSummary('main');
  const subSummary = streamSummary('sub');

  return (
    <section className="preview-panel">
      <div className="preview-toolbar">
        <div className="stream-switcher">
          <button
            type="button"
            className={stream === 'main' ? 'active' : ''}
            onClick={() => switchStream('main')}
          >
            <strong>{mainSummary.label}</strong>
            <span className={mainSummary.running ? 'running' : ''}>{mainSummary.state}</span>
            <em>{mainSummary.detail}</em>
          </button>
          <button
            type="button"
            className={stream === 'sub' ? 'active' : ''}
            onClick={() => switchStream('sub')}
          >
            <strong>{subSummary.label}</strong>
            <span className={subSummary.running ? 'running' : ''}>{subSummary.state}</span>
            <em>{subSummary.detail}</em>
          </button>
        </div>
        <div className="preview-actions">
          <button
            type="button"
            className={mode === 'webrtc' ? 'active' : ''}
            disabled={!webrtcEnabled || !webrtcPlaybackEnabled}
            onClick={() => switchMode('webrtc')}
          >
            WebRTC
          </button>
          <button
            type="button"
            className={mode === 'hls' ? 'active' : ''}
            disabled={!hlsPlaybackEnabled}
            onClick={() => switchMode('hls')}
          >
            HLS
          </button>
          <button
            type="button"
            className={mode === 'flv' ? 'active' : ''}
            disabled={!flvPlaybackEnabled}
            onClick={() => switchMode('flv')}
          >
            HTTP-FLV
          </button>
          {onSnapshot && (
            <button
              type="button"
              disabled={!streamRunning}
              onClick={() => onSnapshot(stream)}
            >
              抓图
            </button>
          )}
          <button type="button" onClick={toggleFullscreen}>全屏</button>
        </div>
      </div>

      <div className="video-surface" ref={surfaceRef} onDoubleClick={toggleFullscreen}>
        {!enabled ? (
          <div className="video-placeholder">
            <div className="lens-ring paused" />
            <strong>预览已暂停</strong>
            <span>正在应用视频参数</span>
          </div>
        ) : (
          <video ref={videoRef} className="video-element" autoPlay muted playsInline />
        )}
        {!connected && (
          <div className="video-placeholder">
            <div className="lens-ring" />
            <strong>{previewState}</strong>
            <span>{previewDetail}</span>
          </div>
        )}
      </div>

      <div className="preview-footer">
        <StatusBadge state={active?.state === 'running' ? 'running' : 'pending'} />
        <span>{streamLabel}</span>
        <span>{protocolLabel}</span>
        <span>{active?.codec || 'H.264'}</span>
        <span>分辨率 {active?.resolution || '1920x1080'}</span>
        {decodedSize && <span>实际 {decodedSize}</span>}
        {displaySize && <span>显示 {displaySize}</span>}
        <span>{active?.fps || 25} fps</span>
        <span>{active?.bitrateKbps || 12288} kbps</span>
      </div>
    </section>
  );
}

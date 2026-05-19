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
  snapshotUrl as buildSnapshotUrl,
} from '../api/client';
import type { StreamName, StreamStatus, WebrtcConfig } from '../api/types';
import { StatusBadge } from './StatusBadge';

type PreviewMode = 'webrtc' | 'hls' | 'flv' | 'snapshot';

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

interface VideoPreviewProps {
  stream: StreamName;
  statuses: StreamStatus[];
  onStreamChange: (stream: StreamName) => void;
  enabled?: boolean;
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
      reject(new Error(`load failed: ${src}`));
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
  return window.Hls as HlsConstructor | undefined;
}

async function loadLocalFlvModule(): Promise<FlvModule | undefined> {
  const current = window.mpegts || window.flvjs;
  if (current) {
    return current as FlvModule;
  }
  await loadScriptOnce('/vendor/flv.min.js');
  return (window.mpegts || window.flvjs) as FlvModule | undefined;
}

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

export function VideoPreview({
  stream,
  statuses,
  onStreamChange,
  enabled = true,
}: VideoPreviewProps) {
  const [mode, setMode] = useState<PreviewMode>('webrtc');
  const [snapshotTick, setSnapshotTick] = useState(0);
  const [webrtcConfig, setWebrtcConfig] = useState<WebrtcConfig | null>(null);
  const [webrtcConfigLoaded, setWebrtcConfigLoaded] = useState(false);
  const [previewState, setPreviewState] = useState('等待 WebRTC 视频流');
  const [connected, setConnected] = useState(false);
  const surfaceRef = useRef<HTMLDivElement | null>(null);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const sessionRef = useRef(0);
  const peerRef = useRef<RTCPeerConnection | null>(null);
  const peerIdRef = useRef('');
  const hlsRef = useRef<HlsPlayer | null>(null);
  const flvRef = useRef<FlvPlayer | null>(null);
  const active = statuses.find((item) => item.stream === stream);
  const snapshotUrl = buildSnapshotUrl(stream, snapshotTick);
  const activeCodec = (active?.codec || '').toLowerCase().replace(/[^a-z0-9]/g, '');

  useEffect(() => {
    let mounted = true;
    const controller = new AbortController();
    void getWebrtcConfig({ signal: controller.signal })
      .then((config) => {
        if (mounted) {
          setWebrtcConfig(config);
          setWebrtcConfigLoaded(true);
          if (!config.enabled) {
            setMode((current) => (current === 'webrtc' ? 'flv' : current));
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
          setMode((current) => (current === 'webrtc' ? 'flv' : current));
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
    activeCodec === '' || activeCodec === 'h264';
  const streamingPlaybackEnabled =
    activeCodec === '' || activeCodec === 'h264' || activeCodec === 'h265';

  useEffect(() => {
    if (mode !== 'snapshot') {
      return;
    }
    const timer = window.setInterval(
      () => setSnapshotTick((value) => value + 1),
      2000,
    );
    return () => window.clearInterval(timer);
  }, [mode]);

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
      target.onplaying = null;
      target.onerror = null;
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
    if (mode === 'snapshot') {
      setSessionPreviewState('抓图预览');
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
    video.onplaying = () => {
      setSessionConnected(true);
      setSessionPreviewState('视频已连接');
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
      if (!webrtcEnabled || !webrtcPlaybackEnabled) {
        setMode(streamingPlaybackEnabled ? 'flv' : 'snapshot');
        setSessionPreviewState('WebRTC 未启用');
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

    if (!streamingPlaybackEnabled) {
      setMode('snapshot');
      setSessionPreviewState(
        mode === 'hls' ? 'HLS 不支持当前编码' : 'HTTP-FLV 不支持当前编码',
      );
      return cleanupSession;
    }

    if (mode === 'hls') {
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
        } catch {
          if (isCurrentSession()) {
            setSessionPreviewState('HLS 播放器脚本未加载');
          }
        }
      })();
      return cleanupSession;
    }

    setSessionPreviewState('等待 HTTP-FLV 视频流');
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
        const player = flvModule.createPlayer({
          type: 'flv',
          isLive: true,
          url: flvStreamUrl(stream),
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
          player.on(errorEvent, () => {
            if (isCurrentSession() && flvRef.current === player) {
              setSessionPreviewState('HTTP-FLV 播放失败');
            }
          });
        }
        player.attachMediaElement(video);
        player.load();
        void player.play().catch(() => {});
        setSessionPreviewState('正在拉取 HTTP-FLV 码流');
      } catch {
        if (isCurrentSession()) {
          setSessionPreviewState('HTTP-FLV 播放器脚本未加载');
        }
      }
    })();
    return cleanupSession;
  }, [
    mode,
    enabled,
    stream,
    streamingPlaybackEnabled,
    webrtcConfig,
    webrtcConfigLoaded,
    webrtcEnabled,
    webrtcPlaybackEnabled,
  ]);

  const openSnapshot = () => {
    window.open(buildSnapshotUrl(stream), '_blank', 'noopener,noreferrer');
  };

  const requestFullscreen = () => {
    void surfaceRef.current?.requestFullscreen?.();
  };

  const previewDetail =
    mode === 'webrtc'
      ? '正在建立浏览器拉流会话'
      : mode === 'hls'
        ? '浏览器正在拉取 HLS 分片'
        : mode === 'flv'
          ? '浏览器正在接收 HTTP-FLV 视频流'
          : '定时刷新 JPEG 抓图';

  return (
    <section className="preview-panel">
      <div className="preview-toolbar">
        <div className="segmented">
          <button
            type="button"
            className={stream === 'main' ? 'active' : ''}
            onClick={() => onStreamChange('main')}
          >
            主码流
          </button>
          <button
            type="button"
            className={stream === 'sub' ? 'active' : ''}
            onClick={() => onStreamChange('sub')}
          >
            子码流
          </button>
        </div>
        <div className="preview-actions">
          <button
            type="button"
            className={mode === 'webrtc' ? 'active' : ''}
            disabled={!webrtcEnabled || !webrtcPlaybackEnabled}
            onClick={() => setMode('webrtc')}
          >
            WebRTC
          </button>
          <button
            type="button"
            className={mode === 'hls' ? 'active' : ''}
            disabled={!streamingPlaybackEnabled}
            onClick={() => setMode('hls')}
          >
            HLS
          </button>
          <button
            type="button"
            className={mode === 'flv' ? 'active' : ''}
            disabled={!streamingPlaybackEnabled}
            onClick={() => setMode('flv')}
          >
            HTTP-FLV
          </button>
          <button
            type="button"
            className={mode === 'snapshot' ? 'active' : ''}
            onClick={() => setMode('snapshot')}
          >
            抓图预览
          </button>
          <button type="button" onClick={openSnapshot}>截图</button>
          <button type="button" onClick={requestFullscreen}>全屏</button>
        </div>
      </div>

      <div className="video-surface" ref={surfaceRef}>
        {!enabled ? (
          <div className="video-placeholder">
            <div className="lens-ring paused" />
            <strong>预览已暂停</strong>
            <span>正在应用视频参数</span>
          </div>
        ) : mode === 'snapshot' ? (
          <img
            className="snapshot-preview"
            src={snapshotUrl}
            alt="snapshot preview"
            onLoad={(event) => {
              event.currentTarget.style.opacity = '1';
              setConnected(true);
            }}
            onError={(event) => {
              event.currentTarget.style.opacity = '0';
              setConnected(false);
            }}
          />
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
        <span>{active?.codec || 'H.264'}</span>
        <span>分辨率 {active?.resolution || '1920x1080'}</span>
        <span>{active?.fps || 25} fps</span>
        <span>{active?.bitrateKbps || 4096} kbps</span>
      </div>
    </section>
  );
}

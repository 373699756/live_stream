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
  createPlayer: (config: Record<string, unknown>) => FlvPlayer;
  isSupported?: () => boolean;
};

interface VideoPreviewProps {
  stream: StreamName;
  statuses: StreamStatus[];
  onStreamChange: (stream: StreamName) => void;
}

function loadScriptOnce(src: string): Promise<void> {
  return new Promise((resolve, reject) => {
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
    script.onerror = () => reject(new Error(`load failed: ${src}`));
    if (!existing) {
      document.head.appendChild(script);
    }
  });
}

async function loadLocalFlvModule(): Promise<FlvModule | undefined> {
  const current = window.mpegts || window.flvjs;
  if (current) {
    return current as FlvModule;
  }
  await loadScriptOnce('/vendor/flv.min.js');
  return (window.mpegts || window.flvjs) as FlvModule | undefined;
}

export function VideoPreview({ stream, statuses, onStreamChange }: VideoPreviewProps) {
  const [mode, setMode] = useState<PreviewMode>('flv');
  const [snapshotTick, setSnapshotTick] = useState(0);
  const [webrtcConfig, setWebrtcConfig] = useState<WebrtcConfig | null>(null);
  const [previewState, setPreviewState] = useState('等待 HTTP-FLV 视频流');
  const surfaceRef = useRef<HTMLDivElement | null>(null);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const peerRef = useRef<RTCPeerConnection | null>(null);
  const peerIdRef = useRef('');
  const hlsRef = useRef<HlsPlayer | null>(null);
  const flvRef = useRef<FlvPlayer | null>(null);
  const active = statuses.find((item) => item.stream === stream);
  const snapshotUrl = buildSnapshotUrl(stream, snapshotTick);
  const activeCodec = (active?.codec || '').toLowerCase().replace(/[^a-z0-9]/g, '');

  useEffect(() => {
    let mounted = true;
    void getWebrtcConfig()
      .then((config) => {
        if (mounted) {
          setWebrtcConfig(config);
          if (!config.enabled) {
            setMode((current) => (current === 'webrtc' ? 'snapshot' : current));
            setPreviewState('WebRTC 未启用');
          }
        }
      })
      .catch(() => {
        if (mounted) {
          setWebrtcConfig(null);
          setMode((current) => (current === 'webrtc' ? 'snapshot' : current));
          setPreviewState('WebRTC 配置不可用');
        }
      });
    return () => {
      mounted = false;
    };
  }, []);

  const webrtcEnabled = Boolean(webrtcConfig?.enabled);
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
    const resetVideoSurface = () => {
      if (hlsRef.current) {
        try {
          hlsRef.current.destroy();
        } catch {
          // Best-effort cleanup.
        }
        hlsRef.current = null;
      }
      if (flvRef.current) {
        try {
          flvRef.current.unload?.();
          flvRef.current.detachMediaElement?.();
          flvRef.current.destroy();
        } catch {
          // Best-effort cleanup.
        }
        flvRef.current = null;
      }
      if (video) {
        video.pause();
        video.srcObject = null;
        video.removeAttribute('src');
        video.load();
        video.onloadeddata = null;
        video.onplaying = null;
        video.onerror = null;
      }
    };
    const closeWebrtc = () => {
      if (peerRef.current) {
        peerRef.current.close();
        peerRef.current = null;
      }
      if (video) {
        video.srcObject = null;
      }
      void closeWebrtcPeer(peerIdRef.current);
      peerIdRef.current = '';
    };
    const cleanup = () => {
      resetVideoSurface();
      closeWebrtc();
    };

    if (mode === 'snapshot') {
      cleanup();
      setPreviewState('抓图预览');
      return cleanup;
    }
    if (!video) {
      setPreviewState('视频预览器不可用');
      return cleanup;
    }

    let disposed = false;
    video.onloadeddata = () => {
      setPreviewState('视频已连接');
    };
    video.onplaying = () => {
      setPreviewState('视频已连接');
    };
    video.onerror = () => {
      if (mode === 'hls') {
        setPreviewState('HLS 播放失败');
      } else if (mode === 'flv') {
        setPreviewState('HTTP-FLV 播放失败');
      }
    };

    if (mode === 'webrtc') {
      resetVideoSurface();
      if (!webrtcEnabled) {
        setMode('snapshot');
        setPreviewState('WebRTC 未启用');
        return cleanup;
      }
      setPreviewState('等待 WebRTC 视频流');
      const pc = new RTCPeerConnection({
        iceServers: (webrtcConfig?.ice_servers || []).map((server) => ({
          urls: server.url,
          username: server.username,
          credential: server.credential,
        })),
      });
      peerRef.current = pc;
      pc.addTransceiver('video', { direction: 'recvonly' });
      pc.ontrack = (event) => {
        if (videoRef.current && event.track.kind === 'video') {
          videoRef.current.srcObject = new MediaStream([event.track]);
          setPreviewState('视频已连接');
        }
      };
      pc.onicecandidate = (event) => {
        if (event.candidate && peerIdRef.current) {
          void sendWebrtcCandidate(peerIdRef.current, event.candidate.toJSON());
        }
      };
      pc.onconnectionstatechange = () => {
        setPreviewState(pc.connectionState);
      };
      pc.oniceconnectionstatechange = () => {
        if (pc.iceConnectionState === 'failed') {
          setPreviewState('ICE 连接失败');
        }
      };

      void (async () => {
        try {
          const peer = await createWebrtcPeer(stream);
          if (!peer.peer_id || disposed) {
            setPreviewState('WebRTC 后端不可用');
            return;
          }
          peerIdRef.current = peer.peer_id;
          const offer = await pc.createOffer();
          await pc.setLocalDescription(offer);
          const answer = await sendWebrtcOffer(peer.peer_id, offer.sdp || '');
          if (!answer.sdp || disposed) {
            setPreviewState('WebRTC 应答无效');
            return;
          }
          await pc.setRemoteDescription({ type: 'answer', sdp: answer.sdp });
        } catch {
          setPreviewState('WebRTC 连接失败');
        }
      })();

      return () => {
        disposed = true;
        cleanup();
      };
    }

    closeWebrtc();
    if (!streamingPlaybackEnabled) {
      setMode('snapshot');
      setPreviewState(mode === 'hls' ? 'HLS 不支持当前编码' : 'HTTP-FLV 不支持当前编码');
      return cleanup;
    }

    if (mode === 'hls') {
      resetVideoSurface();
      setPreviewState('等待 HLS 视频流');
      const url = hlsPlaylistUrl(stream);
      if (video.canPlayType('application/vnd.apple.mpegurl')) {
        video.src = url;
        void video.play().catch(() => {});
        setPreviewState('正在拉取 HLS 码流');
        return () => {
          disposed = true;
          cleanup();
        };
      }
      const Hls = window.Hls as HlsConstructor | undefined;
      if (!Hls || !Hls.isSupported?.()) {
        setPreviewState('HLS 播放器不可用');
        return () => {
          disposed = true;
          cleanup();
        };
      }
      const player = new Hls({ enableWorker: true });
      hlsRef.current = player;
      const errorEvent = Hls.Events?.ERROR;
      if (errorEvent && player.on) {
        player.on(errorEvent, () => {
          setPreviewState('HLS 播放失败');
        });
      }
      player.attachMedia(video);
      player.loadSource(url);
      void video.play().catch(() => {});
      setPreviewState('正在拉取 HLS 码流');
      return () => {
        disposed = true;
        cleanup();
      };
    }

    resetVideoSurface();
    setPreviewState('等待 HTTP-FLV 视频流');
    void (async () => {
      try {
        const flvModule = await loadLocalFlvModule();
        if (disposed) {
          return;
        }
        if (!flvModule || !flvModule.isSupported?.()) {
          setPreviewState('HTTP-FLV 播放器不可用');
          return;
        }
        const player = flvModule.createPlayer({
          type: 'flv',
          isLive: true,
          url: flvStreamUrl(stream),
          hasAudio: false,
          hasVideo: true,
        });
        flvRef.current = player;
        const errorEvent = flvModule.Events?.ERROR;
        if (errorEvent && player.on) {
          player.on(errorEvent, () => {
            setPreviewState('HTTP-FLV 播放失败');
          });
        }
        player.attachMediaElement(video);
        player.load();
        void player.play().catch(() => {});
        setPreviewState('正在拉取 HTTP-FLV 码流');
      } catch {
        if (!disposed) {
          setPreviewState('HTTP-FLV 播放器脚本未加载');
        }
      }
    })();
    return () => {
      disposed = true;
      cleanup();
    };
  }, [streamingPlaybackEnabled, mode, stream, webrtcConfig, webrtcEnabled]);

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
            disabled={!webrtcEnabled}
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
        {mode === 'snapshot' ? (
          <img
            className="snapshot-preview"
            src={snapshotUrl}
            alt="snapshot preview"
            onLoad={(event) => {
              event.currentTarget.style.opacity = '1';
            }}
            onError={(event) => {
              event.currentTarget.style.opacity = '0';
            }}
          />
        ) : (
          <video ref={videoRef} className="video-element" autoPlay muted playsInline />
        )}
        <div className="video-placeholder">
          <div className="lens-ring" />
          <strong>{previewState}</strong>
          <span>{previewDetail}</span>
        </div>
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

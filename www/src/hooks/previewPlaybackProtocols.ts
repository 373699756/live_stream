import {
  closeWebrtcPeer,
  createWebrtcPeer,
  flvStreamUrl,
  hlsPlaylistUrl,
  mjpegStreamUrl,
  sendWebrtcCandidate,
  sendWebrtcOffer,
} from '../api/stream';
import type { StreamName, WebrtcConfig } from '../api/types';
import type { PreviewMode } from './previewMode';
import {
  loadLocalFlvModule,
  loadLocalHlsModule,
  PlayerModuleUnavailableError,
  type FlvPlayer,
  type HlsPlayer,
} from './previewPlayerModules';

interface CurrentRef<T> {
  current: T;
}

export interface PreviewSessionControls {
  isCurrentSession: () => boolean;
  sessionSignal: AbortSignal;
  setConnected: (value: boolean) => void;
  setDecodedSize: (value: string) => void;
  setPreviewState: (value: string) => void;
  updateDisplaySize: () => void;
}

export function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

function streamSessionUrl(baseUrl: string, sessionId: number): string {
  return `${baseUrl}${baseUrl.includes('?') ? '&' : '?'}session=${sessionId}`;
}

function playerErrorDetails(args: unknown[]): string {
  return args
    .map((item) => (
      typeof item === 'string'
        ? item
        : item instanceof Error
          ? item.message
          : JSON.stringify(item)
    ))
    .filter(Boolean)
    .join(' ');
}

export function startMjpegPreview({
  controls,
  image,
  mjpegModeEnabled,
  mjpegReady,
  sessionId,
  stream,
}: {
  controls: PreviewSessionControls;
  image: HTMLImageElement;
  mjpegModeEnabled: boolean;
  mjpegReady: boolean;
  sessionId: number;
  stream: StreamName;
}) {
  if (!mjpegModeEnabled) {
    controls.setPreviewState('MJPEG 码流不可用');
    return;
  }
  if (!mjpegReady) {
    controls.setPreviewState('正在等待 MJPEG 首帧');
    return;
  }
  image.onload = () => {
    if (controls.isCurrentSession()) {
      controls.setConnected(true);
      controls.setPreviewState('MJPEG 已连接');
      if (image.naturalWidth > 0 && image.naturalHeight > 0) {
        controls.setDecodedSize(`${image.naturalWidth}x${image.naturalHeight}`);
      }
      controls.updateDisplaySize();
    }
  };
  image.onerror = () => {
    controls.setConnected(false);
    controls.setPreviewState('MJPEG 播放失败');
  };
  controls.setPreviewState('正在拉取 MJPEG 码流');
  image.src = streamSessionUrl(mjpegStreamUrl(stream), sessionId);
}

export function startHlsPreview({
  controls,
  hlsReady,
  hlsRef,
  setHlsPlayer,
  stream,
  video,
}: {
  controls: PreviewSessionControls;
  hlsReady: boolean;
  hlsRef: CurrentRef<HlsPlayer | null>;
  setHlsPlayer: (player: HlsPlayer) => void;
  stream: StreamName;
  video: HTMLVideoElement;
}) {
  const url = hlsPlaylistUrl(stream);
  if (!hlsReady) {
    controls.setPreviewState('正在启动 HLS 码流');
    void fetch(url, {
      cache: 'no-store',
      signal: controls.sessionSignal,
    }).catch((error: unknown) => {
      if (!isAbortError(error) && controls.isCurrentSession()) {
        controls.setPreviewState('HLS 启动失败');
      }
    });
    return;
  }
  controls.setPreviewState('等待 HLS 视频流');
  if (video.canPlayType('application/vnd.apple.mpegurl')) {
    video.src = url;
    void video.play().catch(() => {});
    controls.setPreviewState('正在拉取 HLS 码流');
    return;
  }
  void (async () => {
    try {
      const Hls = await loadLocalHlsModule();
      if (!controls.isCurrentSession()) {
        return;
      }
      if (!Hls.isSupported?.()) {
        controls.setPreviewState('HLS 播放器不可用');
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
      setHlsPlayer(player);
      const errorEvent = Hls.Events?.ERROR;
      if (errorEvent && player.on) {
        player.on(errorEvent, () => {
          if (controls.isCurrentSession() && hlsRef.current === player) {
            controls.setPreviewState('HLS 播放失败');
          }
        });
      }
      player.attachMedia(video);
      player.loadSource(url);
      void video.play().catch(() => {});
      controls.setPreviewState('正在拉取 HLS 码流');
    } catch (error) {
      if (controls.isCurrentSession()) {
        controls.setPreviewState(
          error instanceof PlayerModuleUnavailableError
            ? 'HLS 播放器初始化失败'
            : 'HLS 播放器脚本加载失败',
        );
      }
    }
  })();
}

export function startFlvPreview({
  controls,
  flvReady,
  flvRef,
  sessionId,
  setFlvPlayer,
  stream,
  video,
}: {
  controls: PreviewSessionControls;
  flvReady: boolean;
  flvRef: CurrentRef<FlvPlayer | null>;
  sessionId: number;
  setFlvPlayer: (player: FlvPlayer) => void;
  stream: StreamName;
  video: HTMLVideoElement;
}) {
  if (!flvReady) {
    controls.setPreviewState('正在等待 HTTP-FLV 首帧');
    return;
  }
  controls.setPreviewState('等待 HTTP-FLV 视频流');
  void (async () => {
    try {
      const flvModule = await loadLocalFlvModule();
      if (!controls.isCurrentSession()) {
        return;
      }
      const flvSupported = flvModule.isSupported?.() ?? true;
      const liveSupported =
        flvModule.getFeatureList?.().mseLiveFlvPlayback ?? flvSupported;
      if (!flvSupported || !liveSupported) {
        controls.setPreviewState('HTTP-FLV 播放器不可用');
        return;
      }
      const player = flvModule.createPlayer({
        type: 'flv',
        isLive: true,
        url: streamSessionUrl(flvStreamUrl(stream), sessionId),
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
      setFlvPlayer(player);
      const errorEvent = flvModule.Events?.ERROR;
      if (errorEvent && player.on) {
        player.on(errorEvent, (...args: unknown[]) => {
          if (controls.isCurrentSession() && flvRef.current === player) {
            const details = playerErrorDetails(args);
            controls.setPreviewState(
              details ? `HTTP-FLV 播放失败：${details}` : 'HTTP-FLV 播放失败',
            );
          }
        });
      }
      player.attachMediaElement(video);
      player.load();
      void player.play().catch(() => {});
      controls.setPreviewState('正在拉取 HTTP-FLV 码流');
    } catch (error) {
      if (controls.isCurrentSession()) {
        controls.setPreviewState(
          error instanceof PlayerModuleUnavailableError
            ? 'HTTP-FLV 播放器初始化失败'
            : 'HTTP-FLV 播放器脚本加载失败',
        );
      }
    }
  })();
}

export function startWebrtcPreview({
  closeWebrtcSession,
  controls,
  flvPlaybackReady,
  isSessionConnected,
  onAutoModeFallback,
  peerRef,
  restartPreview,
  setMode,
  setPeer,
  setPeerId,
  stream,
  videoRef,
  webrtcConfig,
  webrtcConfigError,
  webrtcConfigLoaded,
  webrtcEnabled,
  webrtcReady,
}: {
  closeWebrtcSession: () => void;
  controls: PreviewSessionControls;
  flvPlaybackReady: boolean;
  isSessionConnected: () => boolean;
  onAutoModeFallback: () => void;
  peerRef: CurrentRef<RTCPeerConnection | null>;
  restartPreview: (message: string) => void;
  setMode: (mode: PreviewMode) => void;
  setPeer: (peer: RTCPeerConnection) => void;
  setPeerId: (peerId: string) => void;
  stream: StreamName;
  videoRef: CurrentRef<HTMLVideoElement | null>;
  webrtcConfig: WebrtcConfig | null;
  webrtcConfigError: string;
  webrtcConfigLoaded: boolean;
  webrtcEnabled: boolean;
  webrtcReady: boolean;
}): number {
  if (!webrtcConfigLoaded) {
    controls.setPreviewState('正在读取 WebRTC 配置');
    return 0;
  }
  if (webrtcConfigError) {
    controls.setPreviewState(webrtcConfigError);
    return 0;
  }
  if (!webrtcEnabled) {
    controls.setPreviewState('WebRTC 未启用');
    return 0;
  }
  if (!webrtcReady) {
    controls.setPreviewState('WebRTC 暂未就绪');
    return 0;
  }
  controls.setPreviewState('等待 WebRTC 视频流');
  const startupTimer = window.setTimeout(() => {
    if (!controls.isCurrentSession() || isSessionConnected()) {
      return;
    }
    if (flvPlaybackReady) {
      onAutoModeFallback();
      restartPreview('WebRTC 连接超时，切换 HTTP-FLV');
      setMode('flv');
      return;
    }
    controls.setPreviewState('WebRTC 连接超时');
    closeWebrtcSession();
  }, 3500);
  const pc = new RTCPeerConnection({
    bundlePolicy: 'max-bundle',
    rtcpMuxPolicy: 'require',
    iceServers: (webrtcConfig?.ice_servers || []).map((server) => ({
      urls: server.url,
      username: server.username,
      credential: server.credential,
    })),
  });
  setPeer(pc);
  pc.addTransceiver('video', { direction: 'recvonly' });
  pc.ontrack = (event) => {
    if (
      !controls.isCurrentSession() ||
      peerRef.current !== pc ||
      event.track.kind !== 'video'
    ) {
      return;
    }
    const mediaStream = event.streams[0] || new MediaStream([event.track]);
    if (videoRef.current) {
      videoRef.current.srcObject = mediaStream;
      void videoRef.current.play().catch(() => {});
      controls.setConnected(true);
      controls.setPreviewState('视频已连接');
    }
  };
  let currentPeerId = '';
  pc.onicecandidate = (event) => {
    if (
      controls.isCurrentSession() &&
      peerRef.current === pc &&
      event.candidate &&
      currentPeerId
    ) {
      void sendWebrtcCandidate(currentPeerId, event.candidate.toJSON(), {
        signal: controls.sessionSignal,
      });
    }
  };
  pc.onconnectionstatechange = () => {
    if (!controls.isCurrentSession() || peerRef.current !== pc) {
      return;
    }
    if (pc.connectionState === 'connected') {
      controls.setConnected(true);
      controls.setPreviewState('WebRTC 已连接');
    } else if (
      pc.connectionState === 'failed' ||
      pc.connectionState === 'disconnected' ||
      pc.connectionState === 'closed'
    ) {
      controls.setConnected(false);
      controls.setPreviewState(
        pc.connectionState === 'failed' ? 'WebRTC 连接失败' : 'WebRTC 已断开',
      );
      closeWebrtcSession();
    } else {
      controls.setPreviewState(`WebRTC ${pc.connectionState}`);
    }
  };
  pc.oniceconnectionstatechange = () => {
    if (!controls.isCurrentSession() || peerRef.current !== pc) {
      return;
    }
    if (
      pc.iceConnectionState === 'failed' ||
      pc.iceConnectionState === 'disconnected' ||
      pc.iceConnectionState === 'closed'
    ) {
      controls.setConnected(false);
      controls.setPreviewState('ICE 连接失败');
      closeWebrtcSession();
    }
  };

  void (async () => {
    try {
      const peer = await createWebrtcPeer(stream, { signal: controls.sessionSignal });
      if (!peer.peer_id || !controls.isCurrentSession() || peerRef.current !== pc) {
        if (peer.peer_id) {
          void closeWebrtcPeer(peer.peer_id);
        }
        if (controls.isCurrentSession()) {
          controls.setPreviewState('WebRTC 后端不可用');
          closeWebrtcSession();
        }
        return;
      }
      currentPeerId = peer.peer_id;
      setPeerId(peer.peer_id);
      const offer = await pc.createOffer();
      if (!controls.isCurrentSession() || peerRef.current !== pc) {
        void closeWebrtcPeer(peer.peer_id);
        return;
      }
      await pc.setLocalDescription(offer);
      if (!controls.isCurrentSession() || peerRef.current !== pc) {
        void closeWebrtcPeer(peer.peer_id);
        return;
      }
      const answer = await sendWebrtcOffer(peer.peer_id, offer.sdp || '', {
        signal: controls.sessionSignal,
      });
      if (!answer.sdp || !controls.isCurrentSession() || peerRef.current !== pc) {
        void closeWebrtcPeer(peer.peer_id);
        if (controls.isCurrentSession()) {
          controls.setPreviewState('WebRTC 应答无效');
          closeWebrtcSession();
        }
        return;
      }
      await pc.setRemoteDescription({ type: 'answer', sdp: answer.sdp });
      if (!controls.isCurrentSession() || peerRef.current !== pc) {
        void closeWebrtcPeer(peer.peer_id);
      }
    } catch (error: unknown) {
      if (isAbortError(error)) {
        return;
      }
      if (controls.isCurrentSession()) {
        controls.setPreviewState('WebRTC 连接失败');
        closeWebrtcSession();
      }
    }
  })();

  return startupTimer;
}

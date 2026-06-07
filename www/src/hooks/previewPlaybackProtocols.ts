import {
  loadLocalFlvModule,
  loadLocalHlsModule,
  PlayerModuleUnavailableError,
  type FlvPlayer,
  type HlsPlayer,
} from './previewPlayerModules';
import {
  isAbortError,
  type CurrentRef,
  type PreviewSessionControls,
} from './previewSession';

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
  mjpegUrl,
  mjpegModeEnabled,
  mjpegReady,
  sessionId,
}: {
  controls: PreviewSessionControls;
  image: HTMLImageElement;
  mjpegUrl: string;
  mjpegModeEnabled: boolean;
  mjpegReady: boolean;
  sessionId: number;
}) {
  if (!mjpegModeEnabled) {
    controls.setPreviewState('MJPEG 码流不可用');
    return;
  }
  if (!mjpegReady) {
    controls.setPreviewState('正在等待 MJPEG 首帧');
    return;
  }
  if (!mjpegUrl) {
    controls.setPreviewState('MJPEG 地址不可用');
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
  image.src = streamSessionUrl(mjpegUrl, sessionId);
}

export function startHlsPreview({
  controls,
  hlsReady,
  hlsRef,
  hlsUrl,
  setHlsPlayer,
  video,
}: {
  controls: PreviewSessionControls;
  hlsReady: boolean;
  hlsRef: CurrentRef<HlsPlayer | null>;
  hlsUrl: string;
  setHlsPlayer: (player: HlsPlayer) => void;
  video: HTMLVideoElement;
}) {
  if (!hlsUrl) {
    controls.setPreviewState('HLS 地址不可用');
    return;
  }
  if (!hlsReady) {
    controls.setPreviewState('正在启动 HLS 码流');
    void fetch(hlsUrl, {
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
    video.src = hlsUrl;
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
      player.loadSource(hlsUrl);
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
  flvUrl,
  sessionId,
  setFlvPlayer,
  video,
}: {
  controls: PreviewSessionControls;
  flvReady: boolean;
  flvRef: CurrentRef<FlvPlayer | null>;
  flvUrl: string;
  sessionId: number;
  setFlvPlayer: (player: FlvPlayer) => void;
  video: HTMLVideoElement;
}) {
  if (!flvReady) {
    controls.setPreviewState('正在等待 HTTP-FLV 首帧');
    return;
  }
  if (!flvUrl) {
    controls.setPreviewState('HTTP-FLV 地址不可用');
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
        url: streamSessionUrl(flvUrl, sessionId),
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

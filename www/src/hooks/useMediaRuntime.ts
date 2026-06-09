import { useCallback, useEffect, useRef, useState } from 'react';
import { openMediaEvents } from '../api/mediaEvents';
import {
  getMediaPlaybackUrls,
  getMediaSessions,
  getMediaStreams,
} from '../api/stream';
import type {
  MediaPlaybackUrls,
  MediaSessionsResponse,
  MediaSessionInfo,
  MediaStreamRuntime,
  StreamName,
} from '../api/types';

type RuntimeErrorKey = 'statuses' | 'sessions' | 'playbackUrls';

interface UseMediaRuntimeOptions {
  selectedStream?: StreamName;
  playbackStreams?: StreamName[];
  includeSessions?: boolean;
  refreshIntervalMs?: number;
  fastRefreshIntervalMs?: number;
  fastRefreshCount?: number;
  subscribeEvents?: boolean;
  refreshPlaybackUrlsOnInterval?: boolean;
  statusTimeoutMs?: number;
  playbackUrlTimeoutMs?: number;
  sessionTimeoutMs?: number;
  statusErrorMessage?: string;
  playbackUrlErrorMessage?: string;
  sessionErrorMessage?: string;
}

const defaultStatusTimeoutMs = 1800;
const emptySessionSummary: Omit<MediaSessionsResponse, 'items'> = {
  http_flv_active_clients: 0,
  mjpeg_active_clients: 0,
  rtsp_active_sessions: 0,
  webrtc_active_peers: 0,
  webrtc_dtls_ready: false,
  webrtc_enabled: false,
  webrtc_ice_ready: false,
  webrtc_ice_server_count: 0,
  webrtc_local_port_base: 0,
  webrtc_max_peers: 0,
  webrtc_public_ip: '',
  webrtc_selected_ice_pairs: 0,
  webrtc_signaling_ready: false,
  webrtc_srtp_ready: false,
};

function uniqueStreams(streams: StreamName[]): StreamName[] {
  return streams.filter((stream, index) => streams.indexOf(stream) === index);
}

function requestErrorMessage(error: unknown, fallback: string) {
  return error instanceof Error && error.message ? error.message : fallback;
}

export function useMediaRuntime({
  selectedStream,
  playbackStreams,
  includeSessions = false,
  refreshIntervalMs = 0,
  fastRefreshIntervalMs = 0,
  fastRefreshCount = 0,
  subscribeEvents = false,
  refreshPlaybackUrlsOnInterval = false,
  statusTimeoutMs = defaultStatusTimeoutMs,
  playbackUrlTimeoutMs = statusTimeoutMs,
  sessionTimeoutMs = statusTimeoutMs,
  statusErrorMessage = '码流状态刷新失败',
  playbackUrlErrorMessage = '播放地址加载失败',
  sessionErrorMessage = '媒体会话加载失败',
}: UseMediaRuntimeOptions = {}) {
  const requestedPlaybackStreams = uniqueStreams(
    playbackStreams ?? (selectedStream ? [selectedStream] : []),
  );
  const playbackStreamKey = requestedPlaybackStreams.join(',');
  const [statuses, setStatuses] = useState<MediaStreamRuntime[]>([]);
  const [sessions, setSessions] = useState<MediaSessionInfo[]>([]);
  const [sessionSummary, setSessionSummary] =
    useState<Omit<MediaSessionsResponse, 'items'>>(emptySessionSummary);
  const [urlsByStream, setUrlsByStream] =
    useState<Partial<Record<StreamName, MediaPlaybackUrls>>>({});
  const [errors, setErrors] =
    useState<Partial<Record<RuntimeErrorKey, string>>>({});
  const [lastUpdatedAt, setLastUpdatedAt] = useState<number | null>(null);
  const mountedRef = useRef(true);
  const statusRequestRef = useRef<Promise<void> | null>(null);
  const sessionRequestRef = useRef<Promise<void> | null>(null);
  const statusAbortRef = useRef<AbortController | null>(null);
  const sessionAbortRef = useRef<AbortController | null>(null);
  const playbackUrlAbortRef = useRef<AbortController | null>(null);
  const playbackUrlRequestIdRef = useRef(0);

  const setRuntimeError = useCallback((
    key: RuntimeErrorKey,
    message: string,
  ) => {
    setErrors((current) => {
      const next = { ...current };
      if (message) {
        next[key] = message;
      } else {
        delete next[key];
      }
      return next;
    });
  }, []);

  const abortStatusRequest = useCallback(() => {
    statusAbortRef.current?.abort();
    statusAbortRef.current = null;
    statusRequestRef.current = null;
  }, []);

  const abortSessionRequest = useCallback(() => {
    sessionAbortRef.current?.abort();
    sessionAbortRef.current = null;
    sessionRequestRef.current = null;
  }, []);

  const abortPlaybackUrlRequest = useCallback(() => {
    playbackUrlAbortRef.current?.abort();
    playbackUrlAbortRef.current = null;
    playbackUrlRequestIdRef.current += 1;
  }, []);

  const refreshStatuses = useCallback(() => {
    if (statusRequestRef.current) {
      return statusRequestRef.current;
    }
    const controller = new AbortController();
    statusAbortRef.current = controller;
    const request = getMediaStreams({
      signal: controller.signal,
      timeoutMs: statusTimeoutMs,
    })
      .then((nextStatuses) => {
        if (!mountedRef.current || controller.signal.aborted) {
          return;
        }
        setStatuses(Array.isArray(nextStatuses) ? nextStatuses : []);
        setLastUpdatedAt(Date.now());
        setRuntimeError('statuses', '');
      })
      .catch((error: unknown) => {
        if (mountedRef.current && !controller.signal.aborted) {
          setRuntimeError(
            'statuses',
            requestErrorMessage(error, statusErrorMessage),
          );
        }
      })
      .finally(() => {
        if (statusRequestRef.current === request) {
          statusRequestRef.current = null;
          statusAbortRef.current = null;
        }
      });
    statusRequestRef.current = request;
    return request;
  }, [setRuntimeError, statusErrorMessage, statusTimeoutMs]);

  const refreshSessions = useCallback(() => {
    if (!includeSessions) {
      return Promise.resolve();
    }
    if (sessionRequestRef.current) {
      return sessionRequestRef.current;
    }
    const controller = new AbortController();
    sessionAbortRef.current = controller;
    const request = getMediaSessions({
      signal: controller.signal,
      timeoutMs: sessionTimeoutMs,
    })
      .then((nextSessions) => {
        if (!mountedRef.current || controller.signal.aborted) {
          return;
        }
        const { items, ...summary } = nextSessions;
        setSessions(Array.isArray(items) ? items : []);
        setSessionSummary({ ...emptySessionSummary, ...summary });
        setRuntimeError('sessions', '');
      })
      .catch((error: unknown) => {
        if (mountedRef.current && !controller.signal.aborted) {
          setRuntimeError(
            'sessions',
            requestErrorMessage(error, sessionErrorMessage),
          );
        }
      })
      .finally(() => {
        if (sessionRequestRef.current === request) {
          sessionRequestRef.current = null;
          sessionAbortRef.current = null;
        }
      });
    sessionRequestRef.current = request;
    return request;
  }, [
    includeSessions,
    sessionErrorMessage,
    sessionTimeoutMs,
    setRuntimeError,
  ]);

  const refreshPlaybackUrls = useCallback(() => {
    playbackUrlAbortRef.current?.abort();
    const controller = new AbortController();
    playbackUrlAbortRef.current = controller;
    playbackUrlRequestIdRef.current += 1;
    const requestId = playbackUrlRequestIdRef.current;
    const streamsToLoad = requestedPlaybackStreams;
    if (streamsToLoad.length === 0) {
      playbackUrlAbortRef.current = null;
      setUrlsByStream({});
      setRuntimeError('playbackUrls', '');
      return Promise.resolve();
    }
    return Promise.all(
      streamsToLoad.map(async (stream) => ({
        stream,
        urls: await getMediaPlaybackUrls(stream, {
          signal: controller.signal,
          timeoutMs: playbackUrlTimeoutMs,
        }),
      })),
    )
      .then((entries) => {
        if (
          !mountedRef.current ||
          controller.signal.aborted ||
          playbackUrlRequestIdRef.current !== requestId
        ) {
          return;
        }
        setUrlsByStream((current) => {
          const next = { ...current };
          entries.forEach(({ stream, urls }) => {
            next[stream] = urls;
          });
          return next;
        });
        setRuntimeError('playbackUrls', '');
      })
      .catch((error: unknown) => {
        if (
          !mountedRef.current ||
          controller.signal.aborted ||
          playbackUrlRequestIdRef.current !== requestId
        ) {
          return;
        }
        setUrlsByStream((current) => {
          const next = { ...current };
          streamsToLoad.forEach((stream) => {
            delete next[stream];
          });
          return next;
        });
        setRuntimeError(
          'playbackUrls',
          requestErrorMessage(error, playbackUrlErrorMessage),
        );
      })
      .finally(() => {
        if (playbackUrlRequestIdRef.current === requestId) {
          playbackUrlAbortRef.current = null;
        }
      });
  }, [
    playbackStreamKey,
    playbackUrlErrorMessage,
    playbackUrlTimeoutMs,
    setRuntimeError,
  ]);

  const refreshRuntime = useCallback(() => {
    void refreshStatuses();
    void refreshSessions();
  }, [refreshSessions, refreshStatuses]);

  const refreshIntervalRuntime = useCallback(() => {
    refreshRuntime();
    if (refreshPlaybackUrlsOnInterval) {
      void refreshPlaybackUrls();
    }
  }, [
    refreshPlaybackUrls,
    refreshPlaybackUrlsOnInterval,
    refreshRuntime,
  ]);

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
      abortStatusRequest();
      abortSessionRequest();
      abortPlaybackUrlRequest();
    };
  }, [
    abortPlaybackUrlRequest,
    abortSessionRequest,
    abortStatusRequest,
  ]);

  useEffect(() => {
    if (requestedPlaybackStreams.length > 0) {
      setUrlsByStream((current) => {
        let changed = false;
        const next = { ...current };
        requestedPlaybackStreams.forEach((stream) => {
          if (next[stream]) {
            delete next[stream];
            changed = true;
          }
        });
        return changed ? next : current;
      });
    }
    void refreshPlaybackUrls();
    return abortPlaybackUrlRequest;
  }, [abortPlaybackUrlRequest, refreshPlaybackUrls]);

  useEffect(() => {
    let fastRefreshes = 0;
    refreshRuntime();
    const fastTimer = fastRefreshIntervalMs > 0 && fastRefreshCount > 0
      ? window.setInterval(() => {
        fastRefreshes += 1;
        refreshRuntime();
        if (fastRefreshes >= fastRefreshCount) {
          window.clearInterval(fastTimer);
        }
      }, fastRefreshIntervalMs)
      : 0;
    const steadyTimer = refreshIntervalMs > 0
      ? window.setInterval(refreshIntervalRuntime, refreshIntervalMs)
      : 0;
    let eventSource: EventSource | null = null;
    if (subscribeEvents && typeof EventSource !== 'undefined') {
      eventSource = openMediaEvents((event) => {
        const readyChanged =
          event.target.endsWith('.ready') && event.message === 'changed';
        const firstFrame =
          event.target.endsWith('.frame') && event.message === 'first';
        if (event.type === 'media_status_changed' && (readyChanged || firstFrame)) {
          refreshRuntime();
        }
      });
      eventSource.onerror = () => {
        refreshRuntime();
      };
    }
    return () => {
      eventSource?.close();
      if (fastTimer !== 0) {
        window.clearInterval(fastTimer);
      }
      if (steadyTimer !== 0) {
        window.clearInterval(steadyTimer);
      }
      abortStatusRequest();
      abortSessionRequest();
    };
  }, [
    abortSessionRequest,
    abortStatusRequest,
    fastRefreshCount,
    fastRefreshIntervalMs,
    refreshIntervalMs,
    refreshIntervalRuntime,
    refreshRuntime,
    subscribeEvents,
  ]);

  const playbackUrls = selectedStream
    ? urlsByStream[selectedStream] ?? null
    : null;
  const error =
    errors.statuses || errors.playbackUrls || errors.sessions || '';

  return {
    statuses,
    sessions,
    sessionSummary,
    playbackUrls,
    urlsByStream,
    error,
    lastUpdatedAt,
    refreshStatuses,
    refreshSessions,
    refreshPlaybackUrls,
  };
}

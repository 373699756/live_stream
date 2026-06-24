import { useCallback, useEffect, useRef, useState } from 'react';
import { openMediaEvents } from '../api/mediaEvents';
import {
    getMediaPreviewUrls,
    getMediaSessions,
    getMediaStreams,
} from '../api/stream';
import type {
    MediaPreviewUrls,
    MediaSessionsResponse,
    MediaSessionInfo,
    MediaStreamInfo,
    StreamName,
} from '../api/types';

type MediaStreamsInfoErrorKey = 'statuses' | 'sessions' | 'previewUrls';

interface UseMediaStreamsInfoOptions {
    selectedStream?: StreamName;
    previewStreams?: StreamName[];
    includeSessions?: boolean;
    refreshIntervalMs?: number;
    fastRefreshIntervalMs?: number;
    fastRefreshLimit?: number;
    subscribeEvents?: boolean;
    refreshPreviewUrlsOnInterval?: boolean;
    statusTimeoutMs?: number;
    previewUrlTimeoutMs?: number;
    sessionTimeoutMs?: number;
    statusErrorMessage?: string;
    previewUrlErrorMessage?: string;
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
    webrtc_ice_server_size: 0,
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

export function useMediaStreamsInfo({
    selectedStream,
    previewStreams,
    includeSessions = false,
    refreshIntervalMs = 0,
    fastRefreshIntervalMs = 0,
    fastRefreshLimit = 0,
    subscribeEvents = false,
    refreshPreviewUrlsOnInterval = false,
    statusTimeoutMs = defaultStatusTimeoutMs,
    previewUrlTimeoutMs = statusTimeoutMs,
    sessionTimeoutMs = statusTimeoutMs,
    statusErrorMessage = '码流状态刷新失败',
    previewUrlErrorMessage = '预览地址加载失败',
    sessionErrorMessage = '媒体会话加载失败',
}: UseMediaStreamsInfoOptions = {}) {
    const requestedPreviewStreams = uniqueStreams(
        previewStreams ?? (selectedStream ? [selectedStream] : []),
    );
    const previewStreamKey = requestedPreviewStreams.join(',');
    const [statuses, setStatuses] = useState<MediaStreamInfo[]>([]);
    const [sessions, setSessions] = useState<MediaSessionInfo[]>([]);
    const [sessionSummary, setSessionSummary] =
        useState<Omit<MediaSessionsResponse, 'items'>>(emptySessionSummary);
    const [urlsByStream, setUrlsByStream] = useState<
        Partial<Record<StreamName, MediaPreviewUrls>>
    >({});
    const [errors, setErrors] = useState<
        Partial<Record<MediaStreamsInfoErrorKey, string>>
    >({});
    const [lastUpdatedAt, setLastUpdatedAt] = useState<number | null>(null);
    const mountedRef = useRef(true);
    const statusRequestRef = useRef<Promise<void> | null>(null);
    const sessionRequestRef = useRef<Promise<void> | null>(null);
    const statusAbortRef = useRef<AbortController | null>(null);
    const sessionAbortRef = useRef<AbortController | null>(null);
    const previewUrlAbortRef = useRef<AbortController | null>(null);
    const previewUrlRequestIdRef = useRef(0);

    const setMediaStreamsInfoError = useCallback(
        (key: MediaStreamsInfoErrorKey, message: string) => {
            setErrors((current) => {
                const next = { ...current };
                if (message) {
                    next[key] = message;
                } else {
                    delete next[key];
                }
                return next;
            });
        },
        [],
    );

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

    const abortPreviewUrlRequest = useCallback(() => {
        previewUrlAbortRef.current?.abort();
        previewUrlAbortRef.current = null;
        previewUrlRequestIdRef.current += 1;
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
                setMediaStreamsInfoError('statuses', '');
            })
            .catch((error: unknown) => {
                if (mountedRef.current && !controller.signal.aborted) {
                    setMediaStreamsInfoError(
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
    }, [setMediaStreamsInfoError, statusErrorMessage, statusTimeoutMs]);

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
                setMediaStreamsInfoError('sessions', '');
            })
            .catch((error: unknown) => {
                if (mountedRef.current && !controller.signal.aborted) {
                    setMediaStreamsInfoError(
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
        setMediaStreamsInfoError,
    ]);

    const refreshPreviewUrls = useCallback(() => {
        previewUrlAbortRef.current?.abort();
        const controller = new AbortController();
        previewUrlAbortRef.current = controller;
        previewUrlRequestIdRef.current += 1;
        const requestId = previewUrlRequestIdRef.current;
        const streamsToLoad = requestedPreviewStreams;
        if (streamsToLoad.length === 0) {
            previewUrlAbortRef.current = null;
            setUrlsByStream({});
            setMediaStreamsInfoError('previewUrls', '');
            return Promise.resolve();
        }
        return Promise.all(
            streamsToLoad.map(async (stream) => ({
                stream,
                urls: await getMediaPreviewUrls(stream, {
                    signal: controller.signal,
                    timeoutMs: previewUrlTimeoutMs,
                }),
            })),
        )
            .then((entries) => {
                if (
                    !mountedRef.current ||
                    controller.signal.aborted ||
                    previewUrlRequestIdRef.current !== requestId
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
                setMediaStreamsInfoError('previewUrls', '');
            })
            .catch((error: unknown) => {
                if (
                    !mountedRef.current ||
                    controller.signal.aborted ||
                    previewUrlRequestIdRef.current !== requestId
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
                setMediaStreamsInfoError(
                    'previewUrls',
                    requestErrorMessage(error, previewUrlErrorMessage),
                );
            })
            .finally(() => {
                if (previewUrlRequestIdRef.current === requestId) {
                    previewUrlAbortRef.current = null;
                }
            });
    }, [
        previewStreamKey,
        previewUrlErrorMessage,
        previewUrlTimeoutMs,
        setMediaStreamsInfoError,
    ]);

    const refreshStreamsInfo = useCallback(() => {
        void refreshStatuses();
        void refreshSessions();
    }, [refreshSessions, refreshStatuses]);

    const refreshIntervalStreamsInfo = useCallback(() => {
        refreshStreamsInfo();
        if (refreshPreviewUrlsOnInterval) {
            void refreshPreviewUrls();
        }
    }, [refreshPreviewUrls, refreshPreviewUrlsOnInterval, refreshStreamsInfo]);

    useEffect(() => {
        mountedRef.current = true;
        return () => {
            mountedRef.current = false;
            abortStatusRequest();
            abortSessionRequest();
            abortPreviewUrlRequest();
        };
    }, [abortPreviewUrlRequest, abortSessionRequest, abortStatusRequest]);

    useEffect(() => {
        if (requestedPreviewStreams.length > 0) {
            setUrlsByStream((current) => {
                let changed = false;
                const next = { ...current };
                requestedPreviewStreams.forEach((stream) => {
                    if (next[stream]) {
                        delete next[stream];
                        changed = true;
                    }
                });
                return changed ? next : current;
            });
        }
        void refreshPreviewUrls();
        return abortPreviewUrlRequest;
    }, [abortPreviewUrlRequest, refreshPreviewUrls]);

    useEffect(() => {
        let fastRefreshes = 0;
        refreshStreamsInfo();
        const fastTimer =
            fastRefreshIntervalMs > 0 && fastRefreshLimit > 0
                ? window.setInterval(() => {
                      fastRefreshes += 1;
                      refreshStreamsInfo();
                      if (fastRefreshes >= fastRefreshLimit) {
                          window.clearInterval(fastTimer);
                      }
                  }, fastRefreshIntervalMs)
                : 0;
        const steadyTimer =
            refreshIntervalMs > 0
                ? window.setInterval(
                      refreshIntervalStreamsInfo,
                      refreshIntervalMs,
                  )
                : 0;
        let eventSource: EventSource | null = null;
        if (subscribeEvents && typeof EventSource !== 'undefined') {
            eventSource = openMediaEvents((event) => {
                const readyChanged =
                    event.target.endsWith('.ready') &&
                    event.message === 'changed';
                const firstFrame =
                    event.target.endsWith('.frame') &&
                    event.message === 'first';
                if (
                    event.type === 'media_status_changed' &&
                    (readyChanged || firstFrame)
                ) {
                    refreshStreamsInfo();
                }
            });
            eventSource.onerror = () => {
                refreshStreamsInfo();
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
        fastRefreshLimit,
        fastRefreshIntervalMs,
        refreshIntervalMs,
        refreshIntervalStreamsInfo,
        refreshStreamsInfo,
        subscribeEvents,
    ]);

    const previewUrls = selectedStream
        ? (urlsByStream[selectedStream] ?? null)
        : null;
    const error =
        errors.statuses || errors.previewUrls || errors.sessions || '';

    return {
        statuses,
        sessions,
        sessionSummary,
        previewUrls,
        urlsByStream,
        error,
        lastUpdatedAt,
        refreshStatuses,
        refreshSessions,
        refreshPreviewUrls,
    };
}

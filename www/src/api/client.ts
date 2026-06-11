// Core HTTP fetch utilities. Business API functions live in domain-specific
// modules (video.ts, image.ts, network.ts, system.ts, stream.ts).

import { dispatchAuthInvalid, dispatchMustChangePassword } from './authSession';

const baseHeaders = { 'Content-Type': 'application/json' };
const defaultTimeoutMs = 8000;

export const useMockFallback = import.meta.env.DEV;

export type ApiRequestOptions = RequestInit & {
    timeoutMs?: number;
};

export interface ApiErrorBody {
    code: string;
    message: string;
}

export interface ApiEnvelope<T> {
    ok: boolean;
    data?: T | null;
    error?: ApiErrorBody | string | null;
    request_id?: string;
}

export class ApiClientError extends Error {
    code: string;
    requestId: string;
    status: number;

    constructor({
        code,
        message,
        requestId = '',
        status = 0,
    }: {
        code: string;
        message: string;
        requestId?: string;
        status?: number;
    }) {
        super(message);
        this.name = 'ApiClientError';
        this.code = code;
        this.requestId = requestId;
        this.status = status;
    }
}

interface ManagedRequestSignal {
    cleanup: () => void;
    signal: AbortSignal;
}

interface SendRequestOptions<TResponse> {
    allowMockFallback?: boolean;
    body?: BodyInit;
    fallback?: TResponse;
    init?: ApiRequestOptions;
    method?: string;
    path: string;
    responseType: 'json' | 'void';
}

type AbortSignalStatic = typeof AbortSignal & {
    any?: (signals: AbortSignal[]) => AbortSignal;
    timeout?: (milliseconds: number) => AbortSignal;
};

function fetchOptions(init?: ApiRequestOptions): RequestInit {
    if (!init) {
        return {};
    }
    const options: ApiRequestOptions = { ...init };
    delete options.timeoutMs;
    return options;
}

export function authQuery({
    entries,
}: {
    entries?: Record<string, string>;
} = {}): string {
    const params = new URLSearchParams();
    if (entries) {
        Object.entries(entries).forEach(([key, value]) =>
            params.set(key, value),
        );
    }
    return params.toString();
}

export function authHeaders(init?: RequestInit): HeadersInit {
    const headers = new Headers(baseHeaders);
    if (init?.headers) {
        new Headers(init.headers).forEach((value, key) =>
            headers.set(key, value),
        );
    }
    return headers;
}

function mergeSignals(
    timeoutSignal: AbortSignal,
    requestSignal?: AbortSignal | null,
): ManagedRequestSignal {
    if (!requestSignal) {
        return { signal: timeoutSignal, cleanup: () => {} };
    }
    const abortSignal = AbortSignal as AbortSignalStatic;
    if (abortSignal.any) {
        return {
            signal: abortSignal.any([requestSignal, timeoutSignal]),
            cleanup: () => {},
        };
    }
    const controller = new AbortController();
    const abort = () => controller.abort();
    if (requestSignal.aborted || timeoutSignal.aborted) {
        controller.abort();
        return { signal: controller.signal, cleanup: () => {} };
    }
    requestSignal.addEventListener('abort', abort, { once: true });
    timeoutSignal.addEventListener('abort', abort, { once: true });
    return {
        signal: controller.signal,
        cleanup: () => {
            requestSignal.removeEventListener('abort', abort);
            timeoutSignal.removeEventListener('abort', abort);
        },
    };
}

function timeoutSignal(timeoutMs = defaultTimeoutMs): ManagedRequestSignal {
    const abortSignal = AbortSignal as AbortSignalStatic;
    if (abortSignal.timeout) {
        return { signal: abortSignal.timeout(timeoutMs), cleanup: () => {} };
    }
    const controller = new AbortController();
    const timer = window.setTimeout(() => controller.abort(), timeoutMs);
    return {
        signal: controller.signal,
        cleanup: () => window.clearTimeout(timer),
    };
}

export function managedRequestSignal(
    init?: ApiRequestOptions,
): ManagedRequestSignal {
    const timeout = timeoutSignal(init?.timeoutMs);
    const merged = mergeSignals(timeout.signal, init?.signal);
    return {
        signal: merged.signal,
        cleanup: () => {
            merged.cleanup();
            timeout.cleanup();
        },
    };
}

function isAbortError(error: unknown): boolean {
    return error instanceof DOMException && error.name === 'AbortError';
}

function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === 'object' && value !== null;
}

function hasEnvelopeShape(value: unknown): value is ApiEnvelope<unknown> {
    return (
        isRecord(value) &&
        typeof value.ok === 'boolean' &&
        ('data' in value || 'error' in value || 'request_id' in value)
    );
}

function errorFromEnvelope(
    body: ApiEnvelope<unknown>,
    status = 0,
): ApiClientError {
    const error = body.error;
    if (isRecord(error)) {
        const code =
            typeof error.code === 'string' && error.code
                ? error.code
                : 'request_failed';
        const message =
            typeof error.message === 'string' && error.message
                ? error.message
                : code;
        return new ApiClientError({
            code,
            message,
            requestId: body.request_id || '',
            status,
        });
    }
    const message =
        typeof error === 'string' && error ? error : 'request_failed';
    return new ApiClientError({
        code: message,
        message,
        requestId: body.request_id || '',
        status,
    });
}

function unwrapEnvelope<T>(body: unknown, status = 0): T {
    if (!hasEnvelopeShape(body)) {
        return body as T;
    }
    if (!body.ok) {
        throw errorFromEnvelope(body, status);
    }
    return body.data as T;
}

async function readJsonBody(response: Response): Promise<unknown> {
    const text = await response.text();
    if (!text) {
        return undefined;
    }
    return JSON.parse(text) as unknown;
}

async function readResponseError(response: Response): Promise<ApiClientError> {
    try {
        const body = await readJsonBody(response);
        if (hasEnvelopeShape(body)) {
            return errorFromEnvelope(body, response.status);
        }
        if (isRecord(body)) {
            const error = body.error;
            if (typeof error === 'string' && error) {
                return new ApiClientError({
                    code: error,
                    message: error,
                    status: response.status,
                });
            }
            if (isRecord(error) && typeof error.message === 'string') {
                return new ApiClientError({
                    code:
                        typeof error.code === 'string'
                            ? error.code
                            : 'request_failed',
                    message: error.message,
                    status: response.status,
                });
            }
        }
    } catch {
        // Ignore JSON parse failures for error responses.
    }
    return new ApiClientError({
        code: 'http_error',
        message: `${response.status} ${response.statusText}`,
        status: response.status,
    });
}

export async function readError(response: Response): Promise<string> {
    const error = await readResponseError(response);
    return error.requestId
        ? `${error.message} (${error.code}, ${error.requestId})`
        : `${error.message} (${error.code})`;
}

async function handleRejectedResponse(response: Response): Promise<void> {
    if (response.status === 401) {
        dispatchAuthInvalid();
        return;
    }
    if (response.status !== 403) {
        return;
    }
    try {
        const body = await readJsonBody(response.clone());
        const error = hasEnvelopeShape(body)
            ? body.error
            : isRecord(body)
              ? body.error
              : null;
        const errorCode =
            isRecord(error) && typeof error.code === 'string'
                ? error.code
                : typeof error === 'string'
                  ? error
                  : '';
        if (errorCode === 'must_change_password') {
            dispatchMustChangePassword();
        }
    } catch {
        // Ignore malformed error bodies.
    }
}

async function sendRequest<TResponse>({
    allowMockFallback = false,
    body,
    fallback,
    init,
    method = 'GET',
    path,
    responseType,
}: SendRequestOptions<TResponse>): Promise<TResponse> {
    let response: Response;
    const request = managedRequestSignal(init);
    try {
        try {
            response = await fetch(path, {
                ...fetchOptions(init),
                method,
                headers: authHeaders(init),
                body,
                signal: request.signal,
            });
        } catch (error) {
            if (isAbortError(error)) {
                throw error;
            }
            if (
                useMockFallback &&
                (allowMockFallback || fallback !== undefined)
            ) {
                if (fallback !== undefined) {
                    return fallback;
                }
                return undefined as TResponse;
            }
            throw error;
        }
        if (!response.ok) {
            await handleRejectedResponse(response);
            if (
                useMockFallback &&
                (allowMockFallback || fallback !== undefined)
            ) {
                if (fallback !== undefined) {
                    return fallback;
                }
                return undefined as TResponse;
            }
            throw await readResponseError(response);
        }
        if (responseType === 'void') {
            const contentType = response.headers.get('Content-Type') || '';
            if (contentType.includes('application/json')) {
                const responseBody = await readJsonBody(response);
                if (responseBody !== undefined) {
                    unwrapEnvelope<unknown>(responseBody, response.status);
                }
            }
            return undefined as TResponse;
        }
        const responseBody = await readJsonBody(response);
        return unwrapEnvelope<TResponse>(responseBody, response.status);
    } finally {
        request.cleanup();
    }
}

export async function requestJson<T>(
    path: string,
    fallback: T,
    init?: ApiRequestOptions,
): Promise<T> {
    return sendRequest<T>({
        fallback,
        init,
        path,
        responseType: 'json',
    });
}

export async function postJson<TRequest, TResponse>(
    path: string,
    value: TRequest,
    fallback: TResponse,
    init?: ApiRequestOptions,
): Promise<TResponse> {
    return sendRequest<TResponse>({
        body: JSON.stringify(value),
        fallback,
        init,
        method: 'POST',
        path,
        responseType: 'json',
    });
}

export async function putJson<T>(
    path: string,
    value: T,
    init?: ApiRequestOptions,
): Promise<void> {
    await sendRequest<void>({
        allowMockFallback: true,
        body: JSON.stringify(value),
        fallback: undefined,
        init,
        method: 'PUT',
        path,
        responseType: 'void',
    });
}

export async function deleteJson<TResponse>(
    path: string,
    fallback: TResponse,
    init?: ApiRequestOptions,
): Promise<TResponse> {
    return sendRequest<TResponse>({
        fallback,
        init,
        method: 'DELETE',
        path,
        responseType: 'json',
    });
}

export async function uploadBinary<TResponse>({
    body,
    fallback,
    path,
    contentType = 'application/octet-stream',
    init,
}: {
    body: BodyInit;
    fallback: TResponse;
    path: string;
    contentType?: string;
    init?: ApiRequestOptions;
}): Promise<TResponse> {
    const headers = new Headers(init?.headers);
    headers.set('Content-Type', contentType);
    return sendRequest<TResponse>({
        body,
        fallback,
        init: { ...init, headers },
        method: 'POST',
        path,
        responseType: 'json',
    });
}

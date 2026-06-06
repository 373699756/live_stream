// Core HTTP fetch utilities. Business API functions live in domain-specific
// modules (video.ts, image.ts, network.ts, system.ts, stream.ts).

import {
  dispatchAuthInvalid,
  dispatchMustChangePassword,
  getToken,
} from './authSession';

const baseHeaders = { 'Content-Type': 'application/json' };
const defaultTimeoutMs = 8000;

export const useMockFallback = import.meta.env.DEV;

export type ApiRequestOptions = RequestInit & {
  timeoutMs?: number;
};

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
  includeToken = false,
}: {
  entries?: Record<string, string>;
  includeToken?: boolean;
} = {}): string {
  const params = new URLSearchParams();
  const token = getToken();
  if (includeToken && token) {
    params.set('access_token', token);
  }
  if (entries) {
    Object.entries(entries).forEach(([key, value]) => params.set(key, value));
  }
  return params.toString();
}

export function authHeaders(init?: RequestInit): HeadersInit {
  const headers = new Headers(baseHeaders);
  const token = getToken();
  if (token) {
    headers.set('Authorization', `Bearer ${token}`);
  }
  if (init?.headers) {
    new Headers(init.headers).forEach((value, key) => headers.set(key, value));
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

export async function readError(response: Response): Promise<string> {
  try {
    const body = (await response.json()) as { error?: string };
    if (body.error) {
      return body.error;
    }
  } catch {
    // Ignore JSON parse failures for error responses.
  }
  return `${response.status} ${response.statusText}`;
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
    const body = (await response.clone().json()) as { error?: string };
    if (body.error === 'must_change_password') {
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
      if (useMockFallback && (allowMockFallback || fallback !== undefined)) {
        if (fallback !== undefined) {
          return fallback;
        }
        return undefined as TResponse;
      }
      throw error;
    }
    if (!response.ok) {
      await handleRejectedResponse(response);
      if (useMockFallback && (allowMockFallback || fallback !== undefined)) {
        if (fallback !== undefined) {
          return fallback;
        }
        return undefined as TResponse;
      }
      throw new Error(await readError(response));
    }
    if (responseType === 'void') {
      return undefined as TResponse;
    }
    return (await response.json()) as TResponse;
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

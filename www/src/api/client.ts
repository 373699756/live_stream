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

export function authQuery(entries?: Record<string, string>): string {
  const params = new URLSearchParams();
  if (entries) {
    Object.entries(entries).forEach(([key, value]) => params.set(key, value));
  }
  return params.toString();
}

export function authHeaders(init?: RequestInit): HeadersInit {
  const token = getToken();
  return {
    ...baseHeaders,
    ...(token ? { Authorization: `Bearer ${token}` } : {}),
    ...(init?.headers || {}),
  };
}

function mergeSignals(
  timeoutSignal: AbortSignal,
  requestSignal?: AbortSignal | null,
): AbortSignal {
  if (!requestSignal) {
    return timeoutSignal;
  }
  const abortSignal = AbortSignal as AbortSignalStatic;
  if (abortSignal.any) {
    return abortSignal.any([requestSignal, timeoutSignal]);
  }
  const controller = new AbortController();
  const abort = () => controller.abort();
  if (requestSignal.aborted || timeoutSignal.aborted) {
    controller.abort();
    return controller.signal;
  }
  requestSignal.addEventListener('abort', abort, { once: true });
  timeoutSignal.addEventListener('abort', abort, { once: true });
  return controller.signal;
}

function timeoutSignal(timeoutMs = defaultTimeoutMs): AbortSignal {
  const abortSignal = AbortSignal as AbortSignalStatic;
  if (abortSignal.timeout) {
    return abortSignal.timeout(timeoutMs);
  }
  const controller = new AbortController();
  window.setTimeout(() => controller.abort(), timeoutMs);
  return controller.signal;
}

export function requestSignal(init?: ApiRequestOptions): AbortSignal {
  return mergeSignals(timeoutSignal(init?.timeoutMs), init?.signal);
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

export async function requestJson<T>(
  path: string,
  fallback: T,
  init?: ApiRequestOptions,
): Promise<T> {
  let response: Response;
  try {
    response = await fetch(path, {
      ...fetchOptions(init),
      headers: authHeaders(init),
      signal: requestSignal(init),
    });
  } catch (error) {
    if (isAbortError(error)) {
      throw error;
    }
    if (useMockFallback) {
      return fallback;
    }
    throw error;
  }
  if (!response.ok) {
    await handleRejectedResponse(response);
    throw new Error(await readError(response));
  }
  return (await response.json()) as T;
}

export async function postJson<TRequest, TResponse>(
  path: string,
  value: TRequest,
  fallback: TResponse,
  init?: ApiRequestOptions,
): Promise<TResponse> {
  let response: Response;
  try {
    response = await fetch(path, {
      ...fetchOptions(init),
      method: 'POST',
      headers: authHeaders(init),
      body: JSON.stringify(value),
      signal: requestSignal(init),
    });
  } catch (error) {
    if (isAbortError(error)) {
      throw error;
    }
    if (useMockFallback) {
      return fallback;
    }
    throw error;
  }
  if (!response.ok) {
    await handleRejectedResponse(response);
    throw new Error(await readError(response));
  }
  return (await response.json()) as TResponse;
}

export async function putJson<T>(
  path: string,
  value: T,
  init?: ApiRequestOptions,
): Promise<void> {
  let response: Response;
  try {
    response = await fetch(path, {
      ...fetchOptions(init),
      method: 'PUT',
      headers: authHeaders(init),
      body: JSON.stringify(value),
      signal: requestSignal(init),
    });
  } catch (error) {
    if (isAbortError(error)) {
      throw error;
    }
    if (useMockFallback) {
      return;
    }
    throw error;
  }
  if (!response.ok) {
    await handleRejectedResponse(response);
    throw new Error(await readError(response));
  }
}

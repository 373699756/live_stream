// Core HTTP fetch utilities. Business API functions live in domain-specific
// modules (video.ts, image.ts, network.ts, system.ts, stream.ts).

const baseHeaders = { 'Content-Type': 'application/json' };
const tokenKey = 'live_stream_token';
const authInvalidEvent = 'live-stream-auth-invalid';
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

// ---------------------------------------------------------------------------
// Auth token helpers
// ---------------------------------------------------------------------------

export function getToken(): string | null {
  return window.localStorage.getItem(tokenKey);
}

export function hasToken(): boolean {
  return Boolean(window.localStorage.getItem(tokenKey));
}

function setToken(token: string): void {
  window.localStorage.setItem(tokenKey, token);
}

export function removeToken(): void {
  window.localStorage.removeItem(tokenKey);
}

function handleUnauthorized(): void {
  removeToken();
  window.dispatchEvent(new Event(authInvalidEvent));
}

export function onAuthInvalid(listener: () => void): () => void {
  window.addEventListener(authInvalidEvent, listener);
  return () => window.removeEventListener(authInvalidEvent, listener);
}

// ---------------------------------------------------------------------------
// Low-level fetch wrappers (exported for domain modules to use)
// ---------------------------------------------------------------------------

export function authQuery(entries?: Record<string, string>): string {
  const params = new URLSearchParams();
  const token = getToken();
  if (token) {
    params.set('token', token);
  }
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

function requestSignal(init?: ApiRequestOptions): AbortSignal {
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
    if (response.status === 401) {
      handleUnauthorized();
    }
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
    if (response.status === 401) {
      handleUnauthorized();
    }
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
  if (response.status === 401) {
    handleUnauthorized();
  }
  if (!response.ok) {
    throw new Error(await readError(response));
  }
}

// ---------------------------------------------------------------------------
// Auth API (kept here because it manages the token lifecycle)
// ---------------------------------------------------------------------------

export async function login(userName: string, password: string): Promise<boolean> {
  removeToken();
  try {
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      headers: baseHeaders,
      body: JSON.stringify({ user_name: userName, password }),
    });
    if (!response.ok) {
      return false;
    }
    const body = (await response.json()) as { token?: string };
    if (!body.token) {
      return false;
    }
    setToken(body.token);
    return true;
  } catch {
    return false;
  }
}

export async function validateSession(): Promise<boolean> {
  if (!hasToken()) {
    return false;
  }
  try {
    const response = await fetch('/api/auth/me', {
      method: 'GET',
      headers: authHeaders(),
    });
    if (!response.ok) {
      removeToken();
      return false;
    }
    return true;
  } catch {
    if (useMockFallback) {
      return hasToken();
    }
    removeToken();
    window.dispatchEvent(new Event(authInvalidEvent));
    return false;
  }
}

export async function logout(): Promise<void> {
  if (!hasToken()) {
    return;
  }
  try {
    const response = await fetch('/api/auth/logout', {
      method: 'POST',
      headers: authHeaders(),
    });
    if (response.status === 401) {
      handleUnauthorized();
    }
  } catch {
    // Local logout still clears the browser session if the device is offline.
  }
  removeToken();
}

// ---------------------------------------------------------------------------
// URL helpers (used by pages and components directly)
// ---------------------------------------------------------------------------

export function snapshotUrl(stream: string, tick = 0): string {
  const query = authQuery(tick > 0 ? { t: String(tick) } : undefined);
  return `/api/snapshot/${stream}.jpg${query ? `?${query}` : ''}`;
}

export function operationsExportUrl(): string {
  const query = authQuery();
  return `/api/operations/export${query ? `?${query}` : ''}`;
}

export function hlsPlaylistUrl(stream: string): string {
  const query = authQuery();
  return `/api/hls/${stream}/index.m3u8${query ? `?${query}` : ''}`;
}

export function flvStreamUrl(stream: string): string {
  const query = authQuery();
  return `/api/flv/${stream}.flv${query ? `?${query}` : ''}`;
}

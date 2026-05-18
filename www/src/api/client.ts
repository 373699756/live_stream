// Core HTTP fetch utilities. Business API functions live in domain-specific
// modules (video.ts, image.ts, network.ts, system.ts, stream.ts).

const baseHeaders = { 'Content-Type': 'application/json' };
const tokenKey = 'live_stream_token';

export const useMockFallback = import.meta.env.DEV;

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

function removeToken(): void {
  window.localStorage.removeItem(tokenKey);
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

export async function requestJson<T>(path: string, fallback: T, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    ...init,
    headers: authHeaders(init),
  });
  if (!response.ok) {
    if (useMockFallback) {
      return fallback;
    }
    throw new Error(await readError(response));
  }
  return (await response.json()) as T;
}

export async function postJson<TRequest, TResponse>(
  path: string,
  value: TRequest,
  fallback: TResponse,
): Promise<TResponse> {
  const response = await fetch(path, {
    method: 'POST',
    headers: authHeaders(),
    body: JSON.stringify(value),
  });
  if (!response.ok) {
    if (useMockFallback) {
      return fallback;
    }
    throw new Error(await readError(response));
  }
  return (await response.json()) as TResponse;
}

export async function putJson<T>(path: string, value: T): Promise<boolean> {
  const response = await fetch(path, {
    method: 'PUT',
    headers: authHeaders(),
    body: JSON.stringify(value),
  });
  return response.ok;
}

// ---------------------------------------------------------------------------
// Auth API (kept here because it manages the token lifecycle)
// ---------------------------------------------------------------------------

export async function login(userName: string, password: string): Promise<boolean> {
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

export function logout(): void {
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

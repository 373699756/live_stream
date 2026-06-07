import {
  authHeaders,
  type ApiEnvelope,
  managedRequestSignal,
  readError,
  useMockFallback,
} from './client';
import {
  dispatchAuthInvalid,
  hasToken,
  removeToken,
  setToken,
} from './authSession';
import type { AuthPrincipal, AuthState } from './types';

interface AuthResponse {
  token?: string;
  expires_at_ms?: number;
  principal?: AuthPrincipal;
  must_change_password?: boolean;
}

function unwrapAuthResponse(body: AuthResponse | ApiEnvelope<AuthResponse>): AuthResponse {
  if ('ok' in body && typeof body.ok === 'boolean') {
    return (body.data || {}) as AuthResponse;
  }
  return body as AuthResponse;
}

export interface LoginResult extends AuthState {
  error?: string;
}

function stateFromAuthResponse(body: AuthResponse): AuthState {
  const mustChangePassword =
    Boolean(body.must_change_password) ||
    Boolean(body.principal?.must_change_password);
  return {
    authenticated: true,
    mustChangePassword,
    principal: body.principal,
  };
}

export async function login(
  userName: string,
  password: string,
): Promise<LoginResult> {
  removeToken();
  const request = managedRequestSignal();
  try {
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      headers: authHeaders(),
      body: JSON.stringify({ user_name: userName, password }),
      signal: request.signal,
    });
    if (!response.ok) {
      return {
        authenticated: false,
        mustChangePassword: false,
        error: await readError(response),
      };
    }
    const body = unwrapAuthResponse(
      (await response.json()) as AuthResponse | ApiEnvelope<AuthResponse>,
    );
    if (!body.token) {
      return {
        authenticated: false,
        mustChangePassword: false,
        error: 'empty_token',
      };
    }
    setToken(body.token);
    return stateFromAuthResponse(body);
  } catch {
    return {
      authenticated: false,
      mustChangePassword: false,
      error: 'network_error',
    };
  } finally {
    request.cleanup();
  }
}

export async function validateSession(): Promise<AuthState> {
  if (!hasToken()) {
    return { authenticated: false, mustChangePassword: false };
  }
  const request = managedRequestSignal();
  try {
    const response = await fetch('/api/auth/me', {
      method: 'GET',
      headers: authHeaders(),
      signal: request.signal,
    });
    if (!response.ok) {
      removeToken();
      return { authenticated: false, mustChangePassword: false };
    }
    const body = unwrapAuthResponse(
      (await response.json()) as AuthResponse | ApiEnvelope<AuthResponse>,
    );
    return stateFromAuthResponse(body);
  } catch {
    if (useMockFallback) {
      return { authenticated: hasToken(), mustChangePassword: false };
    }
    dispatchAuthInvalid();
    return { authenticated: false, mustChangePassword: false };
  } finally {
    request.cleanup();
  }
}

export async function changePassword(
  oldPassword: string,
  newPassword: string,
): Promise<boolean> {
  if (!hasToken()) {
    return false;
  }
  const request = managedRequestSignal();
  try {
    const response = await fetch('/api/auth/change-password', {
      method: 'POST',
      headers: authHeaders(),
      body: JSON.stringify({
        old_password: oldPassword,
        new_password: newPassword,
      }),
      signal: request.signal,
    });
    if (response.status === 401) {
      dispatchAuthInvalid();
      return false;
    }
    return response.ok;
  } catch {
    return false;
  } finally {
    request.cleanup();
  }
}

export async function logout(): Promise<void> {
  if (!hasToken()) {
    return;
  }
  const request = managedRequestSignal();
  try {
    const response = await fetch('/api/auth/logout', {
      method: 'POST',
      headers: authHeaders(),
      signal: request.signal,
    });
    if (response.status === 401) {
      dispatchAuthInvalid();
    }
  } catch {
    // Local logout still clears the browser session if the device is offline.
  } finally {
    request.cleanup();
  }
  removeToken();
}

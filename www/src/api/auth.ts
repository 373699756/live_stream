import {
  authHeaders,
  readError,
  requestSignal,
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
  try {
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      headers: authHeaders(),
      body: JSON.stringify({ user_name: userName, password }),
      signal: requestSignal(),
    });
    if (!response.ok) {
      return {
        authenticated: false,
        mustChangePassword: false,
        error: await readError(response),
      };
    }
    const body = (await response.json()) as AuthResponse;
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
  }
}

export async function validateSession(): Promise<AuthState> {
  if (!hasToken()) {
    return { authenticated: false, mustChangePassword: false };
  }
  try {
    const response = await fetch('/api/auth/me', {
      method: 'GET',
      headers: authHeaders(),
      signal: requestSignal(),
    });
    if (!response.ok) {
      removeToken();
      return { authenticated: false, mustChangePassword: false };
    }
    const body = (await response.json()) as AuthResponse;
    return stateFromAuthResponse(body);
  } catch {
    if (useMockFallback) {
      return { authenticated: hasToken(), mustChangePassword: false };
    }
    dispatchAuthInvalid();
    return { authenticated: false, mustChangePassword: false };
  }
}

export async function changePassword(
  oldPassword: string,
  newPassword: string,
): Promise<boolean> {
  if (!hasToken()) {
    return false;
  }
  try {
    const response = await fetch('/api/auth/change-password', {
      method: 'POST',
      headers: authHeaders(),
      body: JSON.stringify({
        old_password: oldPassword,
        new_password: newPassword,
      }),
      signal: requestSignal(),
    });
    if (response.status === 401) {
      dispatchAuthInvalid();
      return false;
    }
    return response.ok;
  } catch {
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
      signal: requestSignal(),
    });
    if (response.status === 401) {
      dispatchAuthInvalid();
    }
  } catch {
    // Local logout still clears the browser session if the device is offline.
  }
  removeToken();
}

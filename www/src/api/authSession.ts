const tokenKey = 'live_stream_token';
export const authInvalidEvent = 'live-stream-auth-invalid';
export const mustChangePasswordEvent = 'live-stream-must-change-password';

export function getToken(): string | null {
  return window.localStorage.getItem(tokenKey);
}

export function hasToken(): boolean {
  return Boolean(window.localStorage.getItem(tokenKey));
}

export function setToken(token: string): void {
  window.localStorage.setItem(tokenKey, token);
}

export function removeToken(): void {
  window.localStorage.removeItem(tokenKey);
}

export function dispatchAuthInvalid(): void {
  removeToken();
  window.dispatchEvent(new Event(authInvalidEvent));
}

export function dispatchMustChangePassword(): void {
  window.dispatchEvent(new Event(mustChangePasswordEvent));
}

export function onAuthInvalid(listener: () => void): () => void {
  window.addEventListener(authInvalidEvent, listener);
  return () => window.removeEventListener(authInvalidEvent, listener);
}

export function onMustChangePassword(listener: () => void): () => void {
  window.addEventListener(mustChangePasswordEvent, listener);
  return () => window.removeEventListener(mustChangePasswordEvent, listener);
}

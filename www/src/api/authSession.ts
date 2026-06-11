export const authInvalidEvent = 'live-stream-auth-invalid';
export const mustChangePasswordEvent = 'live-stream-must-change-password';

export function clearBrowserAuthState(): void {
    window.localStorage.removeItem('live_stream_token');
}

export function dispatchAuthInvalid(): void {
    clearBrowserAuthState();
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

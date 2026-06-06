export interface CurrentRef<T> {
  current: T;
}

export interface PreviewSessionControls {
  isCurrentSession: () => boolean;
  sessionSignal: AbortSignal;
  setConnected: (value: boolean) => void;
  setDecodedSize: (value: string) => void;
  setPreviewState: (value: string) => void;
  updateDisplaySize: () => void;
}

export function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

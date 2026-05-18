interface Window {
  Hls?: {
    new (config?: Record<string, unknown>): {
      attachMedia: (media: HTMLMediaElement) => void;
      destroy: () => void;
      loadSource: (url: string) => void;
      on?: (event: string, listener: (...args: unknown[]) => void) => void;
    };
    isSupported?: () => boolean;
    Events?: Record<string, string>;
  };
  mpegts?: {
    Events?: Record<string, string>;
    createPlayer: (config: Record<string, unknown>) => {
      attachMediaElement: (media: HTMLMediaElement) => void;
      destroy: () => void;
      detachMediaElement?: () => void;
      load: () => void;
      on?: (event: string, listener: (...args: unknown[]) => void) => void;
      play: () => Promise<void>;
      unload?: () => void;
    };
    isSupported?: () => boolean;
  };
  flvjs?: Window['mpegts'];
}

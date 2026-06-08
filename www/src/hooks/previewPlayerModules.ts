type HlsConstructor = NonNullable<Window['Hls']>;
type FlvModule = NonNullable<Window['mpegts']>;

export type HlsPlayer = InstanceType<HlsConstructor>;
export type FlvPlayer = ReturnType<FlvModule['createPlayer']>;

export class PlayerScriptLoadError extends Error {
  constructor(src: string) {
    super(`load failed: ${src}`);
    this.name = 'PlayerScriptLoadError';
  }
}

export class PlayerModuleUnavailableError extends Error {
  constructor(name: string) {
    super(`${name} module unavailable`);
    this.name = 'PlayerModuleUnavailableError';
  }
}

const scriptLoads = new Map<string, Promise<void>>();

function loadScriptOnce(src: string): Promise<void> {
  const existingLoad = scriptLoads.get(src);
  if (existingLoad) {
    return existingLoad;
  }
  const load = new Promise<void>((resolve, reject) => {
    const existing = document.querySelector<HTMLScriptElement>(
      `script[src="${src}"]`,
    );
    if (existing?.dataset.loaded === 'true') {
      resolve();
      return;
    }
    const script = existing || document.createElement('script');
    script.src = src;
    script.async = true;
    script.onload = () => {
      script.dataset.loaded = 'true';
      resolve();
    };
    script.onerror = () => {
      script.remove();
      reject(new PlayerScriptLoadError(src));
    };
    if (!existing) {
      document.head.appendChild(script);
    }
  });
  load.catch(() => {
    scriptLoads.delete(src);
  });
  scriptLoads.set(src, load);
  return load;
}

export async function loadLocalHlsModule(): Promise<HlsConstructor> {
  if (window.Hls) {
    return window.Hls;
  }
  await loadScriptOnce('/vendor/hls.min.js');
  if (!window.Hls) {
    throw new PlayerModuleUnavailableError('HLS');
  }
  return window.Hls;
}

export async function loadLocalFlvModule(): Promise<FlvModule> {
  const current = window.mpegts || window.flvjs;
  if (current) {
    return current;
  }
  await loadScriptOnce('/vendor/flv.min.js');
  const module = window.mpegts || window.flvjs;
  if (!module) {
    throw new PlayerModuleUnavailableError('HTTP-FLV');
  }
  return module;
}

export function destroyHls(player: HlsPlayer | null) {
  if (!player) {
    return;
  }
  try {
    player.destroy();
  } catch {
    // Best-effort cleanup.
  }
}

export function destroyFlv(player: FlvPlayer | null) {
  if (!player) {
    return;
  }
  try {
    player.unload?.();
    player.detachMediaElement?.();
    player.destroy();
  } catch {
    // Best-effort cleanup.
  }
}

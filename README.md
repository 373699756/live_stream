# live_stream

HiSilicon IPC video live-preview service.

## Scope

- Video-only live preview.
- Snapshot capture.
- Device configuration.
- Operations and maintenance Web Console.

Out of scope: audio capture/encoding/transport, recording, storage playback,
and related UI/API.

## Layout

- `app/`: process entry and service composition root.
- `libs/*_service/`: backend service modules.
- `configs/`: runtime JSON configuration.
- `www/`: Vite + React + TypeScript IPC Web Console.
- `docs/`: architecture, contracts, AI workflow, and focused engineering notes.

## Build

```sh
make -j2
```

Frontend:

```sh
cd www
npm run build
```

## Documentation

Start from [docs/README.md](docs/README.md). For AI-assisted work, read
[AGENTS.md](AGENTS.md) first, then the short files under `docs/active/`.

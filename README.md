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
- `libs/<module>/`: backend modules.
- `configs/`: runtime JSON configuration.
- `www/`: Vite + React + TypeScript IPC Web Console.
- `docs/`: architecture, contracts, AI workflow, and focused engineering notes.

## Build

```sh
make -j2
```

The default `make` target creates a directly copyable debug tree:

```text
debug/
  bin/
  configs/
  log/
  web/
```

Copy those entries to the board under `/mnt/live_stream`, then start the
program with `/mnt/live_stream` as the working directory. Runtime paths stay
relative: `configs/`, `web/`, and `log/`.

Release upgrade packages are generated separately:

```sh
UPGRADE_SIGN_KEY=/path/to/private_key.pem \
  make release RELEASE_VERSION=1.2.3 RELEASE_PROFILE=web-only
```

Release files are written under `release/`. The default release profile is
`web-only`, so Web Console publishing can update only the web partition.
The packaging logic is split into `scripts/package_debug.sh` and
`scripts/package_release.sh`.

Frontend:

```sh
cd www
npm run build
```

## Documentation

Start from [docs/README.md](docs/README.md). For AI-assisted work, read
[AGENTS.md](AGENTS.md) first, then [docs/README.md](docs/README.md) and the
module document that owns the change.

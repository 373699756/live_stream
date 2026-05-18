# live_stream Architecture

This document describes the current runtime architecture and the intended module boundaries for the IPC live-stream application.

## Product scope

The product scope is fixed to:

- Video-only live preview.
- Snapshots.
- Device configuration.
- Operations and maintenance management.

The product intentionally does not implement audio capture, audio encoding, audio transport, recording, storage playback, or related UI/API surfaces. Existing configuration fields that mention audio or recording are compatibility placeholders unless explicitly guarded as disabled.

## Top-level layout

```text
app/
  Process entry point and composition root. Owns service creation, dependency wiring, startup order, and shutdown order.

libs/*_service/
  Service modules. Each module should expose a small public API in include/ and keep implementation details in src/.

libs/common/
  Shared low-level data types and utilities.

libs/infra_service/
  Process/runtime infrastructure such as logging, filesystem helpers, time helpers, and executors.

configs/
  Runtime JSON configuration and authentication user store.

www/
  Vite + React + TypeScript IPC Web Console. It consumes HTTP APIs and does not own device SDK parsing logic.
```

## Runtime startup order

`app/main.cpp` performs process initialization, installs signal handlers, and starts `AppRuntime`.

`AppRuntime::Start()` currently starts subsystems in this order:

1. `CoreServices`
2. `DeviceSubsystem`
3. `MediaSubsystem`
4. `ProtocolSubsystem`

Shutdown runs in reverse order:

1. `ProtocolSubsystem`
2. `MediaSubsystem`
3. `DeviceSubsystem`
4. `CoreServices`

This order is important because protocol services expose HTTP/RTSP/ONVIF/WebRTC entry points and depend on already-started core, device, and media services.

## Current subsystem graph

```text
main.cpp
  -> AppRuntime
       -> CoreServices
            - LoggerService
            - ConfigService
            - EventService
            - AuthService
       -> DeviceSubsystem
            - SystemService + LinuxSystemPlatform
            - TimeService + LinuxTimePlatform
            - NetworkService + LinuxNetworkPlatform
            - AlarmService
            - UpgradeService + LinuxUpgradePlatform
       -> MediaSubsystem
            - MediaService
            - AiService, only when ai.enabled is true
            - OsdService
            - SnapshotService
       -> ProtocolSubsystem
            - infra::Executor for network callbacks
            - NetEngine
            - RtspService
            - WebrtcService
            - StreamHubService
            - OnvifService + IOnvifUriProvider
            - HttpService
```

## Core services

Source files:

- `app/core_services.h`
- `app/core_services.cpp`

Responsibilities:

- Start the logger first so later startup failures can be recorded.
- Start `ConfigService` from `configs/business_config.json` with `configs/default_config.json` as defaults.
- Install product-scope guards, including rejecting enabled audio config.
- Start `EventService`.
- Start `AuthService` and connect auth audit records to operation logging.

Core services should not depend on device, media, protocol, or frontend-specific code.

## Device subsystem

Source files:

- `app/device_subsystem.h`
- `app/device_subsystem.cpp`
- `app/device_platforms.h`
- `app/linux_*_platform.cpp`
- `app/platform_factory.h`
- `app/platform_factory.cpp`

Responsibilities:

- Create platform adapters for Linux-backed system, time, network, and upgrade operations via `CreateLinuxPlatformAdapters()`.
- Accept a `PlatformAdapters` struct from `AppRuntime` at startup.
- Start device management services:
  - `SystemService`
  - `TimeService`
  - `NetworkService`
  - `AlarmService`
  - `UpgradeService`

The default network interface is read from `network.default_ifname` in the runtime config (falls back to `"eth0"` if absent). It is no longer hard-coded in `DeviceSubsystem`.

## Media subsystem

Source files:

- `app/media_subsystem.h`
- `app/media_subsystem.cpp`

Responsibilities:

- Start the video media pipeline.
- Start AI only when `ai.enabled` is true.
- Start OSD and snapshot services after media is available.
- Expose `MediaRefs` (holding concrete service pointers) to the app composition root.

Cross-module consumers (`HttpService`, `RtspService`, `OnvifService`) receive narrow view interfaces
(`IMediaView`, `IAiView`, `ISnapshotView`) rather than concrete class pointers. The interfaces are
defined in each service's own header.

## Protocol subsystem

Source files:

- `app/protocol_subsystem.h`
- `app/protocol_subsystem.cpp`

Responsibilities:

- Own the shared network engine and callback executor.
- Start externally visible protocol services:
  - RTSP
  - WebRTC
  - stream hub
  - ONVIF
  - HTTP
- Create the ONVIF URI provider that maps stream IDs to RTSP and snapshot URLs.

The protocol subsystem depends on already-started core, device, and media services.

HTTP route handlers are split by business domain into `src/handlers/*.cpp.inc` files
(auth, media, system, time, network, upgrade, AI, snapshot, stream, config, operations).
Each handler fragment is `#include`d into `HttpServiceImpl`'s private section.
`HttpServiceDependencies` uses narrow view interfaces (`IMediaView`, `IAiView`, `ISnapshotView`)
for the three cross-module dependencies, and concrete interface pointers for all others.

## HTTP/Web Console boundary

The Web Console in `www/` consumes backend HTTP APIs. It does not parse HiSilicon SDK settings or own runtime configuration semantics.

Current web API summary lives in:

- `www/README.md`

When backend API fields change, update both:

- Backend handler/DTO code.
- Frontend TypeScript DTOs in `www/src/api/types.ts`.
- Contract documentation in `docs/api-config-contract.md`.

## Configuration files and runtime paths

`app/main.cpp` resolves runtime paths in this priority order:

1. `--config-dir <dir>` CLI argument — relocates all `configs/*` paths under `<dir>`.
2. `LIVE_STREAM_CONFIG_DIR` environment variable — same effect; CLI takes precedence.
3. Default relative paths (from the process working directory):
   - Business config: `configs/business_config.json`
   - Default config: `configs/default_config.json`
   - Auth users: `configs/auth_users.json`
   - Operation log: `build/runtime/operation.log`

The operation log path is not affected by `--config-dir`.

The full configuration and HTTP API contract is documented in `docs/api-config-contract.md`.

## Dependency direction rules

Allowed direction:

```text
app -> libs/*_service -> libs/common / libs/infra_service
www -> HTTP API only
```

Disallowed direction:

```text
libs/*_service -> app
libs/*_service -> www
www -> device SDK / backend config internals
```

Services should receive dependencies through options/dependency structs, constructors, or factory functions. Services should not reach into global subsystem singletons.

## Known architecture debt

- Event payload ownership and naming conventions are not yet documented.
- `HttpServiceDependencies` is still a wide struct; further decomposition would require converting the `.cpp.inc` handler fragments into independently-constructed `IHttpHandler` classes.

## Resolved architecture debt

The following items were previously listed as debt and have been addressed:

- **Platform adapter construction**: centralized in `app/platform_factory.h` / `platform_factory.cpp` via `CreateLinuxPlatformAdapters()`.
- **`eth0` hard-coding**: network interface name is now read from `network.default_ifname` in the runtime config.
- **Concrete class cross-module dependencies**: `HttpService`, `RtspService`, and `OnvifService` now use narrow view interfaces (`IMediaView`, `IAiView`, `ISnapshotView`) for cross-module media references.
- **HTTP handler decomposition**: route handlers split into 11 domain files under `libs/http_service/src/handlers/`.
- **Runtime configuration sources**: paths are configurable via `--config-dir` or `LIVE_STREAM_CONFIG_DIR`; contract documented in `docs/api-config-contract.md`.
- **Frontend API layer**: `www/src/api/client.ts` is now a thin HTTP layer; domain-specific API functions live in `video.ts`, `image.ts`, `network.ts`, `system.ts`, `stream.ts`; auth state is managed via `AuthContext`.

## Recommended evolution order

1. Convert `src/handlers/*.cpp.inc` fragments into true `IHttpHandler` implementations if `HttpServiceDependencies` further decomposition becomes necessary.
2. Document event payload ownership and naming conventions.
3. Add `compile_commands.json` generation (via `bear` or `compiledb`) for IDE/clangd support with the cross-compiler.

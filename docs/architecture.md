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

Responsibilities:

- Create platform adapters for Linux-backed system, time, network, and upgrade operations.
- Start device management services:
  - `SystemService`
  - `TimeService`
  - `NetworkService`
  - `AlarmService`
  - `UpgradeService`

Current technical debt:

- The default network interface is hard-coded as `eth0` in `DeviceSubsystem`.
- Linux platform creation is spread across `app/linux_*_platform.cpp` and wired directly in the subsystem.

Preferred direction:

- Add an app-level platform factory when platform variation becomes necessary.
- Keep platform-specific code out of service modules unless the service owns that platform abstraction.

## Media subsystem

Source files:

- `app/media_subsystem.h`
- `app/media_subsystem.cpp`

Responsibilities:

- Start the video media pipeline.
- Start AI only when `ai.enabled` is true.
- Start OSD and snapshot services after media is available.

Current technical debt:

- Cross-module dependencies currently use concrete classes for several media services, such as `MediaService`, `AiService`, and `SnapshotService`.

Preferred direction:

- Keep the media pipeline stable.
- Add interfaces only for cross-module capabilities that are consumed outside the media module.

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

Current technical debt:

- This subsystem is the widest composition point.
- HTTP service dependency wiring is especially wide because HTTP routes touch many business services.

Preferred direction:

- Keep `ProtocolSubsystem` as the composition point, but move option/dependency mapping into small helper functions.
- Split HTTP business route handlers by domain while preserving API paths and DTOs.

## HTTP/Web Console boundary

The Web Console in `www/` consumes backend HTTP APIs. It does not parse HiSilicon SDK settings or own runtime configuration semantics.

Current web API summary lives in:

- `www/README.md`

When backend API fields change, update both:

- Backend handler/DTO code.
- Frontend TypeScript DTOs in `www/src/api/types.ts`.
- Contract documentation in `docs/api-config-contract.md`.

## Configuration files and runtime paths

`app/main.cpp` currently uses relative runtime paths:

- Business config: `configs/business_config.json`
- Default config: `configs/default_config.json`
- Auth users: `configs/auth_users.json`
- Operation log: `build/runtime/operation.log`

Because these are relative paths, startup assumes the process runs from the repository/runtime root or an equivalent packaged working directory.

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

- `HttpServiceDependencies` is broad and makes HTTP service a business API aggregation point.
- Runtime configuration has multiple sources of truth: JSON config, service option structs, runtime config parsing, and frontend TypeScript types.
- Platform adapter construction is not yet centralized.
- Some cross-module dependencies use concrete classes instead of narrow interfaces.
- Event payload ownership and naming conventions are not yet documented.

## Recommended evolution order

1. Document service boundaries and API/config contracts.
2. Refactor `app/protocol_subsystem.cpp` into smaller builder helpers without changing public APIs.
3. Split HTTP handlers by domain while preserving existing API behavior.
4. Introduce platform factory if host/board/platform variation becomes a recurring need.
5. Add interfaces for cross-module media/snapshot/AI dependencies only where they reduce coupling.

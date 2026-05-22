# Module Boundaries

This document records the intended responsibilities and dependency boundaries for live_stream service modules.

## General service rules

Each `libs/*_service` module should:

- Expose a small public API under `include/`.
- Keep implementation details under `src/`.
- Reuse `libs/service_rules.mk` through the module `Makefile`/`module.mk` pattern.
- Accept collaborators through `Options` and `Dependencies` structs or explicit constructor parameters.
- Avoid depending on `app/`, `www/`, or concrete platform details unless the service owns a platform abstraction.
- Return `bool` for action-style success/failure, consistent with project conventions.
- Avoid exceptions and RTTI.

The app layer is the composition root. Services should not discover each other through app singletons.

## Core infrastructure and common code

### `libs/common`

Owns:

- Shared data types such as config JSON wrappers and request context.
- Lightweight utilities that are not tied to a specific service.

Must not own:

- Runtime service lifecycle.
- Platform-specific behavior.
- HTTP API routing.

### `libs/infra_service`

Owns:

- Logging.
- Filesystem helpers.
- Time helpers.
- Executor primitives.

Must not own:

- Product business logic.
- Device SDK details.
- Service orchestration.

## Core services

### `libs/config_service`

Owns:

- Loading/storing JSON config scopes.
- Config attachment validate/apply callbacks.
- Default config fallback behavior.

Should not own:

- Runtime startup policy beyond config access.
- Frontend DTO formatting.
- Device SDK application logic, except through service-provided attachment callbacks.

Notes:

- Compatibility fields for unsupported product areas may exist in JSON, but services must enforce product scope. Audio must remain disabled.

### `libs/logger_service`

Owns:

- Operation log records.
- Operation log query/export.
- File-backed operation log storage.

Should not own:

- General process logging; that belongs to `infra::Log`.
- Auth decisions; it only records auth audit results passed from auth integration.

### `libs/event_service`

Owns:

- Publish/subscribe event dispatch inside the process.

Should not own:

- Persistent event storage.
- Business decision logic.

Payload and naming convention:

- See `docs/contracts/event-payloads.md` for event names, payload ownership, publishers,
  subscribers, and reserved event contracts.

### `libs/auth_service`

Owns:

- Login/logout/session/token validation.
- Password verification through injected verifier.
- Auth audit sink callbacks.

Should not own:

- HTTP routing.
- Operation log storage.
- Frontend session state.

## Device management services

### `libs/system_service`

Owns:

- System/device status queries.
- Device management actions exposed by `ISystemPlatform`.

Should not own:

- HTTP response formatting.
- Upgrade workflow details.

### `libs/time_service`

Owns:

- Time and NTP configuration/application through `ITimePlatform`.

Should not own:

- Network interface management.
- Browser time formatting.

### `libs/network_service`

Owns:

- Network interface config/status.
- Applying network config through `INetworkPlatform`.

Should not own:

- HTTP/RTSP/ONVIF port ownership decisions beyond its config scope.
- Global advertised host policy unless the config contract defines it.

Current issue:

- The app currently creates a Linux network platform for `eth0`. If devices can use other interface names, this must become config-driven with `eth0` as fallback.

### `libs/alarm_service`

Owns:

- Alarm rule config and alarm status/action decisions within the video-only product scope.

Should not own:

- Recording, storage playback, or audio behavior.

### `libs/upgrade_service`

Owns:

- Upgrade package validation and upgrade workflow state.
- Platform-specific upgrade actions through `IUpgradePlatform`.

Should not own:

- HTTP upload transport details.
- Frontend progress rendering.

## Media services

### `libs/media_service`

Owns:

- Video media pipeline lifecycle.
- Stream start/stop/status.
- Frame source/sink integration.
- HiSilicon SDK integration through its SDK abstraction.

Should not own:

- RTSP/HTTP/WebRTC protocol logic.
- Web Console DTOs.
- Audio pipeline functionality.

Boundary note:

- `media_service.h` exposes `IMediaService`. Media status, capabilities,
  hardware channel metadata, key-frame requests, and encoded-frame subscription
  live on that interface. Frame subscription types are simple data/callback
  contracts, not a separate source interface. The concrete media pipeline
  implementation stays private to `media_service.cpp`.

### `libs/snapshot_service`

Owns:

- Snapshot capture policy and JPEG snapshot retrieval through media.

Should not own:

- HTTP path routing.
- Static file serving.

### `libs/osd_service`

Owns:

- OSD config validation/application.
- OSD region interaction with media/MPP adapter.

Should not own:

- Frontend form layout or TypeScript DTO ownership.

### `libs/ai_service`

Owns:

- Optional AI inference configuration and runtime integration when enabled.

Should not own:

- Core video pipeline availability.
- Product behavior when AI is disabled; disabled AI should be a normal state.

Open question:

- Decide whether AI is a core product feature or optional/experimental. If optional, APIs must clearly report disabled/unavailable state.

## Protocol and streaming services

### `libs/net_service`

Owns:

- Shared network engine.
- TCP/UDP primitives.
- Event loop and callback dispatch integration.

Should not own:

- HTTP, RTSP, ONVIF, or WebRTC business semantics.

### `libs/rtsp_service`

Owns:

- RTSP protocol handling.
- RTSP sessions and auth integration.
- Pulling video frames from media through the declared dependency.

Should not own:

- WebRTC signaling.
- HTTP API routing.
- ONVIF device metadata.

### `libs/webrtc_service`

Owns:

- WebRTC peer/session model.
- SDP/ICE handling.
- WebRTC media transport integration.

Should not own:

- HTTP signaling endpoints directly if those endpoints can be kept in the HTTP/API layer.
- RTSP/ONVIF metadata.

### `libs/stream_hub_service`

Owns:

- HTTP live stream fanout/adaptation such as HLS/FLV stream state, if implemented there.
- Media-to-stream consumers coordination.

Should not own:

- HTTP request parsing.
- Browser-specific UI logic.

### `libs/onvif_service`

Owns:

- ONVIF discovery/device/media service behavior.
- ONVIF auth integration.
- ONVIF XML/SOAP/HTTP protocol responses.

Should not own:

- Internal RTSP session state.
- Direct construction of stream URLs outside `OnvifService`.

### `libs/http_service`

Owns:

- HTTP server behavior.
- Request parsing and response serialization.
- Static file serving.
- API routing and auth middleware at the HTTP boundary.

Current issue:

- `HttpServiceDependencies` currently includes most business services, which makes HTTP service the widest aggregation point.

Preferred direction:

- Split business API handlers by domain.
- Preserve existing API paths, status codes, and JSON fields.
- Keep HTTP protocol mechanics separate from domain-specific DTO mapping.

Potential internal handler groups:

- Auth API.
- Config API.
- Media/snapshot API.
- Stream status/live transport API.
- System/network/time/alarm/upgrade API.
- Operation log API.
- WebRTC signaling API.

## Frontend boundary

### `www/`

Owns:

- IPC/NVR-style Web Console UI.
- Page-level state and form interactions.
- TypeScript DTOs for consumed backend APIs.
- Development mock fallback.

Should not own:

- Device SDK parsing.
- Backend config semantics beyond documented API fields.
- Audio/recording/playback UI.

When adding or changing an API:

1. Update backend handler and DTO mapping.
2. Update `www/src/api/types.ts` and client functions.
3. Update `www/README.md` endpoint list when paths change.
4. Update `docs/contracts/api-config.md` for schema/field ownership.

## Cross-module dependency checklist

Before adding a dependency from one service to another, check:

- Is this dependency necessary at runtime, or can the app layer compose the behavior?
- Can the dependency be expressed as a narrow interface?
- Does the dependency create a cycle?
- Is the owner of each config scope still clear?
- Does the dependency pull protocol/UI concerns into a business service?

## Explicitly forbidden scope creep

Do not add or re-enable:

- Audio capture.
- Audio encoding.
- Audio transport.
- Recording.
- Storage playback.
- Recording/playback UI or APIs.

If compatibility config contains such fields, services must reject enabled unsupported behavior or ignore disabled placeholders safely.

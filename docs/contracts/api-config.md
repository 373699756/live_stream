# API and Configuration Contract

This document records the current backend configuration scopes and Web Console API contract. It is intentionally descriptive: it does not change existing JSON schemas, API paths, or DTOs.

## Contract ownership rules

- `configs/default_config.json` and `configs/business_config.json` provide configuration scopes.
- `ConfigService` owns storage, default fallback, and validate/apply attachment dispatch.
- Each business service owns validation and application of its own config scope.
- `app/runtime_config.cpp` should read only process startup values required to wire protocol services and runtime paths.
- `www/src/api/types.ts` should describe fields consumed by the Web Console, not hidden SDK internals.
- `www/README.md` lists public HTTP paths consumed by the Web Console.

## Runtime path contract

`app/main.cpp` resolves paths using this priority order:

1. **CLI argument** `--config-dir <dir>`: relocates all `configs/*` paths under `<dir>`.
2. **Environment variable** `LIVE_STREAM_CONFIG_DIR=<dir>`: same effect as CLI arg; CLI takes precedence.
3. **Default relative paths** (resolved from the process working directory):

| Purpose | Path |
| --- | --- |
| Business config | `configs/business_config.json` |
| Default config | `configs/default_config.json` |
| Auth users | `configs/auth_users.json` |
| Operation log | `log/operation.log` |

The operation log path is not affected by `--config-dir` or `LIVE_STREAM_CONFIG_DIR`; it defaults to `log/operation.log` relative to the working directory. Production startup with `--config-dir /config` maps it to `/data/operation.log`.

Debug deployments should run with `/mnt/live_stream` as the working directory so `configs/`, `web/`, and `log/` resolve under that mount.

## Configuration scopes

### `video`

Owner:

- `MediaService`
- Runtime codec bridge in `app/runtime_config.cpp` for RTSP options
- Web UI video configuration page

Current fields:

- `streams.main.enabled`
- `streams.main.codec`
- `streams.main.resolution`
- `streams.main.fps`
- `streams.main.bitrate_kbps`
- `streams.main.rate_control`
- `streams.main.gop`
- `streams.main.gop_mode`
- `streams.main.smart_codec`
- Same fields under `streams.sub`

Runtime startup usage:

- `app/runtime_config.cpp` reads `streams.main.codec` and `streams.sub.codec` to set RTSP stream codecs.

Frontend DTO:

- `VideoConfig`
- `VideoStreamConfig`
- `MediaCapabilities`

Notes:

- Keep stream names `main` and `sub` stable unless all protocol paths and frontend assumptions are migrated together.
- `smart_codec=true` maps the stream GOP mode to HiSilicon SmartP at runtime. It is only valid for H.264/H.265 streams and requires `GET /api/media/capabilities` to report `streams.<name>.smart_codec=true`.

### `audio`

Owner:

- Product-scope guard in `CoreServices`

Current fields are compatibility placeholders.

Contract:

- `enabled` must remain `false`.
- Enabling audio is rejected because audio capture, audio encoding, and audio transport are out of scope.

### `ai`

Owner:

- `AiService`
- `MediaSubsystem` dependency injection

Current fields:

- `enabled`
- `backend`
- `task`
- `stream`
- `model_path`
- `input_width`
- `input_height`
- `inference_interval_ms`
- `confidence_threshold`
- `max_results`

Runtime startup usage:

- `MediaSubsystem` always creates `AiService`. `ai.enabled=false` keeps the
  service attached to config/status only and does not start inference.
- `task` accepts `object_detection`, `face_detection`,
  `motion_classification`, and `occlusion_detection`. Device-side
  `motion_classification` and `occlusion_detection` do not require a `.wk`
  `model_path`.
- `PUT /api/config/ai` hot-applies runtime changes by stopping or rebuilding
  the AI inference path.

Runtime API:

- `GET /api/ai/status` returns `config`, `stats`, and `last_result`.
- `stats` includes backend/alarm linkage, frame counters, last/max/average
  inference time in milliseconds, active result count, and last success/failure
  wall-clock timestamps.
- `GET /api/ai/alerts` returns `{ "items": [...] }` for recent AI alert
  snapshots.
- `GET /api/ai/alerts/{id}/image` returns the JPEG image for one alert.

Current product status:

- AI is an optional experimental capability and is disabled by default.
- The default NNIE object-detection model is `models/inst_ssd_cycle.wk` with
  300x300 input, `stream=sub`, and `inference_interval_ms=500`. Deploy optional
  model assets under the runtime `models/` directory when AI is enabled.
- Web alarm display is currently an AI snapshot waterfall. AI detections can
  inject an `ai_detection` input into `alarm_service`, but this still does not
  imply recording, playback, or long-term storage.

### `image`

Owner:

- Image/media configuration path
- Web UI image configuration page

Current fields:

- `basic.brightness`
- `basic.contrast`
- `basic.saturation`
- `basic.sharpness`
- `basic.hue`
- `exposure.*`
- `white_balance.*`
- `enhancement.*`
- `backlight.*`
- `orientation.mirror`
- `orientation.flip`
- `color_mode.mode`

Frontend DTO:

- `ImageConfig`
- `ImageCapabilities`

Runtime mapping:

- `basic.brightness`, `basic.contrast`, `basic.saturation`, and `basic.hue` map to HiSilicon ISP CSC attributes.
- `basic.sharpness` maps to HiSilicon ISP sharpen attributes.
- `exposure.max_exposure_time` maps to the ISP AE exposure-time upper bound.
- `backlight.mode=drc` and `backlight.level` map to ISP DRC enable and strength. Full sensor WDR requires VI/MIPI/sensor startup-mode support and is not exposed as a runtime image toggle.
- `color_mode.mode=color/black_white` maps to ISP CSC saturation.
- `strategy.mode` supports `balanced`, `low_noise`, and `detail` in the media-service ISO strategy loop.

### `rtsp`

Owner:

- `RtspService`
- Runtime protocol wiring in `ProtocolSubsystem`

Current fields:

- `enabled`
- `port`
- `auth_required`
- `paths.main`
- `paths.sub`
- `max_sessions`
- `session_timeout_sec`

Runtime startup usage:

- `app/runtime_config.cpp` reads `port`, `auth_required`, and `max_sessions`.
- `ProtocolSubsystem` maps those fields into `RtspServiceOptions`.

Frontend DTO:

- `RtspConfig`

Notes:

- `paths.main` and `paths.sub` must stay aligned with ONVIF URI generation and Web Console stream links.

### `webrtc`

Owner:

- `WebrtcService`
- HTTP WebRTC signaling API

Current fields:

- `enabled`
- `local_port_base`
- `public_ip`
- `signaling_path`
- `ice_servers[]`
- `max_peers`
- `prefer_tcp`

Runtime startup usage:

- `app/runtime_config.cpp` reads `enabled`, `prefer_tcp`, `local_port_base`, `max_peers`, `public_ip`, and `ice_servers`.
- `public_ip` is validated for schema compatibility, but advertised WebRTC addresses use the unified runtime advertise host from `network.advertise_ip`.
- `ProtocolSubsystem` maps runtime fields into `WebrtcServiceOptions`.

Frontend DTO:

- `WebrtcConfig`

Known mismatch:

- Frontend `WebrtcConfig` currently omits `local_port_base` and `public_ip` even though runtime config reads them.
- Decide whether those fields are user-editable, runtime-only, or hidden advanced settings before changing the UI.

### `http`

Owner:

- `HttpService`
- `ProtocolSubsystem` protocol wiring

Current fields:

- `port`
- `static_root`

Runtime startup usage:

- `app/runtime_config.cpp` reads both fields.
- `ProtocolSubsystem` maps them into `HttpServiceOptions`.

Notes:

- `static_root` is relative to the process working directory unless changed by deployment packaging.

### `onvif`

Owner:

- `OnvifService`

Current fields:

- `enabled`
- `device_service_port`
- `discovery_port`
- `discovery_enabled`
- `auth_required`
- `advertise_ip`
- `manufacturer`
- `model`
- `firmware_version`

Runtime startup usage:

- `app/runtime_config.cpp` reads ports, discovery flag, auth flag, advertise IP, and device metadata.
- `advertise_ip` is validated for schema compatibility, but ONVIF advertised addresses use the unified runtime advertise host from `network.advertise_ip`.
- `ProtocolSubsystem` maps these into `OnvifServiceOptions`.

Advertise policy:

- `network.advertise_ip` is the single startup source for `AppRuntimeConfig::advertise_host`.
- `webrtc.public_ip` and `onvif.advertise_ip` remain accepted config fields for compatibility, but no longer overwrite the runtime advertise host.

### `snapshot`

Owner:

- `SnapshotService`
- HTTP snapshot endpoints
- ONVIF snapshot URI generation

Current fields:

- `enabled`
- `main_path`
- `sub_path`
- `jpeg_quality`
- `timeout_ms`

Runtime startup usage:

- `app/runtime_config.cpp` reads `main_path` and `sub_path` for URI generation.

Frontend DTO:

- `SnapshotConfig`

### `network`

Owner:

- `NetworkService`
- Web UI network configuration page

Current fields:

- `hostname`
- `advertise_ip`
- `default_ifname` — primary network interface name (e.g. `"eth0"`). Read at startup by `app/runtime_config.cpp` and passed to `CreateLinuxPlatformAdapters()`. Defaults to `"eth0"` if absent.
- `interfaces.<ifname>.enabled`
- `interfaces.<ifname>.dhcp`
- `interfaces.<ifname>.static_ipv4.address`
- `interfaces.<ifname>.static_ipv4.netmask`
- `interfaces.<ifname>.static_ipv4.gateway`
- `interfaces.<ifname>.dns[]`
- `ports.http`
- `ports.https`
- `ports.rtsp`
- `ports.onvif`

Runtime startup usage:

- `app/runtime_config.cpp` reads `advertise_ip` and `default_ifname`.
- `network.ports` remains part of the config schema for UI/device network settings, but protocol startup ports are read from their protocol owners: `http.port`, `rtsp.port`, and `onvif.device_service_port`.

Known issue:

- `DeviceSubsystem` receives `network_ifname` from `AppRuntimeConfig` (read from `network.default_ifname` in config, defaulting to `"eth0"`). To use a different interface, set `network.default_ifname` in `business_config.json`.

### `time`

Owner:

- `TimeService`

Current fields:

- `timezone`
- `ntp.enabled`
- `ntp.servers[]`
- `ntp.sync_interval_sec`
- `manual_sync_allowed`

### `overlay`

Owner:

- `RegionService`
- Web UI Overlay configuration page

Current fields:

- `enabled`
- `items.timestamp.enabled`
- `items.timestamp.format`
- `items.timestamp.x`
- `items.timestamp.y`
- `items.device_name.enabled`
- `items.device_name.text`
- `items.device_name.x`
- `items.device_name.y`
- `font_size`
- `font_color`
- `background`
- `privacy_masks.main[]`
- `privacy_masks.sub[]`
- `privacy_masks.*[].enabled`
- `privacy_masks.*[].x`
- `privacy_masks.*[].y`
- `privacy_masks.*[].width`
- `privacy_masks.*[].height`
- `privacy_masks.*[].color`

Frontend DTO:

- `OverlayConfig`

### `alarm`

Owner:

- `AlarmService`

Current fields:

- `motion_detection.enabled`
- `motion_detection.sensitivity`
- `motion_detection.min_duration_ms`
- `motion_detection.regions[]`
- `ai_detection.enabled`
- `ai_detection.sensitivity`
- `ai_detection.min_duration_ms`
- `ai_detection.regions[]`
- `actions.snapshot`
- `actions.record`
- `actions.notify`
- `schedule.mode`
- `schedule.weekly[]`

Contract:

- `actions.record` is a compatibility placeholder and must not enable recording because recording/playback is out of product scope.

### `user`

Owner:

- `AuthService` for session/auth policy
- `ConfigService` owns loading and saving `configs/auth_users.json`
- `AuthService` consumes the auth user store interface for login and password changes

Current fields:

- `accounts[]`
- `password_policy.min_length`
- `password_policy.require_number`
- `password_policy.require_symbol`
- `password_policy.lockout_failures`
- `password_policy.lockout_seconds`
- `session.token_ttl_seconds`
- `session.max_sessions_per_user`

Current runtime note:

- `CoreServices` currently hardcodes `token_ttl_seconds = 30 * 60` and `max_sessions = 16` in `AuthServiceOptions`, while JSON contains session policy fields. This should be reconciled before exposing session policy editing.
- `configs/auth_users.json` stores only `password_credential`; `password` and `password_plaintext` fields are rejected. The factory `admin` account may start with `must_change_password=true` so the Web Console forces a password change after the first `admin/admin` login.

### `system`

Owner:

- `SystemService`
- Web UI system page

Current fields:

- `device_name`
- `location`
- `language`
- `auto_reboot.enabled`
- `auto_reboot.time`
- `auto_reboot.days[]`
- `factory_reset_requires_admin`

Frontend DTO:

- `SystemStatus`

### `log`

Owner:

- Process logging policy if/when config-driven logging is enabled

Current fields:

- `level`
- `console_output`
- `file_output`
- `path`
- `max_file_size_bytes`
- `max_files`
- `async_write`

Current runtime note:

- `main.cpp` currently initializes `infra::Log` with hardcoded info-level console logging and synchronous writes.

### `logger`

Owner:

- `LoggerService` operation log behavior

Current fields:

- `operation_log_path`
- `max_file_size_bytes`
- `max_files`
- `export_limit`
- `mask_sensitive_fields`

Current runtime note:

- `CoreServices` currently receives operation log path from `RuntimePaths`, not from the JSON `logger.operation_log_path` field.

## HTTP API paths consumed by Web Console

Auth:

- `POST /api/auth/login`
- `POST /api/auth/logout`
- `POST /api/auth/change-password`
- `GET /api/auth/me`

`POST /api/auth/login` and `GET /api/auth/me` return `must_change_password`.
When it is true, authenticated management APIs are rejected until
`POST /api/auth/change-password` succeeds.
RTSP and ONVIF Basic authentication also rejects users while
`must_change_password` is true.

Config:

- `GET/PUT /api/config/video`
- `GET/PUT /api/config/image`
- `GET/PUT /api/config/overlay`
- `GET/PUT /api/config/network`
- `GET/PUT /api/config/snapshot`

Media and live preview:

- `GET /api/media/capabilities`
- `GET /api/status/streams`
- `GET /api/snapshot/main.jpg`
- `GET /api/snapshot/sub.jpg`
- `GET /api/hls/main/index.m3u8`
- `GET /api/hls/sub/index.m3u8`
- `GET /api/flv/main.flv`
- `GET /api/flv/sub.flv`
- `GET /api/mjpeg/main.mjpg`
- `GET /api/mjpeg/sub.mjpg`

`GET /api/status/streams` exposes runtime playback readiness per stream:
`browserCodec`, `hlsSupported`, `hlsReady`, `flvSupported`, `flvReady`,
`mjpegSupported`, `mjpegReady`, and `webrtcReady`.

System and maintenance:

- `GET /api/system/status`
- `POST /api/upgrade/upload?filename=<name>`
- `GET /api/upgrade/status`
- `POST /api/upgrade/start`
- `POST /api/upgrade/cancel`
- `POST /api/upgrade/confirm-reboot`
- `GET /api/operations`
- `GET /api/operations/export`

WebRTC signaling:

- `POST /api/webrtc/peers`
- `POST /api/webrtc/offer`
- `POST /api/webrtc/candidate`
- `POST /api/webrtc/close`

## Compatibility and migration rules

When changing config or API fields:

1. Keep existing fields backward compatible where possible.
2. If a field is deprecated, keep reading it for at least one migration window and document the replacement.
3. Update backend validation and application code together.
4. Update frontend DTOs and mock data together.
5. Update this document and `www/README.md` when paths or public DTOs change.
6. Do not use config compatibility placeholders to add out-of-scope audio or recording features.

# IPC Web Console

This directory contains the IPC browser UI.

## Stack

- Vite
- React
- TypeScript
- Plain CSS with CSS variables

The UI intentionally follows an IPC/NVR management-console style: dense forms,
dark live-preview area, compact left navigation, and clear operational states.

## Development

```sh
npm install
npm run dev
```

The Vite dev server proxies `/api` to `http://127.0.0.1:8080` by default.
Override it with:

```sh
IPC_API_TARGET=http://device-ip:80 npm run dev
```

## Build

```sh
npm run build
```

The static output is generated under `dist/`. On the device, configure
`http_service` with `static_root` pointing to the deployed `dist` directory.

## Backend Contract

The web UI does not parse or own device SDK settings. It calls HTTP APIs:

- `POST /api/auth/login`
- `POST /api/auth/logout`
- `POST /api/auth/change-password`
- `GET /api/auth/me`
- `GET/PUT /api/config/video`
- `GET/PUT /api/config/image`
- `GET/PUT /api/config/overlay`
- `GET/PUT /api/config/network`
- `GET/PUT /api/config/snapshot`
- `GET /api/media/capabilities`
- `GET /api/ai/status`
- `GET /api/ai/alerts`
- `GET /api/ai/alerts/{id}/image`
- `GET /api/system/status`
- `POST /api/upgrade/upload?filename=<name>`
- `GET /api/upgrade/status`
- `POST /api/upgrade/start`
- `POST /api/upgrade/cancel`
- `POST /api/upgrade/confirm-reboot`
- `GET /api/status/streams`
- `GET /api/snapshot/main.jpg`
- `GET /api/snapshot/sub.jpg`
- `GET /api/hls/main/index.m3u8`
- `GET /api/hls/sub/index.m3u8`
- `GET /api/flv/main.flv`
- `GET /api/flv/sub.flv`
- `GET /api/mjpeg/main.mjpg`
- `GET /api/mjpeg/sub.mjpg`
- `GET /api/operations`
- `GET /api/operations/export`
- `POST /api/webrtc/peers`
- `POST /api/webrtc/offer`
- `POST /api/webrtc/candidate`
- `POST /api/webrtc/close`

Product scope is video-only live preview, snapshots, configuration, and
maintenance. Audio capture, audio encoding, audio transport, recording,
storage playback, and related UI/API surfaces are intentionally out of scope.
Existing config fields that mention audio or recording are compatibility
placeholders and must not be treated as enabled product features.

The web live preview now exposes four modes:

- `WebRTC` for low-latency preview
- `HLS` for browser-compatible segmented playback
- `HTTP-FLV` for continuous live playback on MSE-capable browsers
- `MJPEG` for multipart JPEG streams
- `snapshot` for still-image capture

WebRTC, RTSP, HLS, HTTP-FLV, and MJPEG are video-only paths. HLS, HTTP-FLV, and
WebRTC support `H.264` and `H.265` video-only streams; `H.265` playback still
depends on the browser and hardware decoder exposed to `hls.js`, `mpegts.js`,
or the WebRTC implementation. MJPEG preview uses `/api/mjpeg/{stream}.mjpg`
when the active stream codec is `mjpeg`.

`GET /api/media/capabilities` includes `streams.<name>.available` so the UI can
hide or disable stream configuration that the current firmware does not start.
It also exposes `streams.<name>.smart_codec`; when enabled, video config
`streams.<name>.smart_codec=true` is saved and applied as HiSilicon SmartP GOP
mode for H.264/H.265 streams.
`GET /api/status/streams` is the runtime source of truth for stream access
links, including `hlsSupported/hlsReady`, `flvSupported/flvReady`,
`mjpegSupported/mjpegReady`, and `webrtcReady`.

Image capabilities expose only runtime-supported ISP controls. Current image
runtime mappings include CSC brightness/contrast/saturation/hue, sharpen,
AE maximum exposure time, DRC backlight strength, color/black-white mode, and
the automatic strategy modes `balanced`, `low_noise`, and `detail`.

AI is an optional device capability and is disabled by default. The current Web
alarm surface is the AI alert image waterfall backed by `/api/ai/alerts`; it is
not recording, playback, or long-term storage.
`/api/ai/status` also exposes backend availability, alarm linkage, last/max/
average inference time, and last success/failure timestamps for board-side
validation.
The default device AI model path is `models/inst_ssd_cycle.wk` with 300x300
input, sub-stream inference, and a 500 ms interval. Deploy optional model assets
under `/mnt/live_stream/models/` when AI is enabled.

When the backend is not available, the frontend uses local mock data so layout
and interaction work during UI development.

Auth users are loaded and saved by `config_service` from
`configs/auth_users.json` with hashed `password_credential` values only. The
factory `admin/admin` login is allowed only as an initial setup path when
`must_change_password` is returned by the auth API; the UI then forces
`POST /api/auth/change-password` before showing the management console.
RTSP and ONVIF authentication reject the factory password until that password
change succeeds.

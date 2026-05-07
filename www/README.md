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
- `GET /api/auth/me`
- `GET/PUT /api/config/video`
- `GET/PUT /api/config/image`
- `GET/PUT /api/config/osd`
- `GET/PUT /api/config/network`
- `GET/PUT /api/config/snapshot`
- `GET /api/media/capabilities`
- `GET /api/system/status`
- `GET /api/status/streams`
- `GET /api/snapshot/main.jpg`
- `GET /api/snapshot/sub.jpg`
- `GET /api/operations`
- `GET /api/operations/export`
- `POST /api/webrtc/peers`
- `POST /api/webrtc/offer`
- `POST /api/webrtc/candidate`
- `POST /api/webrtc/close`

WebRTC is currently disabled by default in device configuration until the
backend is wired to a real WebRTC stack. The live view keeps snapshot preview
available as the default preview path.

`GET /api/media/capabilities` includes `streams.<name>.available` so the UI can
hide or disable stream configuration that the current firmware does not start.
`GET /api/status/streams` is the runtime source of truth for stream access
links.

When the backend is not available, the frontend uses local mock data so layout
and interaction work during UI development.

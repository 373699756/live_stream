# Event Payloads

This document defines the in-process `event_service` event catalog and the
payload contract for each event. The current API uses `EventType` enum values,
but each row also has a canonical event name for logs, bridges, and any future
string-based integration.

## Payload Envelope

`Event` is a lightweight metadata envelope:

| Field | Contract |
| --- | --- |
| `type` | `EventType` enum value. This is the dispatch key used by `IEventService::Subscribe()`. |
| `source` | Publishing service name. Use the service's canonical module name such as `time_service` or `rtsp_service`. |
| `target` | Optional subject of the event: interface name, stream id, peer address, alarm source, or config scope. Leave empty when the event has no single subject. |
| `message` | Optional short action, reason, state, or stable token. It is not JSON and must not contain credentials or large text. |
| `value` | Optional event-specific integer, such as progress percent or active flag. Use `0` when unused. |

The implementation rejects oversized strings: `source` is limited to 64 bytes,
`target` to 128 bytes, and `message` to 256 bytes. Events must not carry media
frames, binary payloads, credentials, or large JSON.

## Naming Rules

Use lower-case, dot-separated names:

```text
<domain>.<subject>.<action>
```

Examples:

- `config.video.changed`
- `stream.main.started`
- `network.interface.changed`
- `upgrade.progress.changed`

The domain should match the owning service or product area. The final segment
should describe what happened in past tense (`changed`, `started`, `stopped`,
`triggered`, `received`) unless the event reports a state stream such as
`upgrade.progress.changed`.

Do not encode unbounded values in the name. Put stream id, interface name, peer
address, or alarm source in `target`; put the short reason or state token in
`message`; put numeric state in `value`.

## Ownership Rules

- The publishing service owns the payload contract for its event type.
- Subscribers must rely only on fields documented here and must ignore unknown
  `message` tokens unless they explicitly own that publisher contract.
- A new publisher for an existing `EventType` must preserve the documented
  payload semantics.
- A new event must be added here before production code publishes it.
- `message` values should be stable tokens where possible. Human-readable
  details belong in service logs or operation audit records, not in event
  routing decisions.

## Event Catalog

Current production code has no `event_service` subscribers outside tests.
Subscriber rows therefore say `none` until a production module registers a
handler.

| EventType | Canonical name | Status | Publisher(s) | Subscriber(s) | Payload |
| --- | --- | --- | --- | --- | --- |
| `kConfigChanged` | `config.changed` | reserved | none | none | Intended for config scope changes. Use `source=<config publisher>`, `target=<config scope>`, `message=<change token>`, `value=0`. Prefer a more specific name such as `config.video.changed` when adding string bridges. |
| `kMediaPipelineStarted` | `media.pipeline.started` | reserved | none | none | Intended for media pipeline lifecycle. Use `source=media_service`, `target=<channel or stream id>`, `message=<reason or empty>`, `value=0`. |
| `kMediaPipelineStopped` | `media.pipeline.stopped` | reserved | none | none | Intended for media pipeline lifecycle. Use `source=media_service`, `target=<channel or stream id>`, `message=<reason or empty>`, `value=0`. |
| `kMediaPipelineError` | `media.pipeline.error` | reserved | none | none | Intended for media pipeline failures. Use `source=media_service`, `target=<channel or stream id>`, `message=<stable error token>`, `value=<optional error code>`. |
| `kStreamStarted` | `stream.started` | reserved | none | none | Intended for encoded stream availability. Use `source=<stream owner>`, `target=<stream id>`, `message=<reason or empty>`, `value=0`. For named streams, prefer canonical names such as `stream.main.started` or `stream.sub.started`. |
| `kStreamStopped` | `stream.stopped` | reserved | none | none | Intended for encoded stream availability. Use `source=<stream owner>`, `target=<stream id>`, `message=<reason or empty>`, `value=0`. For named streams, prefer canonical names such as `stream.main.stopped` or `stream.sub.stopped`. |
| `kRtspClientConnected` | `rtsp.client.connected` | active | `rtsp_service` | none | `source=rtsp_service`, `target=<peer ip>`, `message=""`, `value=0`. |
| `kRtspClientDisconnected` | `rtsp.client.disconnected` | active | `rtsp_service` | none | `source=rtsp_service`, `target=<peer ip>`, `message=""`, `value=0`. |
| `kWebRtcClientConnected` | `webrtc.client.connected` | reserved | none | none | Intended for WebRTC client session lifecycle. Use `source=webrtc_service`, `target=<peer or session id>`, `message=""`, `value=0`. |
| `kWebRtcClientDisconnected` | `webrtc.client.disconnected` | reserved | none | none | Intended for WebRTC client session lifecycle. Use `source=webrtc_service`, `target=<peer or session id>`, `message=<reason or empty>`, `value=0`. |
| `kOnvifRequestReceived` | `onvif.request.received` | active | `onvif_service` | none | `source=onvif_service`, `target=""`, `message=<ONVIF action name>`, `value=0`. Current action names are `GetDeviceInformation`, `GetSystemDateAndTime`, `SetSystemDateAndTime`, `GetProfiles`, `GetStreamUri`, `GetSnapshotUri`, and `Unknown`. |
| `kSnapshotCreated` | `snapshot.created` | reserved | none | none | Intended for successful snapshot creation. Use `source=snapshot_service`, `target=<channel or stream id>`, `message=<format or empty>`, `value=<byte size or 0>`. |
| `kTimeChanged` | `time.changed` | active | `time_service` | none | `source=time_service`, `target=""`, `message=<change reason>`, `value=0`. Current message tokens are `timezone`, `manual`, `ntp`, and `config`. |
| `kNetworkChanged` | `network.interface.changed` | active | `network_service` | none | `source=network_service`, `target=<ifname>`, `message=interface_config_changed`, `value=0`. |
| `kAlarmTriggered` | `alarm.triggered` | active | `alarm_service` | none | `source=alarm_service`, `target=<alarm source>`, `message=<alarm input message>`, `value=1`. Current alarm sources are `motion`, `io_input`, `tamper`, `network`, and `unknown`. |
| `kStorageStateChanged` | `storage.state.changed` | reserved | none | none | Intended for storage mount, health, or capacity state changes. Use `source=<storage owner>`, `target=<device or volume id>`, `message=<state token>`, `value=<percent or 0>`. |
| `kSystemStatusChanged` | `system.status.changed` | active | `system_service` | none | `source=system_service`, `target=""`, `message=<status token>`, `value=0`. Current messages are `reboot`, `factory_reset`, `heartbeat recovered`, and `heartbeat timeout: <component>`. |
| `kUpgradeProgressChanged` | `upgrade.progress.changed` | active | `upgrade_service` | none | `source=upgrade_service`, `target=""`, `message=<upgrade stage>`, `value=<progress percent 0..100>`. Current stage tokens are `idle`, `validating`, `preparing`, `writing`, `committing`, `waiting_reboot`, `completed`, `failed`, and `canceled`. |

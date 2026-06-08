# Board Hot Path Probe

板端热路径优化先采集数据，再决定是否减少拷贝或调整队列上限。固定入口是
`scripts/board_hot_path_probe.sh`，输出 CSV 和原始 HTTP diagnostics。

## Usage

在板端启动 `live_stream` 后运行：

```sh
scripts/board_hot_path_probe.sh \
    --base-url http://127.0.0.1:8080 \
    --stream main \
    --duration 180 \
    --interval 2 \
    --hls-clients 2 \
    --flv-clients 2 \
    --mjpeg-clients 1 \
    --user admin \
    --password '<password>'
```

脚本默认采集：

- `/proc/<pid>/status` 和 `/proc/stat`：RSS、VmHWM、进程 CPU、系统 CPU。
- `GET /api/media/streams`：`cached_bytes`、`hls_bytes`、reader/client 数、
  `last_reset_reason`。
- `GET /api/media/sessions`：pending bytes、活跃 FLV/MJPEG/RTSP/WebRTC 数、
  RTSP/WebRTC drop 计数。
- HLS playlist 和 snapshot 请求延迟，用作控制面/媒体入口延迟近似值。

输出目录默认是 `/tmp/live_stream_hot_path_<timestamp>`：

- `metrics.csv`：每个采样周期的聚合指标。
- `raw/*.json`：每次采样的原始 HTTP 响应。
- `clients.log`：脚本启动的客户端和跳过原因。
- `run.env`：采集参数。

## WebRTC

WebRTC 需要真实 SDP/ICE 客户端。脚本不内置伪客户端；需要时设置
`WEBRTC_CLIENT_CMD`，并指定 `--webrtc-clients N`。命令会拿到这些环境变量：
`BASE_URL`、`STREAM`、`WHEP_URL`、`COOKIE_JAR`、`AUTH_HEADER`、`CLIENT_INDEX`。

## Decision Gate

采集 HLS、FLV、MJPEG、WebRTC 多客户端数据后再做代码改动：

- `cached_bytes` 或 `hls_bytes` 随客户端数增长：先查 `media_source` cache 引用和 retain。
- `sessions_pending_bytes` 或 CPU 持续升高：先查 `net` send queue 和慢客户端关闭。
- `webrtc_dropped_frames` / `rtsp_dropped_frames` 增长：先查 fanout 和 RTP 发送路径。
- RSS/VmHWM 不回落：先查 HTTP session、media reader/client detach 和缓存引用释放。

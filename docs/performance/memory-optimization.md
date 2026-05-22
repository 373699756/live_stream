# Memory Optimization Plan

## Context

本项目是 HiSilicon 平台上的实时视频流服务器，核心热路径：

```text
hardware encoder
  -> EncodedFrame + VideoBuffer explicit refcount
  -> StreamHubService
  -> HLS / FLV distribution
```

整体框架已经收敛为 `IVideoBufferPool` + `VideoBuffer*` 零拷贝传帧，但
`stream_mux` 和 `stream_hub_stream_state` 仍有一些转封装层内存问题。

## Hot Spots

| File | Issue |
| --- | --- |
| `libs/stream_mux/src/stream_mux.cpp` | 临时 `std::string` / `std::vector<uint8_t>`、`AppendBytes`、RTP 分片 |
| `libs/stream_mux/include/stream_mux.h` | `RtpPacket` 结构和公开函数签名 |
| `libs/stream_hub_service/src/stream_hub_stream_state.h` | `ParsedFramePayload`、`HlsSegmentState::body` |
| `libs/stream_hub_service/src/stream_hub_stream_state.cpp` | `AppendFrameToStream`、H264/H265 输出构建 |
| `libs/stream_hub_service/src/stream_hub_service.cpp` | `ProcessFrame` 锁边界 string 拷贝 |

## Problems

1. `std::string` 被当字节容器使用。TS segment body、FLV tag、PES packet
   都可能因为 append 或 `+` 触发 realloc 和 memcpy。
2. RTP 分片中存在临时 `std::vector<uint8_t> fragment`，每个分片会产生
   一次临时分配和一次额外拷贝。
3. `BuildPesPacket`、`BuildPatPacket` 等返回 `std::string`，调用链中容易产生
   中间对象。
4. `ParsedFramePayload` 内含 `std::vector<H264NalUnit>` /
   `std::vector<H265NalUnit>`，可能每帧分配。
5. `PackagedFrameResult`、`ProcessFrame` 中的 `std::string` 出参跨锁边界时要避免
   无意义拷贝。
6. `AppendBytes` 使用逐字节 push，应改成块追加。

## P0: Remove Per-Frame Temporary Work

### 1. Replace byte-by-byte append

Target: `libs/stream_mux/src/stream_mux.cpp`

```cpp
void AppendBytes(std::string *out, const uint8_t *data, size_t size) {
  out->append(reinterpret_cast<const char *>(data), size);
}
```

### 2. Remove RTP fragment vectors

Target: `PacketizeH264()` and `PacketizeH265()`

Current issue: each fragmentation loop creates a temporary vector and then copies it
into RTP packet bytes.

Preferred direction: pass small FU headers and payload slices into a local
`SendRtpPacket()` helper that reserves final packet capacity once.

```cpp
void SendRtpPacket(const EncodedFrame &frame,
                   const uint8_t *header,
                   uint32_t header_size,
                   const uint8_t *payload,
                   uint32_t payload_size,
                   bool marker,
                   uint16_t *sequence,
                   uint32_t ssrc,
                   std::vector<RtpPacket> *packets);
```

### 3. Append TS/PES packets in place

Target: `BuildPesPacket()`, `BuildPatPacket()`, `BuildPmtPacket()`

Preferred direction: replace return-by-value packet builders with append-style helpers
that write into the segment body.

```cpp
void AppendPesPacket(const std::string &access_unit,
                     uint64_t pts_90k,
                     uint64_t dts_90k,
                     std::string *out);
```

### 4. Reserve HLS segment body

Target: `StartSegment()`

Reserve an estimated segment size when starting a segment. Start with a conservative
fixed estimate if no rolling bitrate estimate exists.

```cpp
stream->current_segment.body.reserve(2 * 1024 * 1024);
```

## P1: Reduce Cross-Lock Copies

### 5. Move packaged FLV outputs

Target: `ProcessFrame()`

Prefer moving `sequence_header_tag` and `flv_tag` out of `PackagedFrameResult`
when crossing the lock boundary.

```cpp
sequence_header_tag = std::move(packaged_frame.sequence_header_tag);
flv_tag = std::move(packaged_frame.flv_tag);
```

### 6. Avoid duplicate NAL scans

Target: H264/H265 output builders in `stream_hub_stream_state.cpp`

`BuildH264AvccSample()` and `BuildH264AnnexBAccessUnit()` are derived from the
same parsed NAL list. If profiling shows this path matters, combine both outputs
in one traversal.

## P2: Reduce ParsedFramePayload Allocations

Most frames contain a small number of NAL units. Options:

- Reserve a small fixed capacity before parsing.
- Reuse per-thread parse objects.
- Introduce a local small-vector only if profiling shows vector allocation cost is
  meaningful.

Do not add a third-party small-vector dependency for this alone.

## Execution Order

1. Replace `AppendBytes` implementation.
2. Move packaged FLV outputs across lock boundary.
3. Reserve HLS segment body.
4. Append TS/PES packets in place.
5. Remove RTP fragment vectors.
6. Combine duplicate NAL scans if still useful.
7. Consider small-vector style parsing only after profiling or board evidence.

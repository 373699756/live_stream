# Quality Report

本文档由 `scripts/quality_scan.sh quick` 生成，汇总代码质量、性能和设计候选问题。

- Generated: `20260522-025749`
- Raw log directory: `/home/c/linux/hisi/live_stream/reports/quality/20260522-025749`
- Git commit: `93db900`

## Counts

- cppcheck diagnostics: 0
- cppcheck errors: 0
- keyword risk hits: 229
- hot-path/logging hits: 195
- clang-tidy diagnostics: 0
- include-what-you-use findings: 0

## Must Check First

- No required step failed.

## Must Fix: Cppcheck Errors

_No findings in this category._

## Review: Cppcheck Warnings

_No findings in this category._

## Review: Clang-Tidy Diagnostics

_No log file generated._

## Review: Include-What-You-Use

_No log file generated._

## Optimization Candidates: Files With Most Keyword Risk Hits

```text
     29 libs/media_service/src/media_buffer_pool.cpp
     12 libs/hisi_vendor/src/hisi_mpp_venc.cpp
     11 www/src/hooks/usePreviewPlayer.ts
      9 www/src/api/client.ts
      9 libs/media_service/src/media_service.cpp
      8 libs/hisi_vendor/src/hisi_mpp_vi.cpp
      7 libs/hisi_vendor/src/hisi_mpp_snapshot.cpp
      6 libs/webrtc_service/src/webrtc_engine.cpp
      6 libs/stream_hub_service/src/stream_hub_service.cpp
      6 libs/hisi_vendor/src/hisi_mpp_region.cpp
      5 libs/snapshot_service/src/snapshot_service.cpp
      5 libs/network_service/src/network_service.cpp
      5 libs/media_service/src/media_buffer.cpp
      4 libs/time_service/src/time_service.cpp
      4 libs/infra_service/include/infra/executor.h
      4 libs/hisi_vendor/src/hisi_mpp_vpss.cpp
      4 libs/hisi_vendor/src/hisi_mpp_sys.cpp
      4 libs/ai_service/src/ai_service.cpp
      3 libs/webrtc_service/src/webrtc_service.cpp
      3 libs/upgrade_service/src/upgrade_service.cpp
```

## Optimization Candidates: Files With Most Hot-Path Or Logging Hits

```text
     26 libs/media_service/src/media_buffer_pool.cpp
     19 libs/media_service/src/media_service.cpp
     17 libs/hisi_vendor/src/hisi_mpp_venc.cpp
     16 libs/webrtc_service/src/webrtc_engine.cpp
     15 libs/stream_hub_service/src/stream_hub_service.cpp
     13 libs/rtsp_service/src/rtsp_service.cpp
      7 libs/stream_mux/src/stream_mux.cpp
      7 libs/media_service/include/media/encoded_frame.h
      7 libs/hisi_vendor/src/hisi_mpp_snapshot.cpp
      5 libs/webrtc_service/src/webrtc_service.cpp
      5 libs/stream_mux/include/stream_mux.h
      5 libs/stream_hub_service/src/stream_hub_stream_state.cpp
      5 libs/media_service/src/media_buffer.cpp
      4 libs/net_service/src/tcp_connection.cpp
      4 libs/net_service/src/net_engine_impl.cpp
      3 libs/net_service/include/net_service.h
      3 libs/media_service/include/media/media_buffer.h
      3 libs/media_service/include/media/frame_attach.h
      2 libs/stream_hub_service/src/stream_hub_stream_state.h
      2 libs/onvif_service/src/onvif_service.cpp
```

## Build Failure Tail

_No build failure pattern detected._

## How To Use This Report

1. 先处理 `Must Check First` 中的失败步骤。
2. 再处理 `Must Fix`，这些比关键词命中更可靠。
3. `Review` 是设计/生命周期风险，逐项确认是否真实影响业务。
4. `Optimization Candidates` 只列热点候选文件，具体行号到原始日志里追。
5. 原始工具日志只作证据，不作为主要阅读入口。

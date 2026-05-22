# Quality Report

本文档由 `scripts/quality_scan.sh full` 生成，汇总代码质量、性能和设计候选问题。

- Generated: `20260522-022057`
- Raw log directory: `/home/c/linux/hisi/live_stream/reports/quality/20260522-022057`
- Git commit: `3f9efb1`

## Counts

- cppcheck diagnostics: 12
- cppcheck errors: 1
- keyword risk hits: 229
- hot-path/logging hits: 195
- clang-tidy diagnostics: 0

## Must Check First

- No required step failed.
- Warning step: `scan-build-10: exit code 2`; inspect related log before trusting that tool result.
- Skipped: `code size: missing cloc or tokei`.
- Skipped: `include-what-you-use: missing include-what-you-use or iwyu`.

## Must Fix: Cppcheck Errors

```text
libs/stream_mux/src/stream_mux.cpp:309:68: error: Invalid out->resize() argument nr 2. The value is -1 but the valid values are '0:255'. [invalidFunctionArg]
```

## Review: Cppcheck Warnings

```text
libs/config_service/src/config_service.cpp:262:10: warning: Virtual function 'SaveFile' is called from destructor '~ConfigServiceImpl()' at line 134. Dynamic binding is not used. [virtualCallInConstructor]
libs/event_service/src/event_service.cpp:69:10: warning: Virtual function 'Stop' is called from destructor '~EventServiceImpl()' at line 35. Dynamic binding is not used. [virtualCallInConstructor]
libs/event_service/src/event_service.cpp:69:10: warning: Virtual function 'Stop' is called from destructor '~EventServiceImpl()' at line 36. Dynamic binding is not used. [virtualCallInConstructor]
libs/http_service/src/http_service_impl.h:32:10: warning: Virtual function 'Stop' is called from destructor '~HttpServiceImpl()' at line 42. Dynamic binding is not used. [virtualCallInConstructor]
libs/http_service/src/http_service_impl.h:32:10: warning: Virtual function 'Stop' is called from destructor '~HttpServiceImpl()' at line 43. Dynamic binding is not used. [virtualCallInConstructor]
libs/logger_service/src/logger_service.cpp:46:10: warning: Virtual function 'Stop' is called from destructor '~LoggerServiceImpl()' at line 18. Dynamic binding is not used. [virtualCallInConstructor]
libs/net_service/src/net_engine_impl.h:26:10: warning: Virtual function 'Stop' is called from destructor '~NetEngineImpl()' at line 16. Dynamic binding is not used. [virtualCallInConstructor]
libs/rtsp_service/src/rtsp_service.cpp:142:10: warning: Virtual function 'Stop' is called from destructor '~RtspServiceImpl()' at line 58. Dynamic binding is not used. [virtualCallInConstructor]
libs/stream_hub_service/src/stream_hub_service.cpp:195:10: warning: Virtual function 'Stop' is called from destructor '~StreamHubServiceImpl()' at line 65. Dynamic binding is not used. [virtualCallInConstructor]
libs/upgrade_service/src/upgrade_service.cpp:138:10: warning: Virtual function 'Stop' is called from destructor '~UpgradeServiceImpl()' at line 89. Dynamic binding is not used. [virtualCallInConstructor]
libs/upgrade_service/src/upgrade_service.cpp:138:10: warning: Virtual function 'Stop' is called from destructor '~UpgradeServiceImpl()' at line 90. Dynamic binding is not used. [virtualCallInConstructor]
```

## Review: Clang-Tidy Diagnostics

_No findings in this category._

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

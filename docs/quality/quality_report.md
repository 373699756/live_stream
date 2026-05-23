# Quality Report

本文档由 `scripts/quality_scan.sh full` 生成，汇总代码质量、性能和设计候选问题。

- Generated: `20260523-105220`
- Raw log directory: `/home/cp/Public/hisi/live_stream/reports/quality/20260523-105220`
- Git commit: `0274b17`

## Counts

- cppcheck diagnostics: 0
- cppcheck errors: 0
- keyword risk hits: 231
- hot-path/logging hits: 196
- clang-tidy diagnostics: 0
- include-what-you-use findings: 235

## Must Check First

- Required step failed: `clang-tidy`; inspect `clang-tidy.log`.
- Warning step: `include-what-you-use: exit code 1`; inspect related log before trusting that tool result.

## Must Fix: Cppcheck Errors

_No findings in this category._

## Review: Cppcheck Warnings

_No findings in this category._

## Review: Clang-Tidy Diagnostics

_No findings in this category._

## Review: Include-What-You-Use

```text
/home/cp/Public/hisi/live_stream/app/app_runtime.h should add these lines:
/home/cp/Public/hisi/live_stream/app/app_runtime.h should remove these lines:
/home/cp/Public/hisi/live_stream/app/app_runtime.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/app_runtime.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/app/core_services.h has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/app/core_services.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/core_services.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/app/device_subsystem.h should add these lines:
/home/cp/Public/hisi/live_stream/app/device_subsystem.h should remove these lines:
/home/cp/Public/hisi/live_stream/app/device_subsystem.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/device_subsystem.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/app/linux_network_platform.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/linux_network_platform.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/app/linux_system_platform.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/linux_system_platform.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/app/linux_time_platform.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/linux_time_platform.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/app/linux_upgrade_platform.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/linux_upgrade_platform.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/app/media_subsystem.h has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/app/media_subsystem.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/media_subsystem.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/app/platform_factory.h has correct #includes/fwd-decls)
(/home/cp/Public/hisi/live_stream/app/platform_factory.cpp has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/app/runtime_config.h should add these lines:
/home/cp/Public/hisi/live_stream/app/runtime_config.h should remove these lines:
/home/cp/Public/hisi/live_stream/app/runtime_config.cpp should add these lines:
/home/cp/Public/hisi/live_stream/app/runtime_config.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/include/infra/executor.h should add these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/include/infra/executor.h should remove these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/src/executor.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/src/executor.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/libs/logger_service/src/file_operation_log_store.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/logger_service/src/file_operation_log_store.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/libs/logger_service/include/logger_service.h has correct #includes/fwd-decls)
(/home/cp/Public/hisi/live_stream/libs/logger_service/src/logger_service.cpp has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/libs/infra_service/src/file.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/src/file.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/libs/infra_service/include/infra/log.h has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/libs/infra_service/src/log.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/src/log.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/libs/logger_service/src/operation_record_codec.h has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/libs/logger_service/src/operation_record_codec.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/logger_service/src/operation_record_codec.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/src/path.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/infra_service/src/path.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/libs/net_service/src/event_fd.h has correct #includes/fwd-decls)
(/home/cp/Public/hisi/live_stream/libs/net_service/src/event_fd.cpp has correct #includes/fwd-decls)
(/home/cp/Public/hisi/live_stream/libs/infra_service/include/infra/time.h has correct #includes/fwd-decls)
(/home/cp/Public/hisi/live_stream/libs/infra_service/src/time.cpp has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/libs/net_service/src/event_loop.h should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/event_loop.h should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/event_loop.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/event_loop.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/libs/config_service/src/auth_user_config_store.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/config_service/src/auth_user_config_store.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/libs/net_service/src/fd.h has correct #includes/fwd-decls)
(/home/cp/Public/hisi/live_stream/libs/net_service/src/fd.cpp has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/libs/net_service/src/net_engine_impl.h should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/net_engine_impl.h should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/net_engine_impl.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/net_engine_impl.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/libs/net_service/include/net_service.h has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/libs/net_service/src/net_service.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/net_service.cpp should remove these lines:
(/home/cp/Public/hisi/live_stream/libs/config_service/include/config_service.h has correct #includes/fwd-decls)
/home/cp/Public/hisi/live_stream/libs/config_service/src/config_service.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/config_service/src/config_service.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/socket_util.h should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/socket_util.h should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/socket_util.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/socket_util.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_connection.h should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_connection.h should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_connection.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_connection.cpp should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_server.h should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_server.h should remove these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_server.cpp should add these lines:
/home/cp/Public/hisi/live_stream/libs/net_service/src/tcp_server.cpp should remove these lines:
```

## Optimization Candidates: Files With Most Keyword Risk Hits

```text
     29 libs/media_service/src/media_buffer_pool.cpp
     12 libs/hisi_vendor/src/hisi_mpp_venc.cpp
     11 www/src/hooks/usePreviewPlayer.ts
     10 www/src/api/client.ts
      9 libs/media_service/src/media_service.cpp
      9 libs/hisi_vendor/src/hisi_mpp_vi.cpp
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

# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

review 并优化 HTTP-FLV GOP 缓存起播路径。

## Problem fixed

- `stream_hub_service` 不再为 FLV 客户端额外构造整帧 `std::string` tag，统一使用
  `FlvVideoTagView` slice 发送。
- FLV 起播缓存从单个 last keyframe 扩展为当前 GOP 缓存，缓存只复制小头部，媒体
  payload 继续由 `EncodedFrame`/`VideoBuffer` 持有。
- GOP 缓存超过 64 帧窗口后立即释放，等待下一关键帧重建，避免继续保留和导出不可用
  的不完整 GOP。
- `BuildFlvStartData` 不再导出不完整 GOP；`http_service` 只有在没有完整缓存时才让新
  客户端继续等待下一关键帧。
- `stream_hub_service` 只在确认有 FLV tag 需要发送时转换 public tag view，减少热路径
  无效 work。

## Files changed

- `libs/stream_hub_service/src/stream_hub_stream_state.cpp`
- `libs/stream_hub_service/src/stream_hub_service.cpp`
- `libs/http_service/src/handlers/flv_handler.cpp`
- `docs/active/coder_report.md`

## Verification

已通过：

- `make -C libs/stream_hub_service`
- `make -C libs/http_service`
- `git diff --check`
- `make -j2`

## Commit

Pending: `fix(stream): avoid sending incomplete FLV GOP cache`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在板端用 HTTP-FLV 播放器实测多客户端接入时的起播延迟、内存占用和画面连续性。

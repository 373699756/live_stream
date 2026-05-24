# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI 模块开发，补齐 host_stub 三类 AI 任务的联调输出。

## Problem fixed

- `host_stub` 现在按 `AiTask` 输出确定性检测结果：
  `object_detection -> person`、`face_detection -> face`、
  `motion_classification -> motion`。
- 三类任务都继续受 `confidence_threshold` 和 `max_results` 控制，便于 Web
  用真实 `/api/ai/status`、`/api/ai/alerts`、告警图片和预览叠框做联调。
- 生产默认配置未变：`ai.enabled=false`，后端仍默认 `hisi3516dv300_nnie`。

## Files changed

- `libs/ai_service/src/ai_service.cpp`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`

## Verification

已通过：

- `make -C libs/ai_service CXX=g++ AR=ar CROSS_COMPILE= BUILD_DIR=/tmp/live_stream_ai_host_build all`
- `make -C libs/ai_service ENABLE_HISI_MPP=1`

## Commit

`feat(ai): cover host stub task outputs`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端打开 `ai.enabled=true`，实测 VGS + IVE CSC
  前处理耗时、检测结果、Web 告警瀑布流和预览叠框。
- 真实 IVE/IVS_MD 移动侦测后端仍需在板端接入和验证；本次只补 host stub 联调输出。
- 当前工作区还有非本次 AI 任务的 media/stream_hub/webrtc 未提交改动，未纳入本次
  验证和提交范围。

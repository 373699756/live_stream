# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI 模块开发，在实时预览页增加当前 AI 检测结果叠框。

## Problem fixed

- `LiveViewPage` 现在轮询 `/api/ai/status`，不改后端 API 契约。
- 新增 `AiDetectionOverlay`，把当前码流的 `last_result.detections` 按归一化坐标叠到
  `VideoPreview` 的实际画面区域，避免视频黑边导致框位置偏移。
- AI 未启用、后端不可用或状态刷新失败时不显示旧框，只保留紧凑 AI 状态提示；
  最近结果来自另一条码流时提示结果来源，不影响实时预览和抓图按钮。

## Files changed

- `www/src/api/ai.ts`
- `www/src/components/AiDetectionOverlay.tsx`
- `www/src/hooks/useAiStatus.ts`
- `www/src/pages/LiveViewPage.tsx`
- `www/src/styles/layout.css`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`

## Verification

已通过：

- `cd www && npm run build`
- `git diff --check`
- `make -j2`

## Commit

`feat(www): overlay AI detections on preview`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端打开 `ai.enabled=true`，实测 VGS + IVE CSC
  前处理耗时、检测结果、Web 告警瀑布流和预览叠框。
- 若要在 PC/host 上联调真实 AI 告警接口，需要显式把配置改成 `backend=host_stub`
  并让 AI 服务启动。

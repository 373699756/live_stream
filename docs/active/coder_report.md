# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI 模块开发，在 Web AI 告警页增加 AI 配置面板。

## Problem fixed

- AI 告警页现在可以编辑并保存 `ai` 配置：启用状态、后端、任务、码流、模型路径、
  推理间隔、置信度阈值和最大结果数。
- 保存复用现有 `PUT /api/config/ai`，不新增后端 HTTP 契约。
- 保存后刷新状态，并提示需要重启服务后让当前 `AiService` 按新配置重建后端。

## Files changed

- `www/src/api/ai.ts`
- `www/src/pages/AiAlertsPage.tsx`
- `www/src/styles/layout.css`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`

## Verification

已通过：

- `cd www && npm run build`
- `git diff --check -- www/src/api/ai.ts www/src/pages/AiAlertsPage.tsx www/src/styles/layout.css docs/active/ai_development_plan.md docs/active/coder_report.md`

## Commit

`feat(www): add AI config controls`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端通过 Web 保存 `ai.enabled=true` 后重启服务，
  验证 NNIE/IVS_MD、Web 告警瀑布流和预览叠框。
- 需要在板端验证 `task=motion_classification` 的 IVS_MD 框坐标、灵敏度和误报率。
- 当前工作区还有非本次 AI 任务的 media/stream_hub/webrtc/http 未提交改动，未纳入
  本次提交范围。

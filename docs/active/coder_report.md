# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI 模块开发，实现 AI 配置热应用。

## Problem fixed

- `AiService` 随媒体子系统常驻创建；`ai.enabled=false` 时不启动推理后端或抓帧线程，
  但保留 `/api/ai/status` 和配置 apply 回调。
- Web 保存 `ai` 配置后，运行中的 `AiService` 会停止旧推理线程和后端，并按新配置
  启动、关闭或重建推理链路，不再要求重启服务。
- 配置热应用失败时回滚到上一份运行配置；停止推理时最多等待 50ms 粒度检查退出，
  避免长推理间隔拖慢切换。
- `AiService` 析构时会 detach `ai` 配置回调，避免媒体子系统重启后留下旧回调。

## Files changed

- `app/media_subsystem.cpp`
- `libs/ai_service/src/ai_service.cpp`
- `www/src/pages/AiAlertsPage.tsx`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`
- `docs/active/decision_log.md`

## Verification

已通过：

- `make -C libs/ai_service CXX=g++ AR=ar CROSS_COMPILE= BUILD_DIR=/tmp/live_stream_ai_host_build all`
- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `make build/obj/app/media_subsystem.o`
- `cd www && npm run build`
- `git diff --check -- libs/ai_service/src/ai_service.cpp app/media_subsystem.cpp www/src/pages/AiAlertsPage.tsx docs/active/ai_development_plan.md docs/active/coder_report.md docs/active/decision_log.md`

## Commit

`feat(ai): hot apply runtime config`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端通过 Web 保存 `ai.enabled=true`，验证无需重启即可
  启停 NNIE/IVS_MD、刷新 Web 告警瀑布流和预览叠框。
- 需要在板端验证 `task=motion_classification` 的 IVS_MD 框坐标、灵敏度和误报率。
- 当前工作区还有非本次 AI 任务的 media/stream_hub/webrtc/http 未提交改动，未纳入
  本次提交范围。

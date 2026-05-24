# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI 模块 1/5/6：板端诊断状态、告警联动和性能统计。

## Problem fixed

- `GET /api/ai/status` 的 `stats` 增加 alarm 联动状态、最近成功/失败时间、
  最近/最大/平均推理耗时，方便 Hi3516DV300/CV500 板端验证 NNIE/IVS_MD。
- AI 有有效检测结果时向 `alarm_service` 注入 `ai_detection` 输入，触发既有
  `kAlarmTriggered` 事件链路；检测消失、抓帧失败或 AI 停止时主动清除输入。
- `alarm` 配置新增 `ai_detection` 规则，默认关闭，并兼容旧配置缺少该字段的情况。
- Web AI 状态页展示耗时、最近成功/失败和告警联动状态。

## Files changed

- `app/app_runtime.cpp`
- `app/media_subsystem.cpp`
- `app/media_subsystem.h`
- `configs/default_config.json`
- `configs/business_config.json`
- `libs/alarm_service/include/alarm_service.h`
- `libs/alarm_service/src/alarm_service.cpp`
- `libs/ai_service/Makefile`
- `libs/ai_service/include/ai_service.h`
- `libs/ai_service/src/ai_service.cpp`
- `libs/http_service/src/handlers/ai_handler.cpp`
- `www/src/api/mock.ts`
- `www/src/api/types.ts`
- `www/src/pages/AiAlertsPage.tsx`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`
- `docs/active/decision_log.md`
- `docs/contracts/api-config.md`
- `www/README.md`

## Verification

已通过：

- `make -C libs/alarm_service CXX=g++ AR=ar CROSS_COMPILE= BUILD_DIR=/tmp/live_stream_alarm_host_build all`
- `make -C libs/ai_service CXX=g++ AR=ar CROSS_COMPILE= BUILD_DIR=/tmp/live_stream_ai_host_build all`
- `make -C libs/alarm_service ENABLE_HISI_MPP=1`
- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `make -C libs/http_service ENABLE_HISI_MPP=1`
- `make -C libs/http_service CXX=g++ AR=ar CROSS_COMPILE= BUILD_DIR=/tmp/live_stream_http_host_build all`
- `make build/obj/app/app_runtime.o build/obj/app/media_subsystem.o`
- `cd www && npm run build`
- `git diff --check -- app/app_runtime.cpp app/media_subsystem.cpp app/media_subsystem.h libs/alarm_service/include/alarm_service.h libs/alarm_service/src/alarm_service.cpp libs/ai_service/Makefile libs/ai_service/include/ai_service.h libs/ai_service/src/ai_service.cpp libs/http_service/src/handlers/ai_handler.cpp configs/default_config.json configs/business_config.json www/src/api/types.ts www/src/api/mock.ts www/src/pages/AiAlertsPage.tsx docs/contracts/api-config.md www/README.md docs/active/ai_development_plan.md docs/active/coder_report.md docs/active/decision_log.md`

## Commit

`feat(ai): add alarm linkage and diagnostics`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。
- 未启用录像、回放、长期存储或音频能力；`actions.record` 仍保持不生效。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端通过 Web 保存 `ai.enabled=true`，验证无需重启即可
  启停 NNIE/IVS_MD、刷新 Web 告警瀑布流、预览叠框和 `ai_detection` 告警事件。
- 需要在板端验证 `task=motion_classification` 的 IVS_MD 框坐标、灵敏度和误报率。
- 当前工作区还有非本次 AI 任务的 media/stream_hub/webrtc/http 未提交改动，未纳入
  本次提交范围。

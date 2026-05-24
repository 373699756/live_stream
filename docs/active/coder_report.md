# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

大范围优化 AI/NNIE 首版 SSD 路径，降低默认板端负载和每帧 CPU/内存抖动。

## Problem fixed

- AI 默认从主码流改为子码流，默认推理间隔从 200ms 改为 500ms。
- `inst_ssd_cycle.wk` 的 U8_C3 CPU 前处理缓存 resize 采样点，并用 YUV 查表替代
  每像素重复乘法。
- SSD prior boxes 在模型加载后生成一次，后处理复用 loc/conf/box/proposal/NMS
  buffer，避免每帧重复分配大数组。
- SSD 输出 blob 直接追加到复用 buffer，不再为每个输出层创建临时 vector。
- NNIE 推理不再持有 `AiService` 状态锁；状态查询、停止和 Web 告警读取不会被单次
  forward 长时间阻塞。
- 抓帧调度改为按目标时间补眠，避免抓帧等待后再固定 sleep 一整个推理间隔。

## Files changed

- `libs/ai_service/src/ai_service.cpp`
- `libs/ai_service/include/ai_service.h`
- `configs/default_config.json`
- `configs/business_config.json`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`
- `docs/contracts/api-config.md`
- `www/README.md`
- `www/src/api/mock.ts`

## Verification

已通过：

- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `npm run build`（`www/`）
- `git diff --check`
- `make -j2`

## Commit

Pending: `perf(ai): reduce NNIE inference overhead`

## Deviations

- CPU resize + 色彩转换仍是临时可运行路径；下一步大性能收益应来自 IVE/VPSS
  前处理替换。
- 本轮不修改 `tests/`，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端打开 `ai.enabled=true` 实测 CPU 占用、推理耗时和
  Web 告警瀑布流刷新。

# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI/NNIE 开发，把项目默认 SSD 模型从 forward 接到真实检测结果和 Web
告警瀑布流触发源。

## Problem fixed

- `ai_service` 支持 `inst_ssd_cycle.wk` 的 300x300 U8_C3 输入。
- 从 VPSS YVU420SP 帧做首版 CPU resize + YVU 到 BGR planar 转换，写入 NNIE
  输入 blob。
- 按官方 SVP SSD sample 参数实现 prior box、softmax、bbox decode、NMS、
  `confidence_threshold` 和 `max_results` 过滤。
- NNIE forward 后生成归一化 `AiDetection`，有检测结果时原有告警图片保存和
  `/api/ai/alerts` 瀑布流即可触发。
- 默认配置填写 `models/inst_ssd_cycle.wk`、300x300 输入，但 `ai.enabled=false`
  仍默认关闭。
- `make out` 会把项目内 SSD 模型复制到 `out/models/inst_ssd_cycle.wk`。

## Files changed

- `libs/ai_service/src/ai_service.cpp`
- `libs/ai_service/include/ai_service.h`
- `configs/default_config.json`
- `configs/business_config.json`
- `Makefile`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`
- `docs/contracts/api-config.md`
- `www/README.md`
- `www/src/api/mock.ts`

## Verification

已通过：

- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `git diff --check`
- `npm run build`（`www/`）
- `make -j2`

## Commit

`feat(ai): decode SSD NNIE detections`

## Deviations

- 当前 resize/色彩转换先用 CPU 路径，能跑通功能但不是最终性能方案；后续应换成
  IVE/VPSS。
- 本轮只支持官方 SSD VOC 21 类模型；YOLO/RFCN/分类模型仍需单独后处理。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端设置 `ai.enabled=true` 做实机验证，确认 NNIE
  输出和告警图片瀑布流。
- 若主码流 CPU 转换占用过高，下一步做 IVE/VPSS 前处理优化。

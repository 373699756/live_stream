# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI 前处理优化，把 SSD U8_C3 输入的 resize 从 CPU 优先迁到 VGS 硬件缩放。

## Problem fixed

- `YuvFrame` 增加 MPP 低层帧元信息，保留 VPSS 帧物理地址、stride、pixel format、
  compress mode 等硬件处理所需字段。
- `hisi_vendor` 的 `CaptureYuvFrame` 在保持原虚拟地址映射和生命周期释放逻辑不变的
  前提下，填充 `MppYuvFrameInfo`。
- `ai_service` 为 NNIE U8_C3 输入增加 VGS 前处理路径：先把 VPSS YVU420SP 帧缩放到
  模型输入尺寸，再做 YVU 到 BGR planar 转换。
- VGS 输出帧使用复用 MMZ buffer，模型卸载时释放；VGS 不可用或帧元信息不足时自动
  回退已有 CPU resize 路径。
- 补齐 semiplanar UV plane 的物理地址/stride 派生值，兼容 SDK 返回 plane1 为空的
  VPSS 帧。

## Files changed

- `libs/media_service/include/hisisdk/hisi_sdk.h`
- `libs/hisi_vendor/src/hisi_mpp_snapshot.cpp`
- `libs/ai_service/src/ai_service.cpp`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`

## Verification

已通过：

- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `make -C libs/hisi_vendor ENABLE_HISI_MPP=1`
- `git diff --check`

未通过：

- `make -j2` 当前被工作树中未完成的 `libs/stream_hub_service/*` 改动阻塞；
  失败点是 `StreamFlvStartData::last_keyframe`、`PackagedFrameResult::flv_tag`
  等 FLV GOP 缓存改动未同步，非本轮 AI/VGS 改动引入。

## Commit

Pending: `perf(ai): use VGS for NNIE input scaling`

## Deviations

- 本轮先迁移 resize；YVU 到 BGR planar 仍由 CPU 完成。下一步继续评估 IVE/VPSS
  色彩转换或模型输入格式调整。
- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端打开 `ai.enabled=true` 实测 VGS 路径是否被使用、
  推理前处理耗时和 CPU 占用变化。

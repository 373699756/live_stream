# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI 模块开发，接入设备端 IVS_MD 移动侦测后端，并收紧 HiSilicon 链接依赖。

## Problem fixed

- `hisi3516dv300_nnie` 后端现在对 `motion_classification` 走 IVS_MD：
  按实际 VPSS YVU420SP 帧尺寸建立 U8C1 双帧工作区，用 IVE DMA 拷贝亮度平面，
  再调用 `HI_IVS_MD_Process` 输出归一化 `motion` 检测框。
- 移动侦测任务不再要求 `.wk` 模型路径；`object_detection` 仍走现有 NNIE SSD。
- 默认 HiSilicon AI 链接增加 `libmd.a`；真实音频库从默认链接中移除。
- 因 `libmpi.a` 内部会引用音频符号，`hisi_vendor` 提供失败返回的弱 stub
  闭合链接，不启用音频 API 或音频能力。

## Files changed

- `libs/ai_service/src/ai_service.cpp`
- `libs/hisi_vendor/src/hisi_mpp_audio_stubs.cpp`
- `libs/hisi_vendor/toolchain_hi3516dv300.mk`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`
- `docs/active/decision_log.md`

## Verification

已通过：

- `make -C libs/ai_service CXX=g++ AR=ar CROSS_COMPILE= BUILD_DIR=/tmp/live_stream_ai_host_build all`
- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `make -C libs/hisi_vendor ENABLE_HISI_MPP=1`
- `make -j2`
- `git diff --check -- libs/ai_service/src/ai_service.cpp libs/hisi_vendor/toolchain_hi3516dv300.mk libs/hisi_vendor/src/hisi_mpp_audio_stubs.cpp`

## Commit

`feat(ai): add device motion detection backend`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。
- `make -j2` 重新打包了 `out/`，但本次不提交构建产物。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端打开 `ai.enabled=true`，实测 VGS + IVE CSC
  前处理耗时、检测结果、Web 告警瀑布流和预览叠框。
- 需要在板端验证 `task=motion_classification` 的 IVS_MD 框坐标、灵敏度和误报率。
- 当前工作区还有非本次 AI 任务的 media/stream_hub/webrtc/http 未提交改动，未纳入
  本次提交范围。

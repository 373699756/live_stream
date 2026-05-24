# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续优化 AI NNIE 前处理路径。

## Problem fixed

- `ai_service` 的 U8_C3 输入前处理现在优先走 `VGS scale -> IVE CSC -> BGR planar
  row copy`。
- IVE 输出 RGB planar 后只在 CPU 上做 B/G/R 平面顺序拷贝，避免每像素 YUV 转 RGB
  公式计算。
- VGS 或 IVE 不可用时保留原 CPU resize + YVU 到 BGR planar 回退路径，避免影响
  `ai.enabled=false` 和 host stub 构建。
- 修正同一段 MPP frame 地址填充代码中的缩进异常。

## Files changed

- `libs/ai_service/src/ai_service.cpp`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`

## Verification

已通过：

- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `make -C libs/ai_service CXX=g++ AR=ar CROSS_COMPILE= BUILD_DIR=/tmp/live_stream_ai_host_build all`
- `git diff --check`
- `make -j2`

## Commit

Pending: `perf(ai): use IVE for NNIE color conversion`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端打开 `ai.enabled=true`，实测 VGS + IVE CSC
  前处理耗时、检测结果和 Web 告警瀑布流。

# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

review AI 模块并优化 host stub 联调链路。

## Problem fixed

- 对照官方 SVP SSD sample 复核了当前 SSD prior、decode、softmax 和 NMS 主流程，
  未发现需要立即修正的错参。
- `host_stub` 后端现在会为有效输入帧返回确定性 object detection 结果，便于不用
  NNIE 硬件也能走通 `/api/ai/status`、`/api/ai/alerts` 和告警图片写入链路。
- host stub 结果仍受 `confidence_threshold`、`max_results` 和 `task` 配置约束；
  生产默认配置仍保持 `ai.enabled=false`。

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

Pending: `feat(ai): emit host stub detections`

## Deviations

- 未修改测试目录，遵循当前阶段测试目录暂不主动整理的项目约定。

## Blocked or follow-up

- 需要在 Hi3516DV300/CV500 板端打开 `ai.enabled=true`，实测 VGS + IVE CSC
  前处理耗时、检测结果和 Web 告警瀑布流。
- 若要在 PC/host 上联调真实 AI 告警接口，需要显式把配置改成 `backend=host_stub`
  并让 AI 服务启动。

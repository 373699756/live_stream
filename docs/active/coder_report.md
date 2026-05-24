# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 AI/NNIE 开发准备，把 SVP/NNIE/IVE 相关依赖本地化到本项目工程目录。

## Problem fixed

- 复制 SDK SVP 文档到 `3rdparty/hisi_svp/docs`。
- 复制 SDK `mpp/sample/svp` 完整示例树到 `3rdparty/hisi_svp/sample/svp`，包含
  NNIE/IVE/HiRuntime sample、17 个 `.wk` 模型和样例输入数据。
- 保留现有 `3rdparty/hisi_mpp/include` / `lib` 作为项目编译用 MPP、NNIE、IVE
  头文件和库，不再要求程序员从外部 SDK 找 AI 头库。
- 增加 `3rdparty/hisi_svp/README.md`，说明来源、目录、模型资源和后续开发参考文件。
- 更新 AI 开发计划和决策日志，固定本地依赖路径和下一步模型后处理方向。

## Files changed

- `3rdparty/hisi_svp/README.md`
- `3rdparty/hisi_svp/docs/*`
- `3rdparty/hisi_svp/sample/svp/*`
- `docs/active/ai_development_plan.md`
- `docs/active/coder_report.md`
- `docs/active/decision_log.md`

## Verification

通过：

- `find` 计数确认 SVP 文档 4 个、SVP sample 文件 117 个、`.wk` 模型 17 个已进入
  `3rdparty/hisi_svp`。
- `git diff --check`
- `make -C libs/ai_service ENABLE_HISI_MPP=1`
- `make -j2`
- `npm run build`（`www/`）

## Commit

Pending.

## Deviations

- 新增依赖体积约 879M，属于用户要求的“相关依赖全部拷贝本项目工程目录中”。
- 本轮没有改默认配置启用 AI；`ai.enabled=false` 仍保持默认关闭。

## Blocked or follow-up

- 下一步需要选定实际检测模型，优先建议
  `3rdparty/hisi_svp/sample/svp/nnie/data/nnie_model/detection/inst_ssd_cycle.wk`。
- NNIE 已能 forward，但还缺 SSD/YOLO/RFCN 等模型后处理，暂不会生成真实
  `AiDetection` 和告警图片。

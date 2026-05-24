# AI Development Plan

短计划，用来指导当前 AI 开发，不写成长历史。

## SDK Source

- SVP 文档：`~/Public/hisi/sdk_hisi3516dv300/zh/01.software/board/SVP`
- SVP 示例：
  `/home/cp/Public/hisi/sdk_hisi3516dv300/Hi3516CV500_SDK_V2.0.1.0/smp/a7_linux/mpp/sample/svp`
- 示例重点：
  - `ive/`：移动侦测、边缘、跟踪和前处理参考。
  - `nnie/`：NNIE 模型加载、forward 和后处理参考。
  - `hirt/`：runtime 模型组、SSD/RFCN/classify 参考。

## Module Boundary

- `ai_service` 是唯一 AI 业务入口，拥有配置、抓帧调度、推理结果和 AI 告警图片记录。
- `snapshot_service` 继续拥有 JPEG 抓图能力；AI 只通过 `ISnapshotView` 获取告警图片。
- `http_service` 只做 DTO 转换、鉴权和图片返回，不拥有 AI 状态。
- `www` 只消费 `/api/ai/*`，不解析 SDK 模型或设备内部状态。

## Current Implementation Target

- AI 默认为可选实验能力，配置 `ai.enabled=false` 时不启动。
- 设备构建已接入 NNIE `.wk` 模型加载/卸载：
  - `ENABLE_HISI_MPP=1` 且 `CONFIG_HISI_AI_LIBS=y` 时编译 NNIE 后端。
  - `ai_service` 使用 MMZ 承载模型文件，并调用 `HI_MPI_SVP_NNIE_LoadModel`
    / `HI_MPI_SVP_NNIE_UnloadModel` 管理模型生命周期。
  - 已准备普通 CNN 模型的 task/tmp/input/output blob workspace；ROI 和 recurrent
    模型暂不支持。
  - 当前尚未接入 YUV 到模型输入的格式适配、forward、query 和后处理，所以 NNIE
    后端不会生成检测结果。
- Web 暂时告警能力是图片瀑布流：
  - `GET /api/ai/status` 返回 AI 配置、统计和最近推理结果。
  - `GET /api/ai/alerts` 返回最近 AI 告警图片列表。
  - `GET /api/ai/alerts/{id}/image` 返回 JPEG 图片。
- 告警图片保存到 `build/runtime/ai_alerts/`，默认保留最近 100 条。
- 第一版不接 `alarm_service`，不做录像、回放或长期存储。

## Next Development Order

1. 保持 host `host_stub` 可构建，确保 Web 可以用 mock 和空告警联调。
2. 将 VPSS YUV 输入适配为首段输入 blob，必要时使用 IVE/VPSS resize 和色彩转换。
3. 接入 `HI_MPI_SVP_NNIE_Forward` / `HI_MPI_SVP_NNIE_Query`，先完成单段 CNN 跑通。
4. 把模型输出统一转换为 `AiDetection`，坐标归一化到 0.0 到 1.0。
5. 检测结果稳定后再增加 IVE 移动侦测和前端预览叠框。

## Acceptance

- `make -j2` 和 `www` build 可验证。
- `ai.enabled=false` 不影响直播、抓图和系统启动。
- AI 有有效检测结果时，Web 能看到告警图片瀑布流。
- 推理失败、抓图失败或图片写入失败只更新统计，不影响实时预览。

# AI Development Plan

短计划，用来指导当前 AI 开发，不写成长历史。

## SDK Source

- SVP 文档：`~/Public/hisi/sdk_hisi3516dv300/zh/01.software/board/SVP`
- SVP 示例：
  `/home/cp/Public/hisi/sdk_hisi3516dv300/Hi3516CV500_SDK_V2.0.1.0/smp/a7_linux/mpp/sample/svp`
- 项目内离线副本：
  - `3rdparty/hisi_mpp/include` 和 `3rdparty/hisi_mpp/lib`：编译用 MPP/NNIE/IVE
    头文件和库。
  - `3rdparty/hisi_svp/docs`：SVP/IVE/IVS 开发文档 PDF。
  - `3rdparty/hisi_svp/sample/svp`：SVP 官方 sample、`.wk` 模型和输入样例。
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
  - 已支持尺寸匹配的 YUV420SP 帧拷贝到首段 YVU420SP 输入 blob 并刷新缓存。
  - 已支持 `inst_ssd_cycle.wk` 的 300x300 U8_C3 输入：从 VPSS 的
    YVU420SP 帧做 CPU resize + YVU 到 BGR planar 转换。
  - 已接入单段 CNN 的 `HI_MPI_SVP_NNIE_Forward` / `HI_MPI_SVP_NNIE_Query`。
  - 已按官方 SSD sample 参数接入 VOC 21 类后处理：prior box、softmax、
    bbox decode、NMS、置信度和 `max_results` 过滤，输出归一化 `AiDetection`。
- 默认模型为
  `3rdparty/hisi_svp/sample/svp/nnie/data/nnie_model/detection/inst_ssd_cycle.wk`，
  `make out` 会复制到 `out/models/inst_ssd_cycle.wk`，运行配置默认填写
  `models/inst_ssd_cycle.wk`，但 `ai.enabled=false` 仍保持默认关闭。
- 官方 SVP 依赖已复制到项目内，后续开发默认从 `3rdparty/hisi_svp` 查 sample
  和模型，不再依赖外部 SDK 路径。
- Web 暂时告警能力是图片瀑布流：
  - `GET /api/ai/status` 返回 AI 配置、统计和最近推理结果。
  - `GET /api/ai/alerts` 返回最近 AI 告警图片列表。
  - `GET /api/ai/alerts/{id}/image` 返回 JPEG 图片。
- 告警图片保存到 `build/runtime/ai_alerts/`，默认保留最近 100 条。
- 第一版不接 `alarm_service`，不做录像、回放或长期存储。

## Next Development Order

1. 保持 host `host_stub` 可构建，确保 Web 可以用 mock 和空告警联调。
2. 在 Hi3516DV300/CV500 板端打开 `ai.enabled=true`，确认
   `models/inst_ssd_cycle.wk` 路径、NNIE forward 和 Web 告警瀑布流。
3. 用 IVE/VPSS 替换当前 CPU resize + 色彩转换，降低主码流推理开销。
4. 检测结果稳定后再增加 IVE 移动侦测和前端预览叠框。

## Acceptance

- `make -j2` 和 `www` build 可验证。
- `ai.enabled=false` 不影响直播、抓图和系统启动。
- AI 有有效检测结果时，Web 能看到告警图片瀑布流。
- 推理失败、抓图失败或图片写入失败只更新统计，不影响实时预览。

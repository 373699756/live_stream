# Decision Log

记录固定下来的产品和架构决策，避免 AI 会话重复打开同一个问题。保持短表，不写过程长文。

| Date | Decision | Reason | Impact |
| --- | --- | --- | --- |
| 2026-05-21 | AI 会话先读短执行文档，再按需读长设计文档。 | 长文档有价值，但每轮都读会浪费 token 并增加跑偏概率。 | 默认入口是 `AGENTS.md` 和 `docs/active/*`；`docs/architecture/*`、`docs/contracts/*`、`docs/performance/*`、`docs/ai/*` 只定向读取。 |
| 2026-05-21 | 一轮任务通常只处理一个模块，最多一个相邻接口模块。 | 多模块大任务容易导致重复封装、命名漂移和行为回归。 | 跨模块工作先做接口/状态契约，再做实现。 |
| 2026-05-21 | 前端直播状态不猜测设备内部状态。 | HLS/FLV/WebRTC ready 已由后端真实链路产生，前端猜测会导致偶现播放卡住。 | Web Console 使用 `browserCodec`、`hlsReady`、`flvReady`、`webrtcReady` 控制预览可用性。 |
| 2026-05-21 | 精简优先，不为简单顺序逻辑新增抽象。 | 过度使用 Context、State、Manager、Store、helper 会降低可读性。 | 新增抽象前必须说明真实收益；否则使用直线流程。 |
| 2026-05-21 | 高频日志默认不进入生产路径。 | 帧路径和首帧路径日志会影响实时性并淹没关键问题。 | 只保留错误、启动停止、配置变化和关键状态变化；临时诊断日志任务后删除。 |
| 2026-05-21 | 普通实现任务不扩写长设计文档。 | 反复追加长文档会降低 AI 使用效率。 | 当前状态写 `docs/active/current_milestone.md`，固定决策写本文件，复盘写 `docs/ai/lessons-learned.md`。 |
| 2026-05-23 | 文字叠加和隐私遮挡统一归入 `region_service`，配置/API 命名为 `overlay`。 | OSD 只是区域能力的一部分，遮挡和文字都依赖 region 生命周期；用 overlay 避免把模块命名限定成文字 OSD。 | 不再新增 `osd_service`、`region_mpp_adapter` 这类并列适配层；HiSilicon API 转换函数留在 `hisi_vendor`。 |
| 2026-05-24 | AI 是默认关闭的可选实验能力，Web 暂用图片瀑布流承载告警。 | 先验证 SVP/NNIE/IVE 链路和 Web 可见性，不扩大到录像、回放或正式告警事件。 | `ai_service` 维护最近 AI 告警图片；`alarm_service` 联动、长期存储和视频叠框后续单独设计。 |
| 2026-05-24 | 设备构建默认链接 HiSilicon NNIE/IVE 库。 | `ai_service` 已直接管理 NNIE 模型资源，应用最终链接需要解析 `HI_MPI_SVP_NNIE_*` 符号。 | `CONFIG_HISI_AI_LIBS ?= y`；需要裁剪 AI 链接时可显式传 `CONFIG_HISI_AI_LIBS=n`。 |
| 2026-05-24 | SVP/NNIE/IVE 开发依赖固定保存在项目内。 | AI 开发不能依赖程序员再去外部 SDK 路径找文档、sample、`.wk` 模型或样例输入。 | 编译头库使用 `3rdparty/hisi_mpp`；开发参考和模型资源使用 `3rdparty/hisi_svp`。 |

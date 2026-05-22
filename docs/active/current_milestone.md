# Current Milestone

短执行账本。普通 AI 任务从这里开始，但不要把它写成长历史。

## Active Focus

稳定现有视频实时预览、抓图、配置和运维管理功能。优先保持后端主链路、HTTP API
和 Web Console 一致，不扩大到音频、录像、存储回放或无关 UI/API。

当前优先级：

1. 保持 `make -j2` 和 `www` 构建可验证。
2. 稳定 `media_service -> stream_hub_service -> HLS/FLV/WebRTC -> HTTP -> www`
   状态链路。
3. 收敛命名、日志、过度封装和重复接口，优先做小范围负代码清理。
4. 前端只消费后端 ready/status 字段，不猜设备内部状态。

## Current Baseline

- `app/` 是组合根，负责服务创建、依赖注入、启动顺序和关闭顺序。
- `libs/media_service` 拥有视频 pipeline、MPP/VENC 适配、码流启动停止和关键帧请求。
- `libs/stream_hub_service` 拥有编码帧分发、HLS/FLV 浏览器流状态和封装缓存。
- `libs/http_service` 拥有 HTTP 路由、认证边界、DTO 转换、静态资源和直播 API。
- `libs/webrtc_service` 拥有 WebRTC peer/session、SDP/ICE 和媒体传输集成。
- `www/` 是 React IPC/NVR 管理台，只通过 HTTP API 与设备交互。

## Work Rules

- 一轮 AI 任务通常只碰一个模块，最多再碰一个相邻接口模块。
- 写代码前先说明目标、范围、不做什么和验证方式。
- bugfix、rename、cleanup、refactor 不混在一个提交里。
- 新增接口、helper、class、hook 前先查已有接口；能直接写清楚就不要抽象。
- 中断恢复后先看 `git status --short`、`git log --oneline -5` 和相关 diff。
- 每次关键代码改动后做聚焦验证并提交相关文件。

## Next Task Queue

1. 按用户要求继续处理明确 bug 或技术债，不主动扩大范围。
2. 阶段性 review 时优先扫描生产代码中的过度封装、泛命名和高频日志。
3. 若直播链路异常，先查状态来源和 ready 字段，再查播放器或协议层。

## Update Rule

本文件保持在 120 行以内。完成项替换掉，不追加长历史。

# API And Config Contract Migration Stub

本目录不再承载长期 API/config 设计正文。API、配置字段和 DTO 语义已经拆回拥有
模块：

- HTTP 路由和 DTO 转换：`../libs/http-service-design.md`
- 配置中心和 scope 应用：`../libs/config-service-design.md`
- Web API 消费：`../web/api-consumption-design.md`
- 视频/图像配置：`../libs/media-service-design.md`
- 浏览器播放状态：`../libs/media-source-design.md`
- overlay 配置：`../libs/region-service-design.md`
- snapshot 配置：`../libs/snapshot-service-design.md`
- AI 配置和 `/api/ai/*`：`../libs/ai-service-design.md`
- network/system/time/alarm/upgrade：对应 `../libs/*-service-design.md`

新增或修改 API/config 时，不要把实现设计写回本文件；请更新拥有模块文档和 Web
消费文档。

# Web API Consumption Design

## 模块定位

前端 API 层负责把 Web Console 的页面动作转成 HTTP 请求，并把响应映射为
TypeScript DTO。后端 API 的语义归 `http_service` 和对应业务模块，前端只消费。

## 总体框架图

```mermaid
flowchart LR
  Pages[pages/hooks] --> DomainApi[api/video image stream system ...]
  DomainApi --> Client[api/client.ts]
  Client --> Backend[/api]
  DomainApi --> Types[api/types.ts]
  DomainApi --> Mock[api/mock.ts]
```

## API 分组

- Auth：login、logout、change password、current user。
- Config：video、image、overlay、network、snapshot、AI。
- Media/status：capabilities、stream status、snapshot、HLS、FLV、MJPEG。
- System/operations：system status、upgrade、operation logs。
- WebRTC signaling：peer、offer、candidate、close。

## DTO 规则

- `www/src/api/types.ts` 描述 Web 消费字段，不描述隐藏 SDK 内部结构。
- 字段新增、删除或语义变更必须同步后端 handler、前端 API、`www/README.md`
  和拥有模块设计文档。
- 前端不补齐后端缺失状态；缺字段应走兼容默认值或显示不可用状态。

## 认证与错误处理

认证状态由 `AuthContext` 管理。工厂密码路径只允许初始设置，后端返回
`must_change_password` 时 Web 必须先完成改密再进入管理台。

HTTP 错误处理应在 API client 层统一转换，页面只负责显示业务含义明确的状态。

## Mock fallback

mock 只用于后端不可用时的 UI 开发和交互验证。mock 数据不得成为真实设备状态或
API schema 的权威来源。

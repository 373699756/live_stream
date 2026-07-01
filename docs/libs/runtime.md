# runtime

## 模块定位

`runtime` 提供进程级运行入口和只读服务注册表，用于承接已删除的跨模块
`*Dependencies` 注入 DTO。它不拥有任何业务对象，只保存 app 组合根安装进来的
non-owning 指针。

## 核心职责

- `Runtime` 保存基础服务入口：`ILogger`、`IConfig`、`IAuth`、
  `event::EventCenter` 和 `ISocketIo`。
- `ServiceRegistry` 保存协议只读视图：RTSP、WebRTC、ONVIF 和 HTTP streaming
  session reader。
- 提供显式 `Install/Register` 和 `Clear/Unregister`，由 app 启停流程按生命周期调用。

## 边界

- `Runtime` 不注册 `DeviceMedia`、`MediaStreams`、`IRtsp`、`IWebrtc`、`IHttp`
  或 ONVIF 完整 service；当前媒体源入口归 `media` 模块的
  `MediaSourceRegistry`。
- `ServiceRegistry` 只允许只读 reader 接口，不允许注册带 `Start`、`Stop`、
  `ApplyOptions`、`ClosePeer` 等业务控制能力的完整 service。
- 媒体帧、配置 `verify/apply/save` 和 MPP/VENC 生命周期不经过本模块。

## 状态与资源模型

所有指针均为 non-owning。重复安装相同指针视为幂等；重复安装不同指针失败，
避免静默覆盖。组合根停止时必须先停止依赖这些入口的协议和媒体模块，再清理
registry/runtime，防止留下悬空指针。

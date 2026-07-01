# net_stat

`net_stat` 已合并到 `libs/net`，不再是独立模块、独立静态库或独立 include
目录。public header 保留为 `net_stat.h`，实际路径为 `libs/net/include/net_stat.h`。

当前职责和事件契约归 `docs/libs/net.md` 维护。历史上直接采样 `rtsp`、`webrtc`
和 `media` public stats/info 的设计已经删除；迁移后 `net_stat` 只依赖
`INetIo` 和可选 `event::EventCenter`，协议活跃数通过轻量事件进入统计。

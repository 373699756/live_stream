# rtp

`rtp` 独立模块已合并到 `libs/media_codec`。`rtp.h` 文件名和
`live_stream::rtp` 命名空间保留，实际路径为 `libs/media_codec/include/rtp.h`，
实现文件为 `libs/media_codec/src/rtp_packetizer.cpp`。

当前 RTP packetizer 职责和契约归 `docs/libs/media_codec.md` 维护。历史上的
独立 `libs/rtp` 目录、`librtp.a` 静态库和 `libs/rtp/include` include 路径已经删除。

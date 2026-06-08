#ifndef LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_WRITER_H_
#define LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_WRITER_H_

#include "http.h"
#include "media/media_buffer.h"
#include "net.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace live_stream {

enum class HttpMediaClientType {
    kNone,
    kFlv,
    kMjpeg,
    kEventStream,
};

struct HttpMediaClientHandle {
    HttpMediaClientType type = HttpMediaClientType::kNone;
    uint64_t id = 0;
};

using HttpMediaCloseCallback =
    std::function<void(const HttpMediaClientHandle &)>;

struct HttpMediaSlice {
    // owner 为空时 data 必须在本次调用内可复制；owner 非空时 data 可指向
    // VideoBuffer payload，HTTP/net 会在异步发送期间 ref/unref owner。
    const uint8_t *data = nullptr;
    size_t size = 0;
    VideoBuffer *owner = nullptr;
};

// 长连接 HTTP 媒体输出边界，例如 HLS segment、HTTP-FLV 和 MJPEG。
// 调用方只描述 slice 和 owner；真正是否复制、何时释放由 HTTP/net 层统一处理。
class HttpMediaWriter {
public:
    virtual ~HttpMediaWriter() = default;

    // 发送一次性 HTTP 响应。close_after_response=true 时 writer 会等队列写完后
    // 关闭 TCP，适合 HLS segment、错误响应和普通短响应。
    virtual void SendResponse(ConnectionId connection_id,
                              const HttpResponse &response,
                              bool close_after_response) = 0;
    // 发送带多段 body 的一次性响应。body_size 必须等于 body_slices 总长度，
    // 用于生成正确 Content-Length。
    virtual bool SendResponseSlices(ConnectionId connection_id,
                                    const HttpResponse &response,
                                    const HttpMediaSlice *body_slices,
                                    size_t body_slice_count,
                                    size_t body_size,
                                    bool close_after_response) = 0;
    // 将 HTTP session 切成流式模式。调用成功后不能再按普通 keep-alive 请求处理。
    virtual bool BeginStream(ConnectionId connection_id) = 0;
    // 绑定媒体模块的 client id，TCP close 时 writer 会通过 close callback 归还。
    virtual bool AttachStreamClient(ConnectionId connection_id,
                                    HttpMediaClientHandle client) = 0;
    // 发送协议小块，例如 FLV header、SSE message、MJPEG boundary。
    virtual bool EnqueueStreamingChunk(ConnectionId connection_id,
                                       const uint8_t *data, size_t size) = 0;
    // 带 owner 的 slice 可以在本调用返回后继续有效，writer 会保留 owner 到
    // 网络发送完成；无 owner 的 slice 必须是可立即复制进 TCP 输出队列的小协议字节。
    virtual bool EnqueueStreamingSlices(ConnectionId connection_id,
                                        const HttpMediaSlice *slices,
                                        size_t slice_count) = 0;
    // close callback 由 HTTP close path 触发，用于 detach FLV/MJPEG 或 unsubscribe SSE。
    virtual void SetCloseCallback(HttpMediaCloseCallback callback) = 0;
    // 主动关闭 TCP 连接，通常用于媒体 attach/发送失败后的清理。
    virtual void CloseConnection(ConnectionId connection_id) = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_WRITER_H_

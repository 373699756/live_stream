#ifndef LIVE_STREAM_MEDIA_SRC_FLV_LIVE_RING_H_
#define LIVE_STREAM_MEDIA_SRC_FLV_LIVE_RING_H_

#include "media/media_streams.h"

#include <cstddef>
#include <map>
#include <vector>

namespace live_stream {
namespace media_internal {

struct PendingFlvClientWrite {
    MediaFlvClientId client_id = 0;
    IMediaFlvSink *sink = nullptr;
    bool send_sequence_header = false;
    bool starts_on_keyframe = false;
};

// 跟踪单个 media source 下的 HTTP-FLV live client。外层服务负责加锁，
// 本类所有方法都应在同一把 mutex 保护下调用。
class FlvLiveRing {
public:
    MediaFlvClientId AttachClient(StreamId stream_id,
                                  uint64_t config_generation,
                                  bool wait_for_keyframe,
                                  IMediaFlvSink *sink,
                                  size_t max_clients);
    bool DetachClient(MediaFlvClientId client_id);
    void Clear();
    size_t ClientCount() const;
    bool IsStreamClientAttached(StreamId stream_id) const;
    std::vector<PendingFlvClientWrite> CollectWrites(
        StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
        bool has_sequence_header, bool keyframe);
    void ReleaseWrite(MediaFlvClientId client_id);

private:
    struct ClientState {
        StreamId stream_id = StreamId::kMain;
        uint64_t config_generation = 0;
        bool wait_for_keyframe = false;
        IMediaFlvSink *sink = nullptr;
        uint32_t pending_writes = 0;
        bool detached = false;
    };

    static void ReleaseClientSink(ClientState *client);
    bool EraseDetachedClient(MediaFlvClientId client_id,
                             ClientState *client);

    std::map<MediaFlvClientId, ClientState> clients_;
    MediaFlvClientId next_client_id_ = 1;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_FLV_LIVE_RING_H_

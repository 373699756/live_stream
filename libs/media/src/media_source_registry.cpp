#include "media/media_source_registry.h"

#include <mutex>

namespace live_stream {
namespace {

std::mutex g_media_source_mutex;
MediaStreams *g_media_streams = nullptr;

}  // namespace

bool MediaSourceRegistry::Register(MediaStreams *media_streams) {
    if (media_streams == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> guard(g_media_source_mutex);
    if (g_media_streams != nullptr && g_media_streams != media_streams) {
        return false;
    }
    g_media_streams = media_streams;
    return true;
}

MediaStreams *MediaSourceRegistry::Streams() {
    std::lock_guard<std::mutex> guard(g_media_source_mutex);
    return g_media_streams;
}

void MediaSourceRegistry::Clear(MediaStreams *media_streams) {
    std::lock_guard<std::mutex> guard(g_media_source_mutex);
    if (g_media_streams == media_streams) {
        g_media_streams = nullptr;
    }
}

void MediaSourceRegistry::Clear() {
    std::lock_guard<std::mutex> guard(g_media_source_mutex);
    g_media_streams = nullptr;
}

}  // namespace live_stream

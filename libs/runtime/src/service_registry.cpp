#include "service_registry.h"

#include <mutex>

namespace live_stream {
namespace {

std::mutex g_service_registry_mutex;
IRtspSessionReader *g_rtsp_reader = nullptr;
IWebrtcReader *g_webrtc_reader = nullptr;
IOnvifReader *g_onvif_reader = nullptr;
IHttpStreamSessionReader *g_http_reader = nullptr;

template <typename T>
bool RegisterReader(T *next, T **slot) {
    if (next == nullptr || slot == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> guard(g_service_registry_mutex);
    if (*slot != nullptr && *slot != next) {
        return false;
    }
    *slot = next;
    return true;
}

template <typename T>
void UnregisterReader(T *reader, T **slot) {
    std::lock_guard<std::mutex> guard(g_service_registry_mutex);
    if (slot != nullptr && *slot == reader) {
        *slot = nullptr;
    }
}

}  // namespace

bool ServiceRegistry::RegisterRtsp(IRtspSessionReader *reader) {
    return RegisterReader(reader, &g_rtsp_reader);
}

bool ServiceRegistry::RegisterWebrtc(IWebrtcReader *reader) {
    return RegisterReader(reader, &g_webrtc_reader);
}

bool ServiceRegistry::RegisterOnvif(IOnvifReader *reader) {
    return RegisterReader(reader, &g_onvif_reader);
}

bool ServiceRegistry::RegisterHttp(IHttpStreamSessionReader *reader) {
    return RegisterReader(reader, &g_http_reader);
}

void ServiceRegistry::UnregisterRtsp(IRtspSessionReader *reader) {
    UnregisterReader(reader, &g_rtsp_reader);
}

void ServiceRegistry::UnregisterWebrtc(IWebrtcReader *reader) {
    UnregisterReader(reader, &g_webrtc_reader);
}

void ServiceRegistry::UnregisterOnvif(IOnvifReader *reader) {
    UnregisterReader(reader, &g_onvif_reader);
}

void ServiceRegistry::UnregisterHttp(IHttpStreamSessionReader *reader) {
    UnregisterReader(reader, &g_http_reader);
}

IRtspSessionReader *ServiceRegistry::Rtsp() {
    std::lock_guard<std::mutex> guard(g_service_registry_mutex);
    return g_rtsp_reader;
}

IWebrtcReader *ServiceRegistry::Webrtc() {
    std::lock_guard<std::mutex> guard(g_service_registry_mutex);
    return g_webrtc_reader;
}

IOnvifReader *ServiceRegistry::Onvif() {
    std::lock_guard<std::mutex> guard(g_service_registry_mutex);
    return g_onvif_reader;
}

IHttpStreamSessionReader *ServiceRegistry::Http() {
    std::lock_guard<std::mutex> guard(g_service_registry_mutex);
    return g_http_reader;
}

void ServiceRegistry::Clear() {
    std::lock_guard<std::mutex> guard(g_service_registry_mutex);
    g_http_reader = nullptr;
    g_onvif_reader = nullptr;
    g_webrtc_reader = nullptr;
    g_rtsp_reader = nullptr;
}

}  // namespace live_stream

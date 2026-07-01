#ifndef LIVE_STREAM_RUNTIME_SERVICE_REGISTRY_H_
#define LIVE_STREAM_RUNTIME_SERVICE_REGISTRY_H_

namespace live_stream {

class IHttpStreamSessionReader;
class INetStat;
class IOnvifReader;
class IRtspSessionReader;
class IWebrtcReader;

class ServiceRegistry {
public:
    static bool RegisterRtsp(IRtspSessionReader *reader);
    static bool RegisterWebrtc(IWebrtcReader *reader);
    static bool RegisterOnvif(IOnvifReader *reader);
    static bool RegisterHttp(IHttpStreamSessionReader *reader);
    static bool RegisterNetStat(INetStat *reader);

    static void UnregisterRtsp(IRtspSessionReader *reader);
    static void UnregisterWebrtc(IWebrtcReader *reader);
    static void UnregisterOnvif(IOnvifReader *reader);
    static void UnregisterHttp(IHttpStreamSessionReader *reader);
    static void UnregisterNetStat(INetStat *reader);

    static IRtspSessionReader *Rtsp();
    static IWebrtcReader *Webrtc();
    static IOnvifReader *Onvif();
    static IHttpStreamSessionReader *Http();
    static INetStat *NetStat();

    static void Clear();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RUNTIME_SERVICE_REGISTRY_H_

#include "handlers/http_handlers.h"

namespace live_stream {

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpHandlerKind kind,
    const HttpHandlerDependencies &dependencies) {
    switch (kind) {
        case HttpHandlerKind::kAuth:
            return MakeAuthHandler(dependencies.access,
                                         dependencies.auth);
        case HttpHandlerKind::kConfig:
            return MakeConfigHandler(dependencies.access,
                                           dependencies.config);
        case HttpHandlerKind::kOperations:
            return MakeOperationsHandler(dependencies.access,
                                               dependencies.logger);
        case HttpHandlerKind::kNetwork:
            return MakeNetworkHandler(dependencies.access,
                                            dependencies.network_config);
        case HttpHandlerKind::kTime:
            return MakeTimeHandler(dependencies.access,
                                         dependencies.time);
        case HttpHandlerKind::kUpgrade:
            return MakeUpgradeHandler(dependencies.access,
                                            dependencies.upgrade);
        case HttpHandlerKind::kSystem:
            return MakeSystemHandler(
                dependencies.access, dependencies.system,
                dependencies.system_status_sources);
        case HttpHandlerKind::kAlarm:
            return MakeAlarmHandler(dependencies.access,
                                    dependencies.alarm);
        case HttpHandlerKind::kMedia:
            return MakeMediaHandler(
                dependencies.access, dependencies.config,
                dependencies.device_media, dependencies.media_source,
                dependencies.rtsp, dependencies.webrtc, dependencies.http);
        case HttpHandlerKind::kAi:
            return MakeAiHandler(
                dependencies.access, dependencies.config,
                dependencies.ai, dependencies.device_media);
        case HttpHandlerKind::kSnapshot:
            return MakeSnapshotHandler(
                dependencies.access, dependencies.device_media,
                dependencies.snapshot);
        case HttpHandlerKind::kEventStream:
            return MakeEventStreamHandler(dependencies.access);
    }
    return nullptr;
}

}  // namespace live_stream

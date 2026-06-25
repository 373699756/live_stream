#include "handlers/http_handlers.h"

namespace live_stream {

std::unique_ptr<IHttpHandler> MakeAuthHandler(
    const AuthHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeConfigHandler(
    const ConfigHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeOperationsHandler(
    const OperationsHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeNetworkHandler(
    const NetworkHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeTimeHandler(
    const TimeHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeUpgradeHandler(
    const UpgradeHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeSystemHandler(
    const SystemHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeAlarmHandler(
    const AlarmHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeMediaHandler(
    const MediaHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeAiHandler(
    const AiHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeSnapshotHandler(
    const SnapshotHandlerDependencies &dependencies);

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpHandlerKind kind,
    const HttpHandlerDependencies &dependencies) {
    switch (kind) {
        case HttpHandlerKind::kAuth:
            return MakeAuthHandler(dependencies.auth);
        case HttpHandlerKind::kConfig:
            return MakeConfigHandler(dependencies.config);
        case HttpHandlerKind::kOperations:
            return MakeOperationsHandler(dependencies.operations);
        case HttpHandlerKind::kNetwork:
            return MakeNetworkHandler(dependencies.network);
        case HttpHandlerKind::kTime:
            return MakeTimeHandler(dependencies.time);
        case HttpHandlerKind::kUpgrade:
            return MakeUpgradeHandler(dependencies.upgrade);
        case HttpHandlerKind::kSystem:
            return MakeSystemHandler(dependencies.system);
        case HttpHandlerKind::kAlarm:
            return MakeAlarmHandler(dependencies.alarm);
        case HttpHandlerKind::kMedia:
            return MakeMediaHandler(dependencies.media);
        case HttpHandlerKind::kAi:
            return MakeAiHandler(dependencies.ai);
        case HttpHandlerKind::kSnapshot:
            return MakeSnapshotHandler(dependencies.snapshot);
    }
    return nullptr;
}

}  // namespace live_stream

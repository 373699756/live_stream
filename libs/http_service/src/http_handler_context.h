#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_CONTEXT_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_CONTEXT_H_

#include "auth_service.h"
#include "http_service.h"
#include "logger_service.h"
#include "net_service.h"
#include "stream_hub_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {

// Narrow private boundary exposed to domain handlers. Handler modules should
// depend on this interface instead of HttpServiceImpl.
class HttpHandlerContext {
public:
    virtual ~HttpHandlerContext() = default;

    virtual AuthPrincipal Authenticate(const HttpRequest &request) = 0;
    virtual bool RequirePermission(const HttpRequest &request,
                                   AuthPermission permission,
                                   const std::string &target,
                                   AuthPrincipal *principal) = 0;
    virtual live_stream::RequestContext MakeContext(
        const HttpRequest &request, const AuthPrincipal *principal) = 0;
    virtual void RecordOperation(const HttpRequest &request,
                                 const AuthPrincipal &principal,
                                 OperationAction action,
                                 const std::string &target,
                                 OperationResult result,
                                 const std::string &reason) = 0;

    virtual void IncrementParseFailures() = 0;
    virtual void IncrementAuthFailures() = 0;
    virtual void IncrementPermissionDenied() = 0;

    virtual void SendResponse(ConnectionId connection_id,
                              const HttpResponse &response,
                              bool close_after_response) = 0;
    virtual bool BeginFlvSession(
        ConnectionId connection_id,
        const std::shared_ptr<IStreamFlvSink> &sink) = 0;
    virtual bool AttachFlvSessionClient(ConnectionId connection_id,
                                        StreamFlvClientId client_id) = 0;
    virtual bool EnqueueStreamingChunk(ConnectionId connection_id,
                                       const uint8_t *data, size_t size) = 0;
    virtual bool EnqueueStreamingChunk(
        ConnectionId connection_id,
        const std::shared_ptr<const std::string> &data) = 0;
    virtual void CloseConnection(ConnectionId connection_id) = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_CONTEXT_H_

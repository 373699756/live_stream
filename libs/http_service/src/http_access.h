#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ACCESS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ACCESS_H_

#include "auth_service.h"
#include "http_service.h"
#include "logger_service.h"

#include <string>

namespace live_stream {

// Narrow access boundary for business HTTP handlers. It owns auth,
// permission, audit, and request context helpers, but no connection I/O.
class HttpAccess {
public:
    virtual ~HttpAccess() = default;

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
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ACCESS_H_

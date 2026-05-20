#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_

#include "http_handler_context.h"

namespace live_stream {
namespace http_handlers {

HttpResponse HandleLogin(HttpHandlerContext *context,
                         const HttpRequest &request);
HttpResponse HandleLogout(HttpHandlerContext *context,
                          const HttpRequest &request);
HttpResponse HandleMe(HttpHandlerContext *context, const HttpRequest &request);
HttpResponse HandleMediaCapabilities(HttpHandlerContext *context);
HttpResponse HandleStreamStatus(HttpHandlerContext *context,
                                const HttpRequest &request);
HttpResponse HandleSystemStatus(HttpHandlerContext *context,
                                const HttpRequest &request);
HttpResponse HandleSystemCapabilities(HttpHandlerContext *context,
                                      const HttpRequest &request);
HttpResponse HandleSystemReboot(HttpHandlerContext *context,
                                const HttpRequest &request);
HttpResponse HandleSystemFactoryReset(HttpHandlerContext *context,
                                      const HttpRequest &request);
HttpResponse HandleTimeStatus(HttpHandlerContext *context,
                              const HttpRequest &request);
HttpResponse HandleTimeTimezone(HttpHandlerContext *context,
                                const HttpRequest &request);
HttpResponse HandleTimeNtp(HttpHandlerContext *context,
                           const HttpRequest &request);
HttpResponse HandleTimeSystemTime(HttpHandlerContext *context,
                                  const HttpRequest &request);
HttpResponse HandleTimeSync(HttpHandlerContext *context,
                            const HttpRequest &request);
HttpResponse HandleNetworkInterfaces(HttpHandlerContext *context,
                                     const HttpRequest &request);
HttpResponse HandleNetworkInterface(HttpHandlerContext *context,
                                    const HttpRequest &request);
HttpResponse HandleNetworkReload(HttpHandlerContext *context,
                                 const HttpRequest &request);
HttpResponse HandleUpgradeUpload(HttpHandlerContext *context,
                                 const HttpRequest &request);
HttpResponse HandleUpgradeStatus(HttpHandlerContext *context,
                                 const HttpRequest &request);
HttpResponse HandleUpgradeValidate(HttpHandlerContext *context,
                                   const HttpRequest &request);
HttpResponse HandleUpgradeStart(HttpHandlerContext *context,
                                const HttpRequest &request);
HttpResponse HandleUpgradeCancel(HttpHandlerContext *context,
                                 const HttpRequest &request);
HttpResponse HandleUpgradeConfirmReboot(HttpHandlerContext *context,
                                        const HttpRequest &request);
HttpResponse HandleAiStatus(HttpHandlerContext *context,
                            const HttpRequest &request);
HttpResponse HandleSnapshot(HttpHandlerContext *context,
                            const HttpRequest &request);
HttpResponse HandleHls(HttpHandlerContext *context,
                       const HttpRequest &request);
HttpResponse HandleWebrtc(HttpHandlerContext *context,
                          const HttpRequest &request);
void StartFlvStream(HttpHandlerContext *context, ConnectionId connection_id,
                    const HttpRequest &request);
HttpResponse HandleConfig(HttpHandlerContext *context,
                          const HttpRequest &request);
HttpResponse HandleOperations(HttpHandlerContext *context,
                              const HttpRequest &request);
HttpResponse HandleOperationsExport(HttpHandlerContext *context,
                                    const HttpRequest &request);

}  // namespace http_handlers
}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_

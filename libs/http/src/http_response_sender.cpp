#include "http_response_sender.h"

#include "http_module.h"
#include "http_protocol.h"
#include "infra/log.h"

#include <array>
#include <map>
#include <string>
#include <utility>

namespace live_stream {

HttpResponseSender::HttpResponseSender(
    uint32_t send_buffer_limit_bytes)
    : send_buffer_limit_bytes_(send_buffer_limit_bytes) {}

bool HttpResponseSender::SendResponse(
    ISocketIo *socket_io, ConnectionId connection_id,
    const HttpResponse &response, bool close_after_response) const {
    if (!response.body.empty() && !response.body_slices.empty()) {
        Error(kHttpModuleName,
              "HTTP response reject conn=%llu reason=mixed_body status=%d",
              static_cast<unsigned long long>(connection_id),
              response.status_code);
        CloseConnection(socket_io, connection_id,
                        TcpCloseReason::kInternalError);
        return false;
    }
    if (!response.body_slices.empty()) {
        if (response.body_slices.size() > kMaxSocketWriteSlices - 1) {
            Error(kHttpModuleName,
                  "HTTP response reject conn=%llu reason=too_many_slices "
                  "status=%d slices=%zu",
                  static_cast<unsigned long long>(connection_id),
                  response.status_code, response.body_slices.size());
            CloseConnection(socket_io, connection_id,
                            TcpCloseReason::kInternalError);
            return false;
        }

        std::array<MediaOutSlice, kMaxSocketWriteSlices - 1> body_slices{};
        size_t body_slice_size = 0;
        size_t body_size = 0;
        for (const HttpResponseBodySlice &slice : response.body_slices) {
            if (slice.size == 0) {
                continue;
            }
            if (slice.data == nullptr) {
                Error(kHttpModuleName,
                      "HTTP response reject conn=%llu reason=null_slice "
                      "status=%d",
                      static_cast<unsigned long long>(connection_id),
                      response.status_code);
                CloseConnection(socket_io, connection_id,
                                TcpCloseReason::kInternalError);
                return false;
            }
            body_slices[body_slice_size].data = slice.data;
            body_slices[body_slice_size].size = slice.size;
            body_slices[body_slice_size].buffer = slice.buffer;
            body_size += slice.size;
            ++body_slice_size;
        }
        return SendResponseSlices(socket_io, connection_id, response,
                                  body_slices.data(), body_slice_size,
                                  body_size, close_after_response);
    }

    MediaOutSlice body_slice;
    const MediaOutSlice *body_slices = nullptr;
    size_t body_slice_size = 0;
    if (!response.body.empty()) {
        body_slice.data =
            reinterpret_cast<const uint8_t *>(response.body.data());
        body_slice.size = response.body.size();
        body_slices = &body_slice;
        body_slice_size = 1;
    }
    return SendResponseSlices(socket_io, connection_id, response, body_slices,
                              body_slice_size, response.body.size(),
                              close_after_response);
}

bool HttpResponseSender::SendResponseSlices(
    ISocketIo *socket_io, ConnectionId connection_id,
    const HttpResponse &response, const MediaOutSlice *body_slices,
    size_t body_slice_size, size_t body_size,
    bool close_after_response) const {
    if (socket_io == nullptr) {
        return false;
    }
    if (body_slice_size > kMaxSocketWriteSlices - 1 ||
        (body_slice_size != 0 && body_slices == nullptr)) {
        return false;
    }

    std::map<std::string, std::string> response_headers = response.headers;
    response_headers["Connection"] =
        close_after_response ? "close" : "keep-alive";
    HttpResponse header_response;
    header_response.status_code = response.status_code;
    header_response.headers = std::move(response_headers);
    const std::string header =
        SerializeResponseHeaderWithBodySize(header_response, body_size);

    SocketWriteSlices slices;
    bool slices_ok = slices.Add(
        reinterpret_cast<const uint8_t *>(header.data()), header.size());
    for (size_t i = 0; slices_ok && i < body_slice_size; ++i) {
        slices_ok = slices.Add(body_slices[i].data, body_slices[i].size,
                               body_slices[i].buffer);
    }

    if (!slices_ok || !socket_io->SendSlices(connection_id, slices)) {
        if (response.status_code >= 500) {
            Error(kHttpModuleName,
                  "HTTP response send failed conn=%llu status=%d "
                  "body=%zu header=%zu close=%d",
                  static_cast<unsigned long long>(connection_id),
                  response.status_code, body_size, header.size(),
                  close_after_response ? 1 : 0);
        } else {
            Debug(kHttpModuleName,
                  "HTTP response send failed conn=%llu status=%d "
                  "body=%zu header=%zu close=%d",
                  static_cast<unsigned long long>(connection_id),
                  response.status_code, body_size, header.size(),
                  close_after_response ? 1 : 0);
        }
        CloseConnection(socket_io, connection_id,
                        TcpCloseReason::kInternalError);
        return false;
    }
    if (close_after_response) {
        (void)socket_io->CloseAfterSend(connection_id);
    }
    return true;
}

bool HttpResponseSender::EnqueueStreamingChunk(
    ISocketIo *socket_io, ConnectionId connection_id, const uint8_t *data,
    size_t size) const {
    SocketWriteSlices slices;
    if (data == nullptr || size == 0) {
        return true;
    }
    if (!slices.Add(data, size)) {
        return false;
    }
    return SendStreamingNetSlices(socket_io, connection_id, slices, size);
}

bool HttpResponseSender::EnqueueStreamingSlices(
    ISocketIo *socket_io, ConnectionId connection_id,
    const MediaOutSlice *slices, size_t slice_size) const {
    SocketWriteSlices net_slices;
    size_t total_size = 0;
    if (slice_size == 0) {
        return true;
    }
    if (slices == nullptr || slice_size > kMaxSocketWriteSlices) {
        return false;
    }
    for (size_t i = 0; i < slice_size; ++i) {
        if (slices[i].size == 0) {
            continue;
        }
        if (!net_slices.Add(slices[i].data, slices[i].size,
                            slices[i].buffer)) {
            return false;
        }
        total_size += slices[i].size;
    }
    if (total_size == 0) {
        return true;
    }
    return SendStreamingNetSlices(socket_io, connection_id, net_slices,
                                  total_size);
}

void HttpResponseSender::CloseConnection(
    ISocketIo *socket_io, ConnectionId connection_id,
    TcpCloseReason reason) const {
    if (socket_io != nullptr) {
        (void)socket_io->Close(connection_id, reason);
    }
}

bool HttpResponseSender::SendStreamingNetSlices(
    ISocketIo *socket_io, ConnectionId connection_id,
    const SocketWriteSlices &slices, size_t size) const {
    if (socket_io == nullptr) {
        Error(kHttpModuleName,
              "HTTP stream enqueue reject conn=%llu reason=no_net size=%zu",
              static_cast<unsigned long long>(connection_id), size);
        return false;
    }
    const uint32_t pending_bytes = socket_io->PendingBytes(connection_id);
    if (pending_bytes >= send_buffer_limit_bytes_ ||
        size > static_cast<size_t>(send_buffer_limit_bytes_ -
                                   pending_bytes)) {
        Error(kHttpModuleName,
              "HTTP stream close conn=%llu reason=pending_limit "
              "pending=%u limit=%zu next=%zu",
              static_cast<unsigned long long>(connection_id), pending_bytes,
              static_cast<size_t>(send_buffer_limit_bytes_), size);
        CloseConnection(socket_io, connection_id,
                        TcpCloseReason::kPendingLimit);
        return false;
    }
    if (!socket_io->SendSlices(connection_id, slices)) {
        Error(kHttpModuleName,
              "HTTP stream send failed conn=%llu size=%zu pending=%u",
              static_cast<unsigned long long>(connection_id), size,
              pending_bytes);
        CloseConnection(socket_io, connection_id,
                        TcpCloseReason::kInternalError);
        return false;
    }
    return true;
}

}  // namespace live_stream

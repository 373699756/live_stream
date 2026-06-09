#include "ice_transport.h"

#include "infra/log.h"
#include "stun_packet.h"

#include <utility>
#include <vector>

namespace live_stream {
namespace webrtc_internal {
namespace {

constexpr const char *kModuleName = "webrtc";

}  // namespace

IceTransport::IceTransport(std::string peer_id)
    : peer_id_(std::move(peer_id)) {}

IceTransport::~IceTransport() { Stop(); }

bool IceTransport::Start(INetEngine *net_engine, INetExecutor *net_executor,
                         const UdpCallbacks &callbacks,
                         const std::string &listen_ip, uint16_t port,
                         std::string local_ufrag,
                         std::string local_password) {
  if (socket_id_ != 0) {
    return true;
  }
  if (net_engine == nullptr || net_executor == nullptr ||
      local_ufrag.empty() || local_password.empty()) {
    return false;
  }
  UdpBindOptions options;
  options.address.ip = listen_ip.empty() ? "0.0.0.0" : listen_ip;
  options.address.port = port;
  UdpSocketId socket_id = net_engine->BindUdp(net_executor, options,
                                              callbacks);
  if (socket_id == 0) {
    return false;
  }
  net_engine_ = net_engine;
  socket_id_ = socket_id;
  local_address_ = net_engine_->UdpLocalAddress(socket_id_);
  local_ufrag_ = std::move(local_ufrag);
  local_password_ = std::move(local_password);
  if (local_address_.port == 0) {
    local_address_ = options.address;
  }
  return true;
}

void IceTransport::Stop() {
  if (net_engine_ != nullptr && socket_id_ != 0) {
    (void)net_engine_->CloseUdp(socket_id_);
  }
  net_engine_ = nullptr;
  socket_id_ = 0;
  local_address_ = NetAddress();
  local_ufrag_.clear();
  local_password_.clear();
  selected_pair_ = IceSelectedPair();
}

bool IceTransport::HandleUdpPacket(NetAddress peer, const uint8_t *data,
                                   size_t size, bool *connected_now) {
  if (connected_now != nullptr) {
    *connected_now = false;
  }
  if (net_engine_ == nullptr || socket_id_ == 0 || data == nullptr ||
      size == 0) {
    return false;
  }
  if (!IsStunPacket(data, size)) {
    return false;
  }

  StunBindingRequest request;
  const StunParseResult parse_result = ParseStunBindingRequest(
      data, size, local_ufrag_, local_password_, &request);
  if (parse_result != StunParseResult::kOk) {
    Warn(kModuleName, "ICE STUN rejected peer=%s remote=%s:%u reason=%s",
         peer_id_.c_str(), peer.ip.c_str(), static_cast<unsigned>(peer.port),
         StunParseResultName(parse_result));
    return false;
  }

  std::vector<uint8_t> response =
      BuildStunBindingSuccessResponse(request, local_password_, peer);
  if (response.empty() ||
      !net_engine_->SendTo(socket_id_, peer, response.data(), response.size())) {
    return false;
  }

  const bool was_connected = connected();
  selected_pair_.local = local_address_;
  selected_pair_.remote = std::move(peer);
  selected_pair_.priority = request.priority;
  selected_pair_.nominated = request.use_candidate;
  (void)net_engine_->SetUdpPeer(socket_id_, selected_pair_.remote);
  if (!was_connected && connected_now != nullptr) {
    *connected_now = true;
  }
  return true;
}

bool IceTransport::SendToSelected(const uint8_t *data, size_t size) {
  if (net_engine_ == nullptr || socket_id_ == 0 || !connected()) {
    return false;
  }
  return net_engine_->SendToPeer(socket_id_, data, size);
}

bool IceTransport::selected_pair(IceSelectedPair *pair) const {
  if (pair == nullptr || !connected()) {
    return false;
  }
  *pair = selected_pair_;
  return true;
}

}  // namespace webrtc_internal
}  // namespace live_stream

#include "stun_packet.h"

#include <openssl/hmac.h>

#include <algorithm>
#include <cstring>

namespace live_stream {
namespace webrtc_internal {
namespace {

constexpr size_t kStunHeaderSize = 20;
constexpr size_t kTransactionIdSize = 12;
constexpr uint32_t kMagicCookie = 0x2112A442U;
constexpr uint32_t kFingerprintXor = 0x5354554eU;
constexpr uint16_t kBindingRequestType = 0x0001;
constexpr uint16_t kBindingSuccessResponseType = 0x0101;
constexpr uint16_t kAttrUsername = 0x0006;
constexpr uint16_t kAttrMessageIntegrity = 0x0008;
constexpr uint16_t kAttrXorMappedAddress = 0x0020;
constexpr uint16_t kAttrPriority = 0x0024;
constexpr uint16_t kAttrUseCandidate = 0x0025;
constexpr uint16_t kAttrFingerprint = 0x8028;

constexpr uint32_t kCrc32Table[] = {
    0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU, 0x076dc419U,
    0x706af48fU, 0xe963a535U, 0x9e6495a3U, 0x0edb8832U, 0x79dcb8a4U,
    0xe0d5e91eU, 0x97d2d988U, 0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U,
    0x90bf1d91U, 0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
    0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U, 0x136c9856U,
    0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU, 0x14015c4fU, 0x63066cd9U,
    0xfa0f3d63U, 0x8d080df5U, 0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U,
    0xa2677172U, 0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
    0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U, 0x32d86ce3U,
    0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U, 0x26d930acU, 0x51de003aU,
    0xc8d75180U, 0xbfd06116U, 0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U,
    0xb8bda50fU, 0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
    0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU, 0x76dc4190U,
    0x01db7106U, 0x98d220bcU, 0xefd5102aU, 0x71b18589U, 0x06b6b51fU,
    0x9fbfe4a5U, 0xe8b8d433U, 0x7807c9a2U, 0x0f00f934U, 0x9609a88eU,
    0xe10e9818U, 0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
    0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU, 0x6c0695edU,
    0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U, 0x65b0d9c6U, 0x12b7e950U,
    0x8bbeb8eaU, 0xfcb9887cU, 0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U,
    0xfbd44c65U, 0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
    0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU, 0x4369e96aU,
    0x346ed9fcU, 0xad678846U, 0xda60b8d0U, 0x44042d73U, 0x33031de5U,
    0xaa0a4c5fU, 0xdd0d7cc9U, 0x5005713cU, 0x270241aaU, 0xbe0b1010U,
    0xc90c2086U, 0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
    0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U, 0x59b33d17U,
    0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU, 0xedb88320U, 0x9abfb3b6U,
    0x03b6e20cU, 0x74b1d29aU, 0xead54739U, 0x9dd277afU, 0x04db2615U,
    0x73dc1683U, 0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
    0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U, 0xf00f9344U,
    0x8708a3d2U, 0x1e01f268U, 0x6906c2feU, 0xf762575dU, 0x806567cbU,
    0x196c3671U, 0x6e6b06e7U, 0xfed41b76U, 0x89d32be0U, 0x10da7a5aU,
    0x67dd4accU, 0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
    0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U, 0xd1bb67f1U,
    0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU, 0xd80d2bdaU, 0xaf0a1b4cU,
    0x36034af6U, 0x41047a60U, 0xdf60efc3U, 0xa867df55U, 0x316e8eefU,
    0x4669be79U, 0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
    0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU, 0xc5ba3bbeU,
    0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U, 0xc2d7ffa7U, 0xb5d0cf31U,
    0x2cd99e8bU, 0x5bdeae1dU, 0x9b64c2b0U, 0xec63f226U, 0x756aa39cU,
    0x026d930aU, 0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
    0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U, 0x92d28e9bU,
    0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U, 0x86d3d2d4U, 0xf1d4e242U,
    0x68ddb3f8U, 0x1fda836eU, 0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U,
    0x18b74777U, 0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
    0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U, 0xa00ae278U,
    0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U, 0xa7672661U, 0xd06016f7U,
    0x4969474dU, 0x3e6e77dbU, 0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U,
    0x37d83bf0U, 0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
    0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U, 0xbad03605U,
    0xcdd70693U, 0x54de5729U, 0x23d967bfU, 0xb3667a2eU, 0xc4614ab8U,
    0x5d681b02U, 0x2a6f2b94U, 0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU,
    0x2d02ef8dU};

uint16_t Read16(const uint8_t *data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               data[1]);
}

uint32_t Read32(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

void Write16(std::vector<uint8_t> *data, uint16_t value) {
  data->push_back(static_cast<uint8_t>((value >> 8) & 0xffU));
  data->push_back(static_cast<uint8_t>(value & 0xffU));
}

void Write32(std::vector<uint8_t> *data, uint32_t value) {
  data->push_back(static_cast<uint8_t>((value >> 24) & 0xffU));
  data->push_back(static_cast<uint8_t>((value >> 16) & 0xffU));
  data->push_back(static_cast<uint8_t>((value >> 8) & 0xffU));
  data->push_back(static_cast<uint8_t>(value & 0xffU));
}

void Set16(std::vector<uint8_t> *data, size_t offset, uint16_t value) {
  (*data)[offset] = static_cast<uint8_t>((value >> 8) & 0xffU);
  (*data)[offset + 1] = static_cast<uint8_t>(value & 0xffU);
}

size_t PaddedSize(size_t size) {
  return (size + 3U) & ~static_cast<size_t>(3U);
}

uint32_t Crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < size; ++i) {
    crc = kCrc32Table[(crc ^ data[i]) & 0xffU] ^ (crc >> 8);
  }
  return crc ^ 0xffffffffU;
}

std::array<uint8_t, 20> HmacSha1(const std::string &key,
                                 const uint8_t *data, size_t size) {
  std::array<uint8_t, 20> digest{};
  unsigned int digest_size = 0;
  (void)HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()), data, size,
             digest.data(), &digest_size);
  return digest;
}

bool CheckHeader(const uint8_t *data, size_t size) {
  if (data == nullptr || size < kStunHeaderSize || (data[0] & 0xc0U) != 0) {
    return false;
  }
  if (Read32(data + 4) != kMagicCookie) {
    return false;
  }
  const uint16_t message_length = Read16(data + 2);
  return (message_length % 4U) == 0 &&
         static_cast<size_t>(message_length) + kStunHeaderSize == size;
}

bool UsernameMatchesLocalUfrag(const std::string &username,
                               const std::string &local_ufrag) {
  if (username.size() <= local_ufrag.size() ||
      username[local_ufrag.size()] != ':') {
    return false;
  }
  return username.compare(0, local_ufrag.size(), local_ufrag) == 0;
}

void AddAttribute(std::vector<uint8_t> *data, uint16_t type,
                  const uint8_t *value, size_t value_size) {
  Write16(data, type);
  Write16(data, static_cast<uint16_t>(value_size));
  if (value_size > 0) {
    data->insert(data->end(), value, value + value_size);
  }
  const size_t padding_size = PaddedSize(value_size) - value_size;
  for (size_t i = 0; i < padding_size; ++i) {
    data->push_back(0);
  }
}

void AddXorMappedAddress(std::vector<uint8_t> *data, const NetAddress &peer) {
  uint32_t parts[4] = {0, 0, 0, 0};
  size_t part_index = 0;
  size_t start = 0;
  for (size_t i = 0; i <= peer.ip.size() && part_index < 4; ++i) {
    if (i != peer.ip.size() && peer.ip[i] != '.') {
      continue;
    }
    if (i == start) {
      return;
    }
    uint32_t value = 0;
    for (size_t j = start; j < i; ++j) {
      if (peer.ip[j] < '0' || peer.ip[j] > '9') {
        return;
      }
      value = value * 10U + static_cast<uint32_t>(peer.ip[j] - '0');
      if (value > 255U) {
        return;
      }
    }
    parts[part_index++] = value;
    start = i + 1;
  }
  if (part_index != 4) {
    return;
  }

  uint8_t value[8] = {};
  value[1] = 0x01;
  const uint16_t xport =
      static_cast<uint16_t>(peer.port ^ static_cast<uint16_t>(kMagicCookie >> 16));
  value[2] = static_cast<uint8_t>((xport >> 8) & 0xffU);
  value[3] = static_cast<uint8_t>(xport & 0xffU);
  const uint32_t address = (parts[0] << 24) | (parts[1] << 16) |
                           (parts[2] << 8) | parts[3];
  const uint32_t xaddress = address ^ kMagicCookie;
  value[4] = static_cast<uint8_t>((xaddress >> 24) & 0xffU);
  value[5] = static_cast<uint8_t>((xaddress >> 16) & 0xffU);
  value[6] = static_cast<uint8_t>((xaddress >> 8) & 0xffU);
  value[7] = static_cast<uint8_t>(xaddress & 0xffU);
  AddAttribute(data, kAttrXorMappedAddress, value, sizeof(value));
}

}  // namespace

bool IsStunPacket(const uint8_t *data, size_t size) {
  return CheckHeader(data, size);
}

StunParseResult ParseStunBindingRequest(const uint8_t *data, size_t size,
                                        const std::string &local_ufrag,
                                        const std::string &local_password,
                                        StunBindingRequest *request) {
  if (request == nullptr) {
    return StunParseResult::kMalformed;
  }
  *request = StunBindingRequest();
  if (!CheckHeader(data, size)) {
    return StunParseResult::kNotStun;
  }
  if (Read16(data) != kBindingRequestType) {
    return StunParseResult::kUnsupported;
  }

  std::array<uint8_t, 20> message_integrity{};
  size_t message_integrity_offset = 0;
  bool has_message_integrity = false;
  bool has_fingerprint = false;
  uint32_t fingerprint = 0;

  std::copy(data + 8, data + 8 + kTransactionIdSize,
            request->transaction_id.begin());
  size_t offset = kStunHeaderSize;
  while (offset < size) {
    if (offset + 4 > size) {
      return StunParseResult::kMalformed;
    }
    const uint16_t type = Read16(data + offset);
    const uint16_t attr_size = Read16(data + offset + 2);
    const size_t value_offset = offset + 4;
    const size_t next_offset = value_offset + PaddedSize(attr_size);
    if (value_offset + attr_size > size || next_offset > size) {
      return StunParseResult::kMalformed;
    }
    switch (type) {
      case kAttrUsername:
        request->username.assign(
            reinterpret_cast<const char *>(data + value_offset), attr_size);
        break;
      case kAttrMessageIntegrity:
        if (attr_size != message_integrity.size()) {
          return StunParseResult::kMalformed;
        }
        std::copy(data + value_offset, data + value_offset + attr_size,
                  message_integrity.begin());
        has_message_integrity = true;
        message_integrity_offset = offset;
        break;
      case kAttrFingerprint:
        if (attr_size != 4) {
          return StunParseResult::kMalformed;
        }
        has_fingerprint = true;
        fingerprint = Read32(data + value_offset);
        break;
      case kAttrPriority:
        if (attr_size == 4) {
          request->priority = Read32(data + value_offset);
        }
        break;
      case kAttrUseCandidate:
        request->use_candidate = true;
        break;
      default:
        break;
    }
    offset = next_offset;
  }

  if (!UsernameMatchesLocalUfrag(request->username, local_ufrag)) {
    return StunParseResult::kBadUsername;
  }
  if (!has_message_integrity) {
    return StunParseResult::kBadMessageIntegrity;
  }
  std::vector<uint8_t> integrity_data(data, data + message_integrity_offset);
  const uint16_t integrity_length =
      static_cast<uint16_t>(message_integrity_offset + 24 - kStunHeaderSize);
  Set16(&integrity_data, 2, integrity_length);
  const std::array<uint8_t, 20> expected =
      HmacSha1(local_password, integrity_data.data(), integrity_data.size());
  if (expected != message_integrity) {
    return StunParseResult::kBadMessageIntegrity;
  }
  if (has_fingerprint) {
    if (size < 8) {
      return StunParseResult::kMalformed;
    }
    const uint32_t expected_fingerprint =
        Crc32(data, size - 8) ^ kFingerprintXor;
    if (expected_fingerprint != fingerprint) {
      return StunParseResult::kBadFingerprint;
    }
  }
  request->has_message_integrity = has_message_integrity;
  request->has_fingerprint = has_fingerprint;
  return StunParseResult::kOk;
}

std::vector<uint8_t> BuildStunBindingSuccessResponse(
    const StunBindingRequest &request, const std::string &local_password,
    const NetAddress &peer) {
  std::vector<uint8_t> response;
  response.reserve(kStunHeaderSize + 40);
  Write16(&response, kBindingSuccessResponseType);
  Write16(&response, 0);
  Write32(&response, kMagicCookie);
  response.insert(response.end(), request.transaction_id.begin(),
                  request.transaction_id.end());

  AddXorMappedAddress(&response, peer);
  const uint16_t length_with_integrity =
      static_cast<uint16_t>(response.size() - kStunHeaderSize + 24);
  Set16(&response, 2, length_with_integrity);
  const std::array<uint8_t, 20> integrity =
      HmacSha1(local_password, response.data(), response.size());
  AddAttribute(&response, kAttrMessageIntegrity, integrity.data(),
               integrity.size());

  const uint16_t final_length =
      static_cast<uint16_t>(response.size() - kStunHeaderSize + 8);
  Set16(&response, 2, final_length);
  const uint32_t fingerprint = Crc32(response.data(), response.size()) ^
                               kFingerprintXor;
  uint8_t fingerprint_data[4] = {
      static_cast<uint8_t>((fingerprint >> 24) & 0xffU),
      static_cast<uint8_t>((fingerprint >> 16) & 0xffU),
      static_cast<uint8_t>((fingerprint >> 8) & 0xffU),
      static_cast<uint8_t>(fingerprint & 0xffU)};
  AddAttribute(&response, kAttrFingerprint, fingerprint_data,
               sizeof(fingerprint_data));
  return response;
}

const char *StunParseResultName(StunParseResult result) {
  switch (result) {
    case StunParseResult::kOk:
      return "ok";
    case StunParseResult::kNotStun:
      return "not_stun";
    case StunParseResult::kUnsupported:
      return "unsupported";
    case StunParseResult::kMalformed:
      return "malformed";
    case StunParseResult::kBadUsername:
      return "bad_username";
    case StunParseResult::kBadMessageIntegrity:
      return "bad_message_integrity";
    case StunParseResult::kBadFingerprint:
      return "bad_fingerprint";
  }
  return "unknown";
}

}  // namespace webrtc_internal
}  // namespace live_stream

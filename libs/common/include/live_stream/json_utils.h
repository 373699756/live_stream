#ifndef LIVE_STREAM_COMMON_JSON_UTILS_H_
#define LIVE_STREAM_COMMON_JSON_UTILS_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "live_stream/config_json.h"

namespace live_stream {
namespace json_utils {
namespace detail {

inline const ConfigJson *FindField(const ConfigJson &object, const char *key) {
  if (!object.is_object() || key == nullptr || !object.contains(key)) {
    return nullptr;
  }
  return &object.at(key);
}

inline bool ReadSignedInteger(const ConfigJson &field, int64_t *value) {
  if (value == nullptr || !field.is_number_integer()) {
    return false;
  }
  *value = field.get<int64_t>();
  return true;
}

inline bool ReadUnsignedInteger(const ConfigJson &field, uint64_t *value) {
  if (value == nullptr) {
    return false;
  }
  if (field.is_number_unsigned()) {
    *value = field.get<uint64_t>();
    return true;
  }
  if (!field.is_number_integer()) {
    return false;
  }
  const int64_t signed_value = field.get<int64_t>();
  if (signed_value < 0) {
    return false;
  }
  *value = static_cast<uint64_t>(signed_value);
  return true;
}

} // namespace detail

inline bool Load(const ConfigJson &object, const char *key,
                 std::string *value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = detail::FindField(object, key);
  if (field == nullptr || !field->is_string()) {
    return false;
  }
  *value = field->get<std::string>();
  return true;
}

inline bool Load(const ConfigJson &object, const char *key, bool *value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = detail::FindField(object, key);
  if (field == nullptr || !field->is_boolean()) {
    return false;
  }
  *value = field->get<bool>();
  return true;
}

inline bool LoadObject(const ConfigJson &object, const char *key,
                       const ConfigJson **value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = detail::FindField(object, key);
  if (field == nullptr || !field->is_object()) {
    return false;
  }
  *value = field;
  return true;
}

inline bool LoadArray(const ConfigJson &object, const char *key,
                      const ConfigJson **value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = detail::FindField(object, key);
  if (field == nullptr || !field->is_array()) {
    return false;
  }
  *value = field;
  return true;
}

inline bool LoadStringArray(const ConfigJson &object, const char *key,
                            std::vector<std::string> *value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = nullptr;
  if (!LoadArray(object, key, &field)) {
    return false;
  }
  std::vector<std::string> parsed;
  parsed.reserve(field->size());
  for (const ConfigJson &item : *field) {
    if (!item.is_string()) {
      return false;
    }
    parsed.push_back(item.get<std::string>());
  }
  *value = std::move(parsed);
  return true;
}

inline bool Load(const ConfigJson &object, const char *key, int64_t *value,
                 int64_t min_value, int64_t max_value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = detail::FindField(object, key);
  if (field == nullptr) {
    return false;
  }
  int64_t parsed = 0;
  if (!detail::ReadSignedInteger(*field, &parsed) || parsed < min_value ||
      parsed > max_value) {
    return false;
  }
  *value = parsed;
  return true;
}

inline bool Load(const ConfigJson &object, const char *key, int64_t *value) {
  return Load(object, key, value, std::numeric_limits<int64_t>::min(),
              std::numeric_limits<int64_t>::max());
}

inline bool Load(const ConfigJson &object, const char *key, int32_t *value,
                 int32_t min_value, int32_t max_value) {
  if (value == nullptr) {
    return false;
  }
  int64_t parsed = 0;
  if (!Load(object, key, &parsed, static_cast<int64_t>(min_value),
            static_cast<int64_t>(max_value))) {
    return false;
  }
  *value = static_cast<int32_t>(parsed);
  return true;
}

inline bool Load(const ConfigJson &object, const char *key, int32_t *value) {
  return Load(object, key, value, std::numeric_limits<int32_t>::min(),
              std::numeric_limits<int32_t>::max());
}

inline bool Load(const ConfigJson &object, const char *key, uint32_t *value,
                 uint32_t min_value, uint32_t max_value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = detail::FindField(object, key);
  if (field == nullptr) {
    return false;
  }
  uint64_t parsed = 0;
  if (!detail::ReadUnsignedInteger(*field, &parsed) || parsed < min_value ||
      parsed > max_value) {
    return false;
  }
  *value = static_cast<uint32_t>(parsed);
  return true;
}

inline bool Load(const ConfigJson &object, const char *key, uint32_t *value) {
  return Load(object, key, value, 0, std::numeric_limits<uint32_t>::max());
}

inline bool Load(const ConfigJson &object, const char *key, uint16_t *value,
                 uint16_t min_value, uint16_t max_value) {
  if (value == nullptr) {
    return false;
  }
  uint32_t parsed = 0;
  if (!Load(object, key, &parsed, static_cast<uint32_t>(min_value),
            static_cast<uint32_t>(max_value))) {
    return false;
  }
  *value = static_cast<uint16_t>(parsed);
  return true;
}

inline bool Load(const ConfigJson &object, const char *key, uint16_t *value) {
  return Load(object, key, value, 0, std::numeric_limits<uint16_t>::max());
}

inline bool Load(const ConfigJson &object, const char *key, uint8_t *value,
                 uint8_t min_value, uint8_t max_value) {
  if (value == nullptr) {
    return false;
  }
  uint32_t parsed = 0;
  if (!Load(object, key, &parsed, static_cast<uint32_t>(min_value),
            static_cast<uint32_t>(max_value))) {
    return false;
  }
  *value = static_cast<uint8_t>(parsed);
  return true;
}

inline bool Load(const ConfigJson &object, const char *key, uint8_t *value) {
  return Load(object, key, value, 0, std::numeric_limits<uint8_t>::max());
}

inline bool Load(const ConfigJson &object, const char *key, float *value,
                 float min_value, float max_value) {
  if (value == nullptr) {
    return false;
  }
  const ConfigJson *field = detail::FindField(object, key);
  if (field == nullptr || !field->is_number()) {
    return false;
  }
  const float parsed = field->get<float>();
  if (!std::isfinite(parsed) || parsed < min_value || parsed > max_value) {
    return false;
  }
  *value = parsed;
  return true;
}

} // namespace json_utils
} // namespace live_stream

#endif // LIVE_STREAM_COMMON_JSON_UTILS_H_

#ifndef LIVE_STREAM_INFRA_JSON_READER_H_
#define LIVE_STREAM_INFRA_JSON_READER_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "json.h"

namespace live_stream {
namespace json_reader {
namespace detail {

inline const Json *FindField(const Json &object, const char *key) {
    if (!object.is_object() || key == nullptr || !object.contains(key)) {
        return nullptr;
    }
    return &object.at(key);
}

inline bool ReadSignedInteger(const Json &field, int64_t *value) {
    if (value == nullptr || !field.is_number_integer()) {
        return false;
    }
    *value = field.get<int64_t>();
    return true;
}

inline bool ReadUnsignedInteger(const Json &field, uint64_t *value) {
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

}  // namespace detail

inline bool ReadField(const Json &object, const char *key,
                      std::string *value) {
    if (value == nullptr) {
        return false;
    }
    const Json *field = detail::FindField(object, key);
    if (field == nullptr || !field->is_string()) {
        return false;
    }
    *value = field->get<std::string>();
    return true;
}

inline bool ReadField(const Json &object, const char *key, bool *value) {
    if (value == nullptr) {
        return false;
    }
    const Json *field = detail::FindField(object, key);
    if (field == nullptr || !field->is_boolean()) {
        return false;
    }
    *value = field->get<bool>();
    return true;
}

inline bool ReadStringArray(const Json &object, const char *key,
                            std::vector<std::string> *value) {
    if (value == nullptr) {
        return false;
    }
    const Json *field = detail::FindField(object, key);
    if (field == nullptr || !field->is_array()) {
        return false;
    }
    std::vector<std::string> parsed;
    parsed.reserve(field->size());
    for (const Json &item : *field) {
        if (!item.is_string()) {
            return false;
        }
        parsed.push_back(item.get<std::string>());
    }
    *value = std::move(parsed);
    return true;
}

inline bool ReadField(const Json &object, const char *key,
                      int64_t *value, int64_t min_value,
                      int64_t max_value) {
    if (value == nullptr) {
        return false;
    }
    const Json *field = detail::FindField(object, key);
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

inline bool ReadField(const Json &object, const char *key,
                      int64_t *value) {
    return ReadField(object, key, value, std::numeric_limits<int64_t>::min(),
                     std::numeric_limits<int64_t>::max());
}

inline bool ReadField(const Json &object, const char *key,
                      int32_t *value, int32_t min_value,
                      int32_t max_value) {
    if (value == nullptr) {
        return false;
    }
    int64_t parsed = 0;
    if (!ReadField(object, key, &parsed, static_cast<int64_t>(min_value),
                   static_cast<int64_t>(max_value))) {
        return false;
    }
    *value = static_cast<int32_t>(parsed);
    return true;
}

inline bool ReadField(const Json &object, const char *key,
                      int32_t *value) {
    return ReadField(object, key, value, std::numeric_limits<int32_t>::min(),
                     std::numeric_limits<int32_t>::max());
}

inline bool ReadField(const Json &object, const char *key,
                      uint32_t *value, uint32_t min_value,
                      uint32_t max_value) {
    if (value == nullptr) {
        return false;
    }
    const Json *field = detail::FindField(object, key);
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

inline bool ReadField(const Json &object, const char *key,
                      uint32_t *value) {
    return ReadField(object, key, value, 0,
                     std::numeric_limits<uint32_t>::max());
}

inline bool ReadField(const Json &object, const char *key,
                      uint16_t *value, uint16_t min_value,
                      uint16_t max_value) {
    if (value == nullptr) {
        return false;
    }
    uint32_t parsed = 0;
    if (!ReadField(object, key, &parsed, static_cast<uint32_t>(min_value),
                   static_cast<uint32_t>(max_value))) {
        return false;
    }
    *value = static_cast<uint16_t>(parsed);
    return true;
}

inline bool ReadField(const Json &object, const char *key,
                      float *value, float min_value, float max_value) {
    if (value == nullptr) {
        return false;
    }
    const Json *field = detail::FindField(object, key);
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

}  // namespace json_reader
}  // namespace live_stream

#endif  // LIVE_STREAM_INFRA_JSON_READER_H_

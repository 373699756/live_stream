/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: status.h
 * Brief: Defines infra status codes and value-return result type.
 */

#ifndef LIVE_STREAM_INFRA_STATUS_H_
#define LIVE_STREAM_INFRA_STATUS_H_

#include <utility>

namespace infra {

enum class Status {
    kOk = 0,
    kInvalidParam,
    kNotFound,
    kAlreadyExists,
    kNoPermission,
    kUnauthorized,
    kTimeout,
    kBusy,
    kNoMemory,
    kNotSupported,
    kIoError,
    kInternalError,
};

const char* StatusToString(Status status);

template <typename T>
struct Result {
    Status status;
    T value;

    bool IsOk() const { return status == Status::kOk; }

    static Result<T> Ok(T result_value) {
        return Result<T>{Status::kOk, std::move(result_value)};
    }

    static Result<T> Fail(Status result_status) {
        return Result<T>{result_status, T{}};
    }
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_STATUS_H_

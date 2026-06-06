/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: time.h
 * Brief: Defines high-resolution monotonic and wall-clock time helpers.
 */

#ifndef LIVE_STREAM_INFRA_TIME_H_
#define LIVE_STREAM_INFRA_TIME_H_

#include <cstdint>

namespace infra {

class Time {
public:
    static int64_t MonotonicMillis();
    static int64_t MonotonicNanos();
    static int64_t SystemTimeMillis();
    static void SleepMillis(uint32_t millis);
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_TIME_H_

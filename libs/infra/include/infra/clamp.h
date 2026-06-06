#ifndef LIVE_STREAM_INFRA_CLAMP_H_
#define LIVE_STREAM_INFRA_CLAMP_H_

namespace infra {

template <typename T>
constexpr T Clamp(T value, T min_value, T max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_CLAMP_H_

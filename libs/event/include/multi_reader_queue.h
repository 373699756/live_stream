#ifndef LIVE_STREAM_EVENT_MULTI_READER_QUEUE_H_
#define LIVE_STREAM_EVENT_MULTI_READER_QUEUE_H_

#include <array>
#include <cstddef>
#include <cstdint>

namespace live_stream {
namespace event {

template <typename Value, size_t kCapacity>
class MultiReaderQueue {
public:
    MultiReaderQueue() = default;
    MultiReaderQueue(const MultiReaderQueue &) = delete;
    MultiReaderQueue &operator=(const MultiReaderQueue &) = delete;

    void Configure(uint32_t max_items, uint32_t max_bytes) {
        max_items_ = max_items;
        max_bytes_ = max_bytes;
    }

    void Clear() {
        for (Value &value : values_) {
            value = Value{};
        }
        for (uint32_t &bytes : value_bytes_) {
            bytes = 0;
        }
        head_ = 0;
        size_ = 0;
        bytes_ = 0;
        first_sequence_ = next_sequence_;
    }

    bool Push(const Value &value, uint32_t bytes, uint64_t *sequence) {
        const uint64_t write_sequence = next_sequence_++;
        if (sequence != nullptr) {
            *sequence = write_sequence;
        }

        const size_t active_capacity = ActiveCapacity();
        if (active_capacity == 0 || bytes > max_bytes_) {
            Clear();
            first_sequence_ = next_sequence_;
            return false;
        }

        while (size_ > 0 &&
               (size_ >= active_capacity || bytes_ > max_bytes_ - bytes)) {
            DropOldest();
        }
        if (size_ == 0) {
            head_ = 0;
            first_sequence_ = write_sequence;
        }

        const size_t index = (head_ + size_) % values_.size();
        values_[index] = value;
        value_bytes_[index] = bytes;
        bytes_ += bytes;
        ++size_;
        return true;
    }

    bool Read(uint64_t sequence, Value *value) const {
        if (value == nullptr || sequence < first_sequence_ ||
            sequence >= next_sequence_) {
            return false;
        }
        const uint64_t offset = sequence - first_sequence_;
        if (offset >= size_) {
            return false;
        }
        const size_t index = (head_ + static_cast<size_t>(offset)) %
                             values_.size();
        *value = values_[index];
        return true;
    }

    uint64_t FirstSequence() const { return first_sequence_; }
    uint64_t NextSequence() const { return next_sequence_; }
    uint64_t EvictedSize() const { return evicted_size_; }
    uint32_t Size() const { return static_cast<uint32_t>(size_); }
    uint32_t Bytes() const { return bytes_; }

private:
    size_t ActiveCapacity() const {
        return max_items_ < values_.size() ? max_items_ : values_.size();
    }

    void DropOldest() {
        bytes_ = value_bytes_[head_] > bytes_
                     ? 0
                     : bytes_ - value_bytes_[head_];
        values_[head_] = Value{};
        value_bytes_[head_] = 0;
        head_ = (head_ + 1) % values_.size();
        --size_;
        ++first_sequence_;
        ++evicted_size_;
    }

    std::array<Value, kCapacity> values_;
    std::array<uint32_t, kCapacity> value_bytes_ = {};
    size_t head_ = 0;
    size_t size_ = 0;
    uint32_t bytes_ = 0;
    uint32_t max_items_ = static_cast<uint32_t>(kCapacity);
    uint32_t max_bytes_ = UINT32_MAX;
    uint64_t first_sequence_ = 1;
    uint64_t next_sequence_ = 1;
    uint64_t evicted_size_ = 0;
};

}  // namespace event
}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_MULTI_READER_QUEUE_H_

#ifndef LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
#define LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_

#include <cstdint>
#include <memory>

namespace live_stream {

class IMediaBuffer {
 public:
  virtual ~IMediaBuffer() = default;

  virtual uint8_t* MutableData() = 0;
  virtual const uint8_t* Data() const = 0;
  virtual uint32_t Size() const = 0;
  virtual uint32_t Capacity() const = 0;
  virtual bool SetSize(uint32_t size) = 0;
};

struct BufferSlice {
  std::shared_ptr<IMediaBuffer> buffer;
  uint32_t offset = 0;
  uint32_t size = 0;
};

struct MediaBufferPoolStats {
  uint32_t block_size = 0;
  uint32_t block_count = 0;
  uint32_t free_count = 0;
  uint32_t in_use_count = 0;
  uint32_t high_water_count = 0;
  uint64_t no_memory_count = 0;
};

class IMediaBufferPool {
 public:
  virtual ~IMediaBufferPool() = default;

  virtual std::shared_ptr<IMediaBuffer> Acquire() = 0;
  virtual MediaBufferPoolStats Stats() const = 0;
};

std::shared_ptr<IMediaBuffer> CreateMediaBuffer(uint32_t capacity);

std::shared_ptr<IMediaBufferPool> CreateFixedMediaBufferPool(
    uint32_t block_size, uint32_t block_count);

bool IsValidBufferSlice(const BufferSlice& slice);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_

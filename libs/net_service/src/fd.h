#ifndef LIVE_STREAM_NETFRAME_SERVICE_SRC_FD_H_
#define LIVE_STREAM_NETFRAME_SERVICE_SRC_FD_H_

namespace live_stream {
namespace net_internal {

class UniqueFd {
public:
  UniqueFd() = default;
  explicit UniqueFd(int fd) : fd_(fd) {}
  ~UniqueFd();

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  UniqueFd(UniqueFd &&other) noexcept;
  UniqueFd &operator=(UniqueFd &&other) noexcept;

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }

  int Release();
  void Reset(int fd = -1);

private:
  int fd_ = -1;
};

} // namespace net_internal
} // namespace live_stream

#endif // LIVE_STREAM_NETFRAME_SERVICE_SRC_FD_H_

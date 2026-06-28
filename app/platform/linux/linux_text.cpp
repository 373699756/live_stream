#include "platform/linux/linux_text.h"

#include "infra/fs.h"

namespace live_stream {
namespace linux_platform {
namespace {

bool IsTextEdgeChar(char value) {
    const unsigned char byte = static_cast<unsigned char>(value);
    return byte <= static_cast<unsigned char>(' ');
}

}  // namespace

std::string Trim(const std::string &value) {
    std::size_t begin = 0;
    while (begin < value.size() && IsTextEdgeChar(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && IsTextEdgeChar(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string ReadFirstText(const std::vector<std::string> &paths) {
    for (const std::string &path : paths) {
        const std::string value = Trim(infra::File::ReadAll(path));
        if (!value.empty()) {
            return value;
        }
    }
    return std::string();
}

}  // namespace linux_platform
}  // namespace live_stream

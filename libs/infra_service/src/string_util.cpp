#include "infra/string_util.h"

#include <cctype>

namespace infra {

std::string StringUtil::Trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() &&
           std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
        ++begin;
    }

    size_t end = input.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return input.substr(begin, end - begin);
}

std::vector<std::string> StringUtil::Split(const std::string& input,
                                           char delimiter) {
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin <= input.size()) {
        const size_t pos = input.find(delimiter, begin);
        if (pos == std::string::npos) {
            parts.push_back(input.substr(begin));
            break;
        }
        parts.push_back(input.substr(begin, pos - begin));
        begin = pos + 1;
    }
    return parts;
}

bool StringUtil::StartsWith(const std::string& input,
                            const std::string& prefix) {
    return input.size() >= prefix.size() &&
           input.compare(0, prefix.size(), prefix) == 0;
}

bool StringUtil::EndsWith(const std::string& input,
                          const std::string& suffix) {
    return input.size() >= suffix.size() &&
           input.compare(input.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace infra

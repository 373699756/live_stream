#include "infra/time.h"
#include "infra/fs.h"
#include "infra/string_util.h"

int main() {
    const std::string dir = "/tmp/live_stream_infra_test_dir/sub";
    const std::string file_path = infra::Path::Join(dir, "data.txt");

    if (infra::Path::MakeDirs(dir) != infra::Status::kOk) {
        return 1;
    }
    if (!infra::Path::Exists(dir)) {
        return 2;
    }
    if (infra::Path::BaseName(file_path) != "data.txt") {
        return 3;
    }
    if (infra::Path::DirName(file_path) != dir) {
        return 4;
    }

    if (infra::File::WriteAll(file_path, "abc") != infra::Status::kOk) {
        return 5;
    }
    if (infra::File::Append(file_path, "def") != infra::Status::kOk) {
        return 6;
    }
    infra::Result<std::string> data = infra::File::ReadAll(file_path);
    if (!data.IsOk() || data.value != "abcdef") {
        return 7;
    }
    infra::Result<uint64_t> size = infra::File::Size(file_path);
    if (!size.IsOk() || size.value != 6U) {
        return 8;
    }

    if (infra::StringUtil::Trim("  value \n") != "value") {
        return 9;
    }
    if (!infra::StringUtil::StartsWith("abcdef", "abc") ||
        !infra::StringUtil::EndsWith("abcdef", "def")) {
        return 10;
    }
    const std::vector<std::string> parts = infra::StringUtil::Split("a,b,", ',');
    if (parts.size() != 3 || parts[0] != "a" || parts[1] != "b" ||
        parts[2] != "") {
        return 11;
    }

    const int64_t before = infra::Time::MonotonicMillis();
    infra::Time::SleepMillis(1);
    const int64_t after = infra::Time::MonotonicMillis();
    if (after < before) {
        return 12;
    }

    infra::File::Remove(file_path);
    return 0;
}

#include "infra/fs.h"
#include "infra/time.h"

int main() {
    const std::string dir = "/tmp/live_stream_infra_test_dir/sub";
    const std::string file_path = infra::Path::Join(dir, "data.txt");

    if (!infra::Path::MakeDirs(dir)) {
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

    if (!infra::File::WriteAll(file_path, "abc")) {
        return 5;
    }
    if (!infra::File::Append(file_path, "def")) {
        return 6;
    }
    if (infra::File::ReadAll(file_path) != "abcdef") {
        return 7;
    }
    if (infra::File::Size(file_path) != 6U) {
        return 8;
    }

    const int64_t before = infra::Time::MonotonicMillis();
    infra::Time::SleepMillis(1);
    const int64_t after = infra::Time::MonotonicMillis();
    if (after < before) {
        return 9;
    }

    infra::File::Remove(file_path);
    return 0;
}

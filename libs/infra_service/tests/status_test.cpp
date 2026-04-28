#include "infra/status.h"

#include <cstring>

int main() {
    if (std::strcmp(infra::StatusToString(infra::Status::kOk), "Ok") != 0) {
        return 1;
    }
    if (std::strcmp(infra::StatusToString(infra::Status::kInvalidParam),
                    "InvalidParam") != 0) {
        return 2;
    }
    if (std::strcmp(infra::StatusToString(static_cast<infra::Status>(999)),
                    "Unknown") != 0) {
        return 3;
    }
    return 0;
}


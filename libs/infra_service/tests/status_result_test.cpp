#include "infra/status.h"

#include <string>

int main() {
    infra::Result<int> int_result{infra::Status::kOk, 7};
    infra::Result<std::string> string_result{infra::Status::kOk, "ok"};
    infra::Result<int> ok_result = infra::Result<int>::Ok(9);
    infra::Result<int> fail_result =
        infra::Result<int>::Fail(infra::Status::kBusy);

    if (int_result.status != infra::Status::kOk || int_result.value != 7) {
        return 1;
    }
    if (string_result.status != infra::Status::kOk || string_result.value != "ok") {
        return 2;
    }
    if (!ok_result.IsOk() || ok_result.value != 9) {
        return 3;
    }
    if (fail_result.IsOk() || fail_result.status != infra::Status::kBusy) {
        return 4;
    }
    return 0;
}

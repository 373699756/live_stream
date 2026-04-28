#include "infra/service.h"

class FakeService : public infra::IService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake"; }
};

int main() {
    FakeService service;
    return service.Init() == infra::Status::kOk &&
                   service.Start() == infra::Status::kOk
               ? 0
               : 1;
}


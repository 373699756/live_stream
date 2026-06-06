#include "upgrade_service.h"

#include <cstring>

int main() {
    return std::strcmp(live_stream::UpgradeService::Name(), "upgrade_service");
}

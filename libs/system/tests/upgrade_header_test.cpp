#include "system/upgrade.h"

#include <cstring>

int main() {
    return std::strcmp(live_stream::Upgrade::Name(), "upgrade");
}

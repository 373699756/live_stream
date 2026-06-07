#include "rtsp.h"

#include <cstring>

int main() {
    return std::strcmp(live_stream::Rtsp::Name(), "rtsp");
}

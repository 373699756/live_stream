#include "rtsp_service.h"

#include <cstring>

int main() {
    return std::strcmp(live_stream::RtspService::Name(), "rtsp_service");
}

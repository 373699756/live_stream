#include "platform/linux/hisi_version.h"

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include("mpi_sys.h")
#include <cstring>
#include "mpi_sys.h"
#define LIVE_STREAM_HAS_HISI_SDK_VERSION 1
#else
#define LIVE_STREAM_HAS_HISI_SDK_VERSION 0
#endif

namespace live_stream {

std::string ReadHisiFirmwareVersion() {
#if LIVE_STREAM_HAS_HISI_SDK_VERSION
    MPP_VERSION_S version{};
    if (HI_MPI_SYS_GetVersion(&version) != HI_SUCCESS) {
        return std::string();
    }
    if (version.aVersion[0] == '\0') {
        return std::string();
    }
    return std::string(version.aVersion);
#else
    return std::string();
#endif
}

}  // namespace live_stream

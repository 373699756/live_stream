#ifndef LIVE_STREAM_APP_TOOLS_SYSUPGRADE_UPGRADE_FLASH_H_
#define LIVE_STREAM_APP_TOOLS_SYSUPGRADE_UPGRADE_FLASH_H_

#include "system/package.h"

#include <cstdint>
#include <functional>
#include <string>

namespace live_stream {
namespace upgrade_flash {

using MtdProgressCallback = std::function<void(uint32_t progress_percent,
                                               const std::string& stage)>;

bool IsPathOnTmpfs(const std::string& path);
bool ValidateMtdLayoutForManifest(const UpgradeManifest& manifest,
                                  std::string* msg);
bool IsMounted(const std::string& mount_point);
bool UnmountIfMounted(const std::string& mount_point, std::string* msg);
bool Remount(const UpgradePartition& partition, std::string* msg);
bool WriteMtdImage(const UpgradeCommand& command,
                   const std::string& image_path,
                   std::string* msg);
bool WriteMtdImage(const UpgradeCommand& command,
                   const std::string& image_path,
                   MtdProgressCallback progress_callback,
                   std::string* msg);

}  // namespace upgrade_flash
}  // namespace live_stream

#endif  // LIVE_STREAM_APP_TOOLS_SYSUPGRADE_UPGRADE_FLASH_H_

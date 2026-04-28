/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: config_service.h
 * Brief: 定义全局配置中心 service 的 public API。
 */

#ifndef LIVE_STREAM_CONFIG_SERVICE_H_
#define LIVE_STREAM_CONFIG_SERVICE_H_

#include "infra/status.h"
#include "infra/service.h"

#include "nlohmann/json.hpp"

#include <functional>
#include <memory>
#include <string>

namespace live_stream {

using ConfigJson = nlohmann::json;

struct ConfigServiceOptions {
    std::string config_path;
    std::string default_config_path;
    bool create_storage_if_missing = true;
};

using ConfigProc = std::function<infra::Status(const ConfigJson& value)>;

class IConfigService : public infra::IService {
 public:
    virtual infra::Status SetValue(const std::string& name,
                                  const ConfigJson& value) = 0;
    virtual infra::Status GetValue(const std::string& name,
                                  ConfigJson* value) = 0;
    virtual infra::Status GetDefault(const std::string& name,
                                    ConfigJson* value) = 0;
    virtual infra::Status RestoreDefaults() = 0;
    virtual infra::Status SaveFile() = 0;
    virtual infra::Status RegisterApply(const std::string& name,
                                       ConfigProc proc) = 0;
    virtual infra::Status RegisterVerify(const std::string& name,
                                        ConfigProc proc) = 0;
};

std::unique_ptr<IConfigService> CreateConfigService(
    const ConfigServiceOptions& options);

}  // namespace live_stream

#endif  // LIVE_STREAM_CONFIG_SERVICE_H_

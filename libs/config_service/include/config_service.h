/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: config_service.h
 * Brief: 定义全局配置中心 service 的 public API。
 */

#ifndef LIVE_STREAM_CONFIG_SERVICE_H_
#define LIVE_STREAM_CONFIG_SERVICE_H_

#include "config_json.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace live_stream {

struct ConfigServiceOptions {
    std::string config_path;
    std::string default_config_path;
    bool create_storage_if_missing = true;
};

struct ConfigError {
    std::string field;
    std::string reason;

    bool empty() const { return field.empty() && reason.empty(); }
};

struct ConfigResult {
    bool ok = true;
    ConfigError error;

    static ConfigResult Success() { return ConfigResult(); }

    static ConfigResult Failure(std::string field, std::string reason) {
        ConfigResult result;
        result.ok = false;
        result.error.field = std::move(field);
        result.error.reason = std::move(reason);
        return result;
    }
};

using ConfigHandler = std::function<ConfigResult(const ConfigJson &value)>;

struct ConfigAttachment {
    ConfigHandler validate;
    ConfigHandler apply;
};

class IConfigService {
public:
    virtual ~IConfigService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual bool SetValue(const std::string &name, const ConfigJson &value) = 0;
    virtual ConfigJson GetValue(const std::string &name) = 0;
    virtual bool SetDefault(const std::string &name) = 0;
    virtual ConfigJson GetDefault(const std::string &name) = 0;
    virtual bool RestoreDefaults() = 0;
    virtual bool AttachConfig(const std::string &name,
                              const ConfigAttachment &attachment) = 0;
    virtual bool DetachConfig(const std::string &name) = 0;
};

std::unique_ptr<IConfigService>
CreateConfigService(const ConfigServiceOptions &options);

}  // namespace live_stream

#endif  // LIVE_STREAM_CONFIG_SERVICE_H_

/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: config.h
 * Brief: 定义全局配置中心 public API。
 */

#ifndef LIVE_STREAM_CONFIG_CONFIG_H_
#define LIVE_STREAM_CONFIG_CONFIG_H_

#include "json.h"

#include <functional>
#include <memory>
#include <string>

namespace live_stream {

struct ConfigOptions {
    std::string config_path;
    std::string default_config_path;
    bool create_storage_if_missing = true;
};

enum class ConfigCode {
    kOk = 0,
    kInvalid,
    kStopped,
    kMissing,
    kExists,
    kVerify,
    kApply,
    kSave,
};

struct ConfigError {
    std::string scope;
    std::string field;
    std::string message;

    bool empty() const {
        return scope.empty() && field.empty() && message.empty();
    }
};

using ConfigVerify =
    std::function<ConfigCode(const Json &now, ConfigError *error)>;
using ConfigApply = std::function<ConfigCode(const Json &prev,
                                             const Json &now,
                                             ConfigError *error)>;

struct ConfigScope {
    ConfigVerify verify;
    ConfigApply apply;
};

class IConfig {
public:
    virtual ~IConfig() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual ConfigCode Set(const std::string &scope, const Json &now,
                           ConfigError *error = nullptr) = 0;
    virtual Json Get(const std::string &scope) = 0;
    virtual ConfigCode Reset(const std::string &scope,
                             ConfigError *error = nullptr) = 0;
    virtual Json Default(const std::string &scope) = 0;
    virtual ConfigCode ResetAll(ConfigError *error = nullptr) = 0;
    virtual bool AddScope(const std::string &scope,
                          const ConfigScope &config_scope) = 0;
    virtual bool RemoveScope(const std::string &scope) = 0;
};

std::unique_ptr<IConfig>
CreateConfig(const ConfigOptions &options);

}  // namespace live_stream

#endif  // LIVE_STREAM_CONFIG_CONFIG_H_

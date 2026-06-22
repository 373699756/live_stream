/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: config.h
 * Brief: 定义全局配置中心 public API。
 */

#ifndef LIVE_STREAM_CONFIG_CONFIG_H_
#define LIVE_STREAM_CONFIG_CONFIG_H_

#include "config_json.h"

#include <functional>
#include <memory>
#include <string>

namespace live_stream {

struct ConfigOptions {
    std::string config_path;
    std::string default_config_path;
    bool create_storage_if_missing = true;
};

enum class ConfigStatus {
    kOk = 0,
    kInvalid,
    kNotStarted,
    kNotFound,
    kExists,
    kVerifyFailed,
    kApplyFailed,
    kSaveFailed,
};

struct ConfigIssue {
    std::string field;
    std::string reason;

    bool empty() const { return field.empty() && reason.empty(); }
};

using ConfigVerify =
    std::function<ConfigStatus(const ConfigJson &now, ConfigIssue *issue)>;
using ConfigApply = std::function<ConfigStatus(const ConfigJson &prev,
                                               const ConfigJson &now,
                                               ConfigIssue *issue)>;

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
    virtual ConfigStatus Set(const std::string &scope, const ConfigJson &now,
                             ConfigIssue *issue = nullptr) = 0;
    virtual ConfigJson Get(const std::string &scope) = 0;
    virtual ConfigStatus Reset(const std::string &scope,
                               ConfigIssue *issue = nullptr) = 0;
    virtual ConfigJson Default(const std::string &scope) = 0;
    virtual ConfigStatus ResetAll(ConfigIssue *issue = nullptr) = 0;
    virtual bool AddScope(const std::string &scope,
                          const ConfigScope &config_scope) = 0;
    virtual bool RemoveScope(const std::string &scope) = 0;
};

std::unique_ptr<IConfig>
CreateConfig(const ConfigOptions &options);

}  // namespace live_stream

#endif  // LIVE_STREAM_CONFIG_CONFIG_H_

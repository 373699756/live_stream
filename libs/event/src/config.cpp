/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: config.cpp
 * Brief: Unified config implementation.
 */

#include "config.h"

#include "infra/fs.h"

#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

constexpr size_t kMaxConfigFileBytes = 512 * 1024;
constexpr int kMaxJsonDepth = 32;

void SetConfigError(ConfigError *error, const std::string &scope,
                    const std::string &field,
                    const std::string &msg) {
    if (error == nullptr) {
        return;
    }
    error->scope = scope;
    error->field = field;
    error->message = msg;
}

ConfigCode Reject(ConfigCode code, const std::string &scope,
                  const std::string &field,
                  const std::string &msg, ConfigError *error) {
    SetConfigError(error, scope, field, msg);
    return code;
}

void FillMissingConfigErrorScope(const std::string &scope,
                                 ConfigError *error) {
    if (error != nullptr && error->scope.empty()) {
        error->scope = scope;
    }
}

// Scope 只允许顶层无点号/无下标名称，避免配置中心绕过拥有模块解析嵌套字段。
bool IsScopeName(const std::string &scope) {
    return !scope.empty() && scope.find('.') == std::string::npos &&
           scope.find('[') == std::string::npos &&
           scope.find(']') == std::string::npos;
}

bool IsJsonWithinDepthLimit(const Json &value, int depth) {
    if (depth > kMaxJsonDepth) {
        return false;
    }
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!IsJsonWithinDepthLimit(it.value(), depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (value.is_array()) {
        for (const Json &item : value) {
            if (!IsJsonWithinDepthLimit(item, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

bool IsJsonWithinLimits(const Json &value) {
    return IsJsonWithinDepthLimit(value, 1);
}

void MergeMissingFields(Json *current, const Json &defaults) {
    if (current == nullptr || !current->is_object() || !defaults.is_object()) {
        return;
    }
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        auto current_it = current->find(it.key());
        if (current_it == current->end()) {
            (*current)[it.key()] = it.value();
            continue;
        }
        if (current_it->is_object() && it.value().is_object()) {
            MergeMissingFields(&(*current_it), it.value());
        }
    }
}

// JSON 文件读取
Json LoadJsonFile(const std::string &path) {
    std::string content = infra::File::ReadAll(path);
    if (content.empty() || content.size() > kMaxConfigFileBytes) {
        return Json();
    }
    Json parsed = Json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() ||
        !IsJsonWithinLimits(parsed)) {
        return Json();
    }
    return parsed;
}

// 原子写入: 先写 .tmp 再 rename（防止半写损坏）
bool AtomicWriteJson(const std::string &path, const Json &root) {
    if (!root.is_object() || path.empty()) {
        return false;
    }
    if (!infra::Path::MakeDirs(infra::Path::DirName(path))) {
        return false;
    }
    const std::string tmp = path + ".tmp";
    if (!infra::File::WriteAll(tmp, root.dump(4))) {
        return false;
    }
    return infra::File::Rename(tmp, path);
}

}  // namespace

class ConfigImpl : public IConfig {
public:
    explicit ConfigImpl(const ConfigOptions &options) : options_(options) {}

    ~ConfigImpl() override { ReleaseInternal(); }

    bool Prepare() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initialized_) {
                return true;
            }
        }

        Json defaults;
        Json current;
        if (!LoadInitialConfig(&defaults, &current)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return true;
        }
        defaults_ = std::move(defaults);
        current_ = std::move(current);
        changed_ = false;
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = true;
        return true;
    }

    void Stop() override {
        StopInternal();
    }

    bool IsStarted() const override {
        return IsStartedForRead();
    }

    void Release() {
        ReleaseInternal();
    }

private:
    void StopInternal() {
        std::lock_guard<std::mutex> write_lock(write_mutex_);
        SaveCurrentFile();
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    void ReleaseInternal() {
        std::lock_guard<std::mutex> write_lock(write_mutex_);
        SaveCurrentFile();
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = Json::object();
        defaults_ = Json::object();
        scopes_.clear();
        changed_ = false;
        initialized_ = false;
        started_ = false;
    }

public:
    ConfigCode Set(const std::string &scope, const Json &now,
                   ConfigError *error) override {
        if (!IsScopeName(scope)) {
            return Reject(ConfigCode::kInvalid, scope, "",
                          "invalid config scope", error);
        }
        if (!IsJsonWithinLimits(now)) {
            return Reject(ConfigCode::kInvalid, scope, "",
                          "config value exceeds limits", error);
        }

        std::lock_guard<std::mutex> write_lock(write_mutex_);
        return SetLocked(scope, now, error);
    }

    Json Get(const std::string &scope) override {
        if (!IsScopeName(scope) || !IsInitializedForRead())
            return Json();

        std::lock_guard<std::mutex> g(mutex_);
        auto it = current_.find(scope);
        if (it == current_.end())
            return Json();
        return it.value();
    }

    ConfigCode Reset(const std::string &scope,
                     ConfigError *error) override {
        if (!IsScopeName(scope)) {
            return Reject(ConfigCode::kInvalid, scope, "",
                          "invalid config scope", error);
        }

        Json default_value;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!initialized_) {
                return Reject(ConfigCode::kStopped, scope, "",
                              "config is not started", error);
            }
            const auto it = defaults_.find(scope);
            if (it == defaults_.end()) {
                return Reject(ConfigCode::kMissing, scope, "",
                              "config scope not found", error);
            }
            default_value = it.value();
        }

        std::lock_guard<std::mutex> write_lock(write_mutex_);
        return SetLocked(scope, default_value, error);
    }

    Json Default(const std::string &scope) override {
        if (!IsScopeName(scope) || !IsInitializedForRead())
            return Json();

        std::lock_guard<std::mutex> g(mutex_);
        auto it = defaults_.find(scope);
        if (it == defaults_.end())
            return Json();
        return it.value();
    }

    ConfigCode ResetAll(ConfigError *error) override {
        if (!IsInitializedForRead()) {
            return Reject(ConfigCode::kStopped, "", "",
                          "config is not started", error);
        }

        std::vector<std::pair<std::string, Json>> entries;
        {
            std::lock_guard<std::mutex> g(mutex_);
            for (auto it = defaults_.begin(); it != defaults_.end(); ++it) {
                entries.emplace_back(it.key(), it.value());
            }
        }
        for (const auto &entry : entries) {
            const ConfigCode code = Reset(entry.first, error);
            if (code != ConfigCode::kOk) {
                return code;
            }
        }
        return ConfigCode::kOk;
    }

    bool AddScope(const std::string &scope,
                  const ConfigScope &config_scope) override {
        if (!IsScopeName(scope) ||
            (!config_scope.verify && !config_scope.apply)) {
            return false;
        }
        std::lock_guard<std::mutex> g(mutex_);
        if (scopes_.find(scope) != scopes_.end()) {
            return false;
        }
        scopes_[scope] = config_scope;
        return true;
    }

    bool RemoveScope(const std::string &scope) override {
        if (!IsScopeName(scope)) {
            return false;
        }
        std::lock_guard<std::mutex> g(mutex_);
        scopes_.erase(scope);
        return true;
    }

private:
    ConfigCode SetLocked(const std::string &scope, const Json &now,
                         ConfigError *error) {
        ConfigScope config_scope;
        bool has_config_scope = false;
        Json prev;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!initialized_ || !started_) {
                return Reject(ConfigCode::kStopped, scope, "",
                              "config is not started", error);
            }
            const auto current_it = current_.find(scope);
            const auto default_it = defaults_.find(scope);
            if (current_it == current_.end() && default_it == defaults_.end()) {
                return Reject(ConfigCode::kMissing, scope, "",
                              "config scope not found", error);
            }
            if (current_it != current_.end() && current_it.value() == now) {
                return ConfigCode::kOk;
            }
            if (current_it != current_.end()) {
                prev = current_it.value();
            } else if (default_it != defaults_.end()) {
                prev = default_it.value();
            }
            auto scope_it = scopes_.find(scope);
            if (scope_it != scopes_.end()) {
                config_scope = scope_it->second;
                has_config_scope = true;
            }
        }

        if (has_config_scope && config_scope.verify) {
            const ConfigCode code = config_scope.verify(now, error);
            if (code != ConfigCode::kOk) {
                FillMissingConfigErrorScope(scope, error);
                return code;
            }
        }
        if (has_config_scope && config_scope.apply) {
            const ConfigCode code = config_scope.apply(prev, now, error);
            if (code != ConfigCode::kOk) {
                FillMissingConfigErrorScope(scope, error);
                return code;
            }
        }

        {
            std::lock_guard<std::mutex> g(mutex_);
            current_[scope] = now;
            changed_ = true;
        }
        if (SaveCurrentFile()) {
            return ConfigCode::kOk;
        }
        return Reject(ConfigCode::kSave, scope, "",
                      "save config file failed", error);
    }

    bool SaveCurrentFile() {
        if (!IsInitializedForRead())
            return false;

        Json snap;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!changed_)
                return true;
            snap = current_;
        }
        const bool saved = AtomicWriteJson(options_.config_path, snap);
        if (saved) {
            std::lock_guard<std::mutex> g(mutex_);
            changed_ = false;
        }
        return saved;
    }

    bool LoadInitialConfig(Json *defaults_out, Json *current_out) {
        if (defaults_out == nullptr || current_out == nullptr) {
            return false;
        }
        Json defaults = LoadJsonFile(options_.default_config_path);
        if (!defaults.is_object())
            return false;

        Json current;
        if (options_.config_path.empty() || !defaults.is_object()) {
            return false;
        } else if (infra::File::Exists(options_.config_path)) {
            current = LoadJsonFile(options_.config_path);
        } else if (!options_.create_storage_if_missing) {
            return false;
        } else {
            if (!AtomicWriteJson(options_.config_path, defaults))
                return false;
            current = defaults;
        }

        if (!current.is_object())
            return false;

        // Preserve user config and only fill fields added by defaults.
        MergeMissingFields(&current, defaults);
        if (!AtomicWriteJson(options_.config_path, current))
            return false;

        *current_out = std::move(current);
        *defaults_out = std::move(defaults);
        return true;
    }

    bool IsInitializedForRead() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_;
    }

    bool IsStartedForRead() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    Json current_ = Json::object();
    Json defaults_ = Json::object();

    std::unordered_map<std::string, ConfigScope> scopes_;

    ConfigOptions options_;
    std::mutex write_mutex_;
    mutable std::mutex mutex_;
    bool changed_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

std::unique_ptr<IConfig>
CreateConfig(const ConfigOptions &options) {
    return std::unique_ptr<IConfig>(new ConfigImpl(options));
}

}  // namespace live_stream

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
constexpr int kMaxConfigJsonDepth = 32;

void SetIssue(ConfigIssue *issue, const std::string &field,
              const std::string &reason) {
    if (issue == nullptr) {
        return;
    }
    issue->field = field;
    issue->reason = reason;
}

ConfigStatus Reject(ConfigStatus status, const std::string &field,
                    const std::string &reason, ConfigIssue *issue) {
    SetIssue(issue, field, reason);
    return status;
}

// Scope 只允许顶层无点号/无下标名称，避免配置中心绕过拥有模块解析嵌套字段。
bool IsScopeName(const std::string &scope) {
    return !scope.empty() && scope.find('.') == std::string::npos &&
           scope.find('[') == std::string::npos &&
           scope.find(']') == std::string::npos;
}

bool IsConfigJsonWithinDepthLimit(const ConfigJson &value, int depth) {
    if (depth > kMaxConfigJsonDepth) {
        return false;
    }
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!IsConfigJsonWithinDepthLimit(it.value(), depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (value.is_array()) {
        for (const ConfigJson &item : value) {
            if (!IsConfigJsonWithinDepthLimit(item, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

bool IsConfigJsonWithinLimits(const ConfigJson &value) {
    return IsConfigJsonWithinDepthLimit(value, 1);
}

void MergeMissingFields(ConfigJson *current, const ConfigJson &defaults) {
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
ConfigJson LoadJsonFile(const std::string &path) {
    std::string content = infra::File::ReadAll(path);
    if (content.empty() || content.size() > kMaxConfigFileBytes) {
        return ConfigJson();
    }
    ConfigJson parsed = ConfigJson::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() ||
        !IsConfigJsonWithinLimits(parsed)) {
        return ConfigJson();
    }
    return parsed;
}

// 原子写入: 先写 .tmp 再 rename（防止半写损坏）
bool AtomicWriteJson(const std::string &path, const ConfigJson &root) {
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

        ConfigJson defaults;
        ConfigJson current;
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
        current_ = ConfigJson::object();
        defaults_ = ConfigJson::object();
        scopes_.clear();
        changed_ = false;
        initialized_ = false;
        started_ = false;
    }

public:
    ConfigStatus Set(const std::string &scope, const ConfigJson &now,
                     ConfigIssue *issue) override {
        if (!IsScopeName(scope)) {
            return Reject(ConfigStatus::kInvalid, "", "invalid config scope",
                          issue);
        }
        if (!IsConfigJsonWithinLimits(now)) {
            return Reject(ConfigStatus::kInvalid, scope,
                          "config value exceeds limits", issue);
        }

        std::lock_guard<std::mutex> write_lock(write_mutex_);
        return SetLocked(scope, now, issue);
    }

    ConfigJson Get(const std::string &scope) override {
        if (!IsScopeName(scope) || !IsInitializedForRead())
            return ConfigJson();

        std::lock_guard<std::mutex> g(mutex_);
        auto it = current_.find(scope);
        if (it == current_.end())
            return ConfigJson();
        return it.value();
    }

    ConfigStatus Reset(const std::string &scope,
                       ConfigIssue *issue) override {
        if (!IsScopeName(scope)) {
            return Reject(ConfigStatus::kInvalid, "", "invalid config scope",
                          issue);
        }

        ConfigJson default_value;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!initialized_) {
                return Reject(ConfigStatus::kNotStarted, scope,
                              "config is not started", issue);
            }
            const auto it = defaults_.find(scope);
            if (it == defaults_.end()) {
                return Reject(ConfigStatus::kNotFound, scope,
                              "config scope not found", issue);
            }
            default_value = it.value();
        }

        std::lock_guard<std::mutex> write_lock(write_mutex_);
        return SetLocked(scope, default_value, issue);
    }

    ConfigJson Default(const std::string &scope) override {
        if (!IsScopeName(scope) || !IsInitializedForRead())
            return ConfigJson();

        std::lock_guard<std::mutex> g(mutex_);
        auto it = defaults_.find(scope);
        if (it == defaults_.end())
            return ConfigJson();
        return it.value();
    }

    ConfigStatus ResetAll(ConfigIssue *issue) override {
        if (!IsInitializedForRead()) {
            return Reject(ConfigStatus::kNotStarted, "",
                          "config is not started", issue);
        }

        std::vector<std::pair<std::string, ConfigJson>> entries;
        {
            std::lock_guard<std::mutex> g(mutex_);
            for (auto it = defaults_.begin(); it != defaults_.end(); ++it) {
                entries.emplace_back(it.key(), it.value());
            }
        }
        for (const auto &entry : entries) {
            const ConfigStatus status = Reset(entry.first, issue);
            if (status != ConfigStatus::kOk) {
                return status;
            }
        }
        return ConfigStatus::kOk;
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
    ConfigStatus SetLocked(const std::string &scope, const ConfigJson &now,
                           ConfigIssue *issue) {
        ConfigScope config_scope;
        bool has_config_scope = false;
        ConfigJson prev;
        bool previous_changed = false;
        bool had_prev = false;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!initialized_ || !started_) {
                return Reject(ConfigStatus::kNotStarted, scope,
                              "config is not started", issue);
            }
            const auto current_it = current_.find(scope);
            const auto default_it = defaults_.find(scope);
            if (current_it == current_.end() && default_it == defaults_.end()) {
                return Reject(ConfigStatus::kNotFound, scope,
                              "config scope not found", issue);
            }
            if (current_it != current_.end() && current_it.value() == now) {
                return ConfigStatus::kOk;
            }
            if (current_it != current_.end()) {
                prev = current_it.value();
                had_prev = true;
            } else if (default_it != defaults_.end()) {
                prev = default_it.value();
            }
            previous_changed = changed_;
            auto scope_it = scopes_.find(scope);
            if (scope_it != scopes_.end()) {
                config_scope = scope_it->second;
                has_config_scope = true;
            }
        }

        if (has_config_scope && config_scope.verify) {
            const ConfigStatus status = config_scope.verify(now, issue);
            if (status != ConfigStatus::kOk) {
                return status;
            }
        }
        if (has_config_scope && config_scope.apply) {
            const ConfigStatus status = config_scope.apply(prev, now, issue);
            if (status != ConfigStatus::kOk) {
                return status;
            }
        }

        {
            std::lock_guard<std::mutex> g(mutex_);
            current_[scope] = now;
            changed_ = true;
        }
        if (SaveCurrentFile()) {
            return ConfigStatus::kOk;
        }

        ConfigIssue rollback_issue;
        if (has_config_scope && config_scope.apply) {
            (void)config_scope.apply(now, prev, &rollback_issue);
        }

        std::lock_guard<std::mutex> g(mutex_);
        if (had_prev) {
            current_[scope] = prev;
        } else {
            current_.erase(scope);
        }
        changed_ = previous_changed;
        return Reject(ConfigStatus::kSaveFailed, scope,
                      "save config file failed", issue);
    }

    bool SaveCurrentFile() {
        if (!IsInitializedForRead())
            return false;

        ConfigJson snap;
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

    bool LoadInitialConfig(ConfigJson *defaults_out, ConfigJson *current_out) {
        if (defaults_out == nullptr || current_out == nullptr) {
            return false;
        }
        ConfigJson defaults = LoadJsonFile(options_.default_config_path);
        if (!defaults.is_object())
            return false;

        ConfigJson current;
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

    ConfigJson current_ = ConfigJson::object();
    ConfigJson defaults_ = ConfigJson::object();

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

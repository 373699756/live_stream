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

// 名称校验: 只允许顶层无点号/无下标的纯名称
bool IsTopLevelName(const std::string &name) {
    return !name.empty() && name.find('.') == std::string::npos &&
           name.find('[') == std::string::npos &&
           name.find(']') == std::string::npos;
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

ConfigResult RunHandler(const ConfigHandler &handler, const ConfigJson &value) {
    if (!handler) {
        return ConfigResult::Success();
    }
    return handler(value);
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

class ConfigServiceImpl : public IConfig {
public:
    explicit ConfigServiceImpl(const ConfigOptions &opts) : opts_(opts) {}

    ~ConfigServiceImpl() override { ReleaseInternal(); }

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
        attachments_.clear();
        changed_ = false;
        initialized_ = false;
        started_ = false;
    }

public:
    bool SetValue(const std::string &name, const ConfigJson &value) override {
        if (!IsTopLevelName(name)) {
            return false;
        }
        if (!IsConfigJsonWithinLimits(value)) {
            return false;
        }

        std::lock_guard<std::mutex> write_lock(write_mutex_);
        return SetValueLocked(name, value);
    }

    ConfigJson GetValue(const std::string &name) override {
        if (!IsTopLevelName(name) || !IsInitializedForRead())
            return ConfigJson();

        std::lock_guard<std::mutex> g(mutex_);
        auto it = current_.find(name);
        if (it == current_.end())
            return ConfigJson();
        return it.value();
    }

    bool SetDefault(const std::string &name) override {
        if (!IsTopLevelName(name)) {
            return false;
        }

        ConfigJson default_value;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!initialized_) {
                return false;
            }
            const auto it = defaults_.find(name);
            if (it == defaults_.end()) {
                return false;
            }
            default_value = it.value();
        }

        std::lock_guard<std::mutex> write_lock(write_mutex_);
        return SetValueLocked(name, default_value);
    }

    ConfigJson GetDefault(const std::string &name) override {
        if (!IsTopLevelName(name) || !IsInitializedForRead())
            return ConfigJson();

        std::lock_guard<std::mutex> g(mutex_);
        auto it = defaults_.find(name);
        if (it == defaults_.end())
            return ConfigJson();
        return it.value();
    }

    bool RestoreDefaults() override {
        if (!IsInitializedForRead())
            return false;

        std::vector<std::pair<std::string, ConfigJson>> entries;
        {
            std::lock_guard<std::mutex> g(mutex_);
            for (auto it = defaults_.begin(); it != defaults_.end(); ++it) {
                entries.emplace_back(it.key(), it.value());
            }
        }
        for (const auto &entry : entries) {
            if (!SetDefault(entry.first))
                return false;
        }
        return true;
    }

    bool AttachConfig(const std::string &name,
                      const ConfigAttachment &attachment) override {
        if (!IsTopLevelName(name) || (!attachment.validate && !attachment.apply)) {
            return false;
        }
        std::lock_guard<std::mutex> g(mutex_);
        if (attachments_.find(name) != attachments_.end()) {
            return false;
        }
        attachments_[name] = attachment;
        return true;
    }

    bool DetachConfig(const std::string &name) override {
        if (!IsTopLevelName(name)) {
            return false;
        }
        std::lock_guard<std::mutex> g(mutex_);
        attachments_.erase(name);
        return true;
    }

private:
    bool SetValueLocked(const std::string &name, const ConfigJson &value) {
        ConfigAttachment attachment;
        bool has_attachment = false;
        ConfigJson previous_value;
        bool previous_changed = false;
        bool had_previous_value = false;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!initialized_ || !started_) {
                return false;
            }
            const auto it = current_.find(name);
            if (it != current_.end() && it.value() == value) {
                return true;
            }
            if (it != current_.end()) {
                previous_value = it.value();
                had_previous_value = true;
            }
            previous_changed = changed_;
            auto attachment_it = attachments_.find(name);
            if (attachment_it != attachments_.end()) {
                attachment = attachment_it->second;
                has_attachment = true;
            }
        }

        if (has_attachment) {
            const ConfigResult validate_result =
                RunHandler(attachment.validate, value);
            if (!validate_result.ok) {
                return false;
            }
            const ConfigResult apply_result = RunHandler(attachment.apply, value);
            if (!apply_result.ok) {
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> g(mutex_);
            current_[name] = value;
            changed_ = true;
        }
        if (SaveCurrentFile()) {
            return true;
        }

        std::lock_guard<std::mutex> g(mutex_);
        if (had_previous_value) {
            current_[name] = previous_value;
        } else {
            current_.erase(name);
        }
        changed_ = previous_changed;
        return false;
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
        const bool saved = AtomicWriteJson(opts_.config_path, snap);
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
        ConfigJson defaults = LoadJsonFile(opts_.default_config_path);
        if (!defaults.is_object())
            return false;

        ConfigJson current;
        if (opts_.config_path.empty() || !defaults.is_object()) {
            return false;
        } else if (infra::File::Exists(opts_.config_path)) {
            current = LoadJsonFile(opts_.config_path);
        } else if (!opts_.create_storage_if_missing) {
            return false;
        } else {
            if (!AtomicWriteJson(opts_.config_path, defaults))
                return false;
            current = defaults;
        }

        if (!current.is_object())
            return false;

        // Preserve user config and only fill fields added by defaults.
        MergeMissingFields(&current, defaults);
        if (!AtomicWriteJson(opts_.config_path, current))
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

    std::unordered_map<std::string, ConfigAttachment> attachments_;

    ConfigOptions opts_;
    std::mutex write_mutex_;
    mutable std::mutex mutex_;
    bool changed_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

std::unique_ptr<IConfig>
CreateConfig(const ConfigOptions &options) {
    return std::unique_ptr<IConfig>(new ConfigServiceImpl(options));
}

}  // namespace live_stream

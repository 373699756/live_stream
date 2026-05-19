/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: config_service.cpp
 * Brief: Unified config service implementation.
 *        Merges: ConfigStore + ConfigPersistence + ConfigSignalRegistry
 *        into a single class. Removes unused config_name parser.
 */

#include "config_service.h"

#include "infra/fs.h"

#include <cstddef>
#include <cstdint>
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
    ConfigResult result = handler(value);
    if (result.ok) {
        result.error = ConfigError();
        return result;
    }
    if (result.error.reason.empty()) {
        result.error.reason = "config rejected";
    }
    return result;
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

ConfigError RollbackError(const ConfigResult &apply_result) {
    if (!apply_result.error.reason.empty()) {
        return apply_result.error;
    }
    return ConfigError{"", "apply failed"};
}

}  // namespace

class ConfigServiceImpl : public IConfigService {
public:
    explicit ConfigServiceImpl(const ConfigServiceOptions &opts) : opts_(opts) {}

    ~ConfigServiceImpl() override { Release(); }

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
        change_generation_ = 0;
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
        SaveFile();
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    void Release() {
        SaveFile();
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = ConfigJson::object();
        defaults_ = ConfigJson::object();
        attachments_.clear();
        observers_.clear();
        last_errors_.clear();
        changed_ = false;
        change_generation_ = 0;
        initialized_ = false;
        started_ = false;
        next_observer_id_ = 1;
    }

    bool SetValue(const std::string &name, const ConfigJson &value) override {
        if (!IsTopLevelName(name)) {
            return false;
        }
        if (!IsConfigJsonWithinLimits(value)) {
            SetLastConfigError(name, ConfigError{"", "config json too deep"});
            return false;
        }

        std::vector<ConfigObserver> observers;
        {
            std::lock_guard<std::mutex> write_lock(write_mutex_);
            if (!IsStartedForRead()) {
                SetLastConfigError(name,
                                   ConfigError{"",
                                               "config service not started"});
                return false;
            }
            if (!SetValueTransaction(name, value, &observers)) {
                return false;
            }
        }
        NotifyObservers(observers, value);
        return true;
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
            if (!SetValue(entry.first, entry.second))
                return false;
        }
        return true;
    }

    bool SaveFile() override {
        std::lock_guard<std::mutex> write_lock(write_mutex_);
        return SaveFileSnapshot();
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

    ConfigObserverId ObserveConfig(const std::string &name,
                                   ConfigObserver observer) override {
        if (!IsTopLevelName(name) || !observer) {
            return 0;
        }
        std::lock_guard<std::mutex> g(mutex_);
        const ConfigObserverId observer_id = next_observer_id_++;
        observers_[name][observer_id] = std::move(observer);
        return observer_id;
    }

    bool UnobserveConfig(const std::string &name,
                         ConfigObserverId observer_id) override {
        if (!IsTopLevelName(name) || observer_id == 0) {
            return false;
        }
        std::lock_guard<std::mutex> g(mutex_);
        auto observers_it = observers_.find(name);
        if (observers_it == observers_.end()) {
            return false;
        }
        if (observers_it->second.erase(observer_id) == 0) {
            return false;
        }
        if (observers_it->second.empty()) {
            observers_.erase(observers_it);
        }
        return true;
    }

    ConfigError GetLastConfigError(const std::string &name) override {
        if (!IsTopLevelName(name)) {
            return ConfigError();
        }
        std::lock_guard<std::mutex> g(mutex_);
        const auto it = last_errors_.find(name);
        if (it == last_errors_.end()) {
            return ConfigError();
        }
        return it->second;
    }

private:
    struct PendingConfigChange {
        ConfigAttachment attachment;
        bool changed = false;
        bool has_attachment = false;
        ConfigJson previous_value;
        bool previous_changed = false;
        uint64_t generation = 0;
    };

    bool SetValueTransaction(const std::string &name, const ConfigJson &value,
                             std::vector<ConfigObserver> *observers) {
        PendingConfigChange change;
        if (!PrepareConfigChange(name, value, &change)) {
            return false;
        }
        if (!change.changed) {
            return true;
        }
        if (change.has_attachment) {
            ConfigResult validate_result =
                RunHandler(change.attachment.validate, value);
            if (!validate_result.ok) {
                SetLastConfigError(name, validate_result.error);
                return false;
            }
        }
        CommitConfigValue(name, value, &change, observers);
        if (!SaveFileSnapshot()) {
            RestoreUnsavedValue(name, change);
            return false;
        }
        if (change.has_attachment) {
            ConfigResult apply_result =
                RunHandler(change.attachment.apply, value);
            if (!apply_result.ok) {
                RollbackAppliedValue(name, value, change,
                                     RollbackError(apply_result));
                return false;
            }
        }
        ClearLastConfigError(name);
        return true;
    }

    bool PrepareConfigChange(const std::string &name, const ConfigJson &value,
                             PendingConfigChange *change) {
        if (change == nullptr) {
            return false;
        }
        ConfigAttachment attachment;
        bool has_attachment = false;
        ConfigJson previous_value;
        bool previous_changed = false;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (defaults_.find(name) == defaults_.end()) {
                last_errors_[name] = ConfigError{"", "config not found"};
                return false;
            }
            const auto it = current_.find(name);
            if (it != current_.end() && it.value() == value) {
                ClearLastConfigErrorLocked(name);
                return true;
            }
            if (it == current_.end()) {
                last_errors_[name] = ConfigError{"", "config not found"};
                return false;
            }
            previous_value = it.value();
            previous_changed = changed_;
            auto attachment_it = attachments_.find(name);
            if (attachment_it != attachments_.end()) {
                attachment = attachment_it->second;
                has_attachment = true;
            }
        }
        change->changed = true;
        change->attachment = std::move(attachment);
        change->has_attachment = has_attachment;
        change->previous_value = std::move(previous_value);
        change->previous_changed = previous_changed;
        return true;
    }

    void CommitConfigValue(const std::string &name, const ConfigJson &value,
                           PendingConfigChange *change,
                           std::vector<ConfigObserver> *observers) {
        if (change == nullptr) {
            return;
        }
        std::vector<ConfigObserver> next_observers;
        {
            std::lock_guard<std::mutex> g(mutex_);
            current_[name] = value;
            changed_ = true;
            ++change_generation_;
            change->generation = change_generation_;
            ClearLastConfigErrorLocked(name);
            next_observers = CollectObserversLocked(name);
        }
        if (observers != nullptr) {
            *observers = std::move(next_observers);
        }
    }

    void RestoreUnsavedValue(const std::string &name,
                             const PendingConfigChange &change) {
        std::lock_guard<std::mutex> g(mutex_);
        current_[name] = change.previous_value;
        ++change_generation_;
        changed_ = change.previous_changed;
        last_errors_[name] = ConfigError{"", "save config file failed"};
    }

    void RollbackAppliedValue(const std::string &name,
                              const ConfigJson &attempted_value,
                              const PendingConfigChange &change,
                              const ConfigError &error) {
        bool should_save_rollback = false;
        {
            std::lock_guard<std::mutex> g(mutex_);
            const auto it = current_.find(name);
            if (change_generation_ == change.generation &&
                it != current_.end() && it.value() == attempted_value) {
                current_[name] = change.previous_value;
                changed_ = true;
                ++change_generation_;
                should_save_rollback = true;
            }
        }
        const bool rollback_saved =
            should_save_rollback && SaveFileSnapshot();
        if (!rollback_saved) {
            SetLastConfigError(
                name, ConfigError{"", "apply failed and rollback save failed"});
            return;
        }
        SetLastConfigError(name, error);
    }

    bool SaveFileSnapshot() {
        if (!IsInitializedForRead())
            return false;

        ConfigJson snap;
        uint64_t snap_generation = 0;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (!changed_)
                return true;
            snap_generation = change_generation_;
            snap = current_;
        }
        const bool saved = AtomicWriteJson(opts_.config_path, snap);
        if (saved) {
            std::lock_guard<std::mutex> g(mutex_);
            if (change_generation_ == snap_generation) {
                changed_ = false;
            }
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

    std::vector<ConfigObserver>
    CollectObserversLocked(const std::string &name) const {
        std::vector<ConfigObserver> observers;
        const auto it = observers_.find(name);
        if (it == observers_.end()) {
            return observers;
        }
        observers.reserve(it->second.size());
        for (const auto &entry : it->second) {
            observers.push_back(entry.second);
        }
        return observers;
    }

    void NotifyObservers(const std::vector<ConfigObserver> &observers,
                         const ConfigJson &value) const {
        for (const ConfigObserver &observer : observers) {
            if (observer) {
                observer(value);
            }
        }
    }

    void ClearLastConfigErrorLocked(const std::string &name) {
        last_errors_.erase(name);
    }

    void ClearLastConfigError(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex_);
        ClearLastConfigErrorLocked(name);
    }

    void SetLastConfigError(const std::string &name, ConfigError error) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_errors_[name] = std::move(error);
    }

    ConfigJson current_ = ConfigJson::object();
    ConfigJson defaults_ = ConfigJson::object();

    std::unordered_map<std::string, ConfigAttachment> attachments_;
    std::unordered_map<std::string,
                       std::unordered_map<ConfigObserverId, ConfigObserver>>
        observers_;
    std::unordered_map<std::string, ConfigError> last_errors_;

    ConfigServiceOptions opts_;
    std::mutex write_mutex_;
    mutable std::mutex mutex_;
    bool changed_ = false;
    uint64_t change_generation_ = 0;
    bool initialized_ = false;
    bool started_ = false;
    ConfigObserverId next_observer_id_ = 1;
};

std::unique_ptr<IConfigService>
CreateConfigService(const ConfigServiceOptions &options) {
    return std::unique_ptr<IConfigService>(new ConfigServiceImpl(options));
}

}  // namespace live_stream

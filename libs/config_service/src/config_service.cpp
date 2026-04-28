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
#include "infra/service.h"
#include "infra/sync.h"

#include <unordered_map>
#include <vector>

namespace live_stream {

namespace {

// 名称校验: 只允许顶层无点号/无下标的纯名称
bool IsTopLevelName(const std::string& name) {
    return !name.empty() &&
           name.find('.') == std::string::npos &&
           name.find('[') == std::string::npos &&
           name.find(']') == std::string::npos;
}

// 顺序执行回调列表，首个失败即停止
infra::Status RunCallbacks(const std::vector<ConfigProc>& cbs,
                           const ConfigJson& value) {
    for (const auto& cb : cbs) {
        const infra::Status st = cb(value);
        if (st != infra::Status::kOk) {
            return st;
        }
    }
    return infra::Status::kOk;
}

void MergeMissingFields(ConfigJson* current, const ConfigJson& defaults) {
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
infra::Result<ConfigJson> LoadJsonFile(const std::string& path) {
    auto content = infra::File::ReadAll(path);
    if (!content.IsOk()) {
        return infra::Result<ConfigJson>::Fail(content.status);
    }
    ConfigJson parsed = ConfigJson::parse(content.value, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return infra::Result<ConfigJson>::Fail(infra::Status::kInvalidParam);
    }
    return infra::Result<ConfigJson>::Ok(std::move(parsed));
}

// 原子写入: 先写 .tmp 再 rename（防止半写损坏）
infra::Status AtomicWriteJson(const std::string& path,
                               const ConfigJson& root) {
    if (!root.is_object() || path.empty()) {
        return infra::Status::kInvalidParam;
    }
    infra::Status st = infra::Path::MakeDirs(infra::Path::DirName(path));
    if (st != infra::Status::kOk) {
        return st;
    }
    const std::string tmp = path + ".tmp";
    st = infra::File::WriteAll(tmp, root.dump(4));
    if (st != infra::Status::kOk) {
        return st;
    }
    return infra::File::Rename(tmp, path);
}

}  // namespace


class ConfigServiceImpl : public IConfigService, private infra::ServiceBase {
 public:
    explicit ConfigServiceImpl(const ConfigServiceOptions& opts)
        : opts_(opts) {}

    ~ConfigServiceImpl() override { Stop(); Deinit(); }

    // -- IService --
    infra::Status Init() override   { return infra::ServiceBase::Init(); }
    infra::Status Start() override  { return infra::ServiceBase::Start(); }
    void Stop() override            { infra::ServiceBase::Stop(); }
    void Deinit() override          { infra::ServiceBase::Deinit(); }

    const char* Name() const override { return "config_service"; }

    // -- IConfigService --
    infra::Status SetValue(const std::string& name,
                          const ConfigJson& value) override {
        if (!IsTopLevelName(name))     return infra::Status::kInvalidParam;
        if (!IsStartedForRead())       return infra::Status::kBusy;

        // Phase 1: 锁内构建候选 + 提取回调
        ConfigJson old_value;
        ConfigJson candidate;
        std::vector<ConfigProc> verify_cbs, apply_cbs;
        {
            infra::MutexGuard g(&mutex_);
            if (defaults_.find(name) == defaults_.end()) {
                return infra::Status::kNotFound;
            }
            auto it = current_.find(name);
            old_value = (it == current_.end())
                        ? ConfigJson(nullptr) : it.value();
            if (old_value == value) return infra::Status::kOk;  // 无变化
            candidate = value;
            verify_cbs = CopyCbs(verify_cbs_, name);
            apply_cbs  = CopyCbs(apply_cbs_, name);
        }

        // Phase 2: 锁外执行 verify -> apply
        infra::Status st = RunCallbacks(verify_cbs, candidate);
        if (st != infra::Status::kOk) return st;
        st = RunCallbacks(apply_cbs, candidate);
        if (st != infra::Status::kOk) return st;

        // Phase 3: 锁内提交 + 持久化
        {
            infra::MutexGuard g(&mutex_);
            current_[name] = std::move(candidate);
            changed_ = true;
        }
        return SaveToFile();
    }

    infra::Status GetValue(const std::string& name,
                          ConfigJson* value) override {
        if (!value || !IsTopLevelName(name))
            return infra::Status::kInvalidParam;
        if (!IsInitializedForRead())
            return infra::Status::kInternalError;

        infra::MutexGuard g(&mutex_);
        auto it = current_.find(name);
        if (it == current_.end()) return infra::Status::kNotFound;
        *value = it.value();
        return infra::Status::kOk;
    }

    infra::Status GetDefault(const std::string& name,
                            ConfigJson* value) override {
        if (!value || !IsTopLevelName(name))
            return infra::Status::kInvalidParam;
        if (!IsInitializedForRead())
            return infra::Status::kInternalError;

        infra::MutexGuard g(&mutex_);
        auto it = defaults_.find(name);
        if (it == defaults_.end()) return infra::Status::kNotFound;
        *value = it.value();
        return infra::Status::kOk;
    }

    infra::Status RestoreDefaults() override {
        if (!IsInitializedForRead()) return infra::Status::kInternalError;

        std::vector<std::pair<std::string, ConfigJson>> entries;
        {
            infra::MutexGuard g(&mutex_);
            for (auto it = defaults_.begin(); it != defaults_.end(); ++it) {
                entries.emplace_back(it.key(), it.value());
            }
        }
        for (const auto& entry : entries) {
            const infra::Status st = SetValue(entry.first, entry.second);
            if (st != infra::Status::kOk)
                return st;
        }
        return infra::Status::kOk;
    }

    infra::Status SaveFile() override {
        if (!IsInitializedForRead()) return infra::Status::kInternalError;

        ConfigJson snap;
        {
            infra::MutexGuard g(&mutex_);
            if (!changed_) return infra::Status::kOk;
            snap = current_;
        }
        auto st = AtomicWriteJson(opts_.config_path, snap);
        if (st == infra::Status::kOk) {
            infra::MutexGuard g(&mutex_);
            changed_ = false;
        }
        return st;
    }

    infra::Status RegisterApply(const std::string& name,
                               ConfigProc proc) override {
        if (!proc || !IsTopLevelName(name)) return infra::Status::kInvalidParam;
        infra::MutexGuard g(&mutex_);
        apply_cbs_[name].push_back(std::move(proc));
        return infra::Status::kOk;
    }

    infra::Status RegisterVerify(const std::string& name,
                                ConfigProc proc) override {
        if (!proc || !IsTopLevelName(name)) return infra::Status::kInvalidParam;
        infra::MutexGuard g(&mutex_);
        verify_cbs_[name].push_back(std::move(proc));
        return infra::Status::kOk;
    }

 private:
    // -- 生命周期 --
    infra::Status OnInit() override {
        auto defaults = LoadJsonFile(opts_.default_config_path);
        if (!defaults.IsOk()) return defaults.status;

        infra::Result<ConfigJson> current =
            infra::Result<ConfigJson>::Fail(infra::Status::kInvalidParam);
        if (opts_.config_path.empty() || !defaults.value.is_object()) {
            current =
                infra::Result<ConfigJson>::Fail(infra::Status::kInvalidParam);
        } else if (infra::File::Exists(opts_.config_path)) {
            current = LoadJsonFile(opts_.config_path);
        } else if (!opts_.create_storage_if_missing) {
            current = infra::Result<ConfigJson>::Fail(infra::Status::kNotFound);
        } else {
            const infra::Status st =
                AtomicWriteJson(opts_.config_path, defaults.value);
            if (st != infra::Status::kOk) {
                current = infra::Result<ConfigJson>::Fail(st);
            } else {
                current = infra::Result<ConfigJson>::Ok(defaults.value);
            }
        }

        if (!current.IsOk()) return current.status;

        // Preserve user config and only fill fields added by defaults.
        MergeMissingFields(&current.value, defaults.value);
        const infra::Status st =
            AtomicWriteJson(opts_.config_path, current.value);
        if (st != infra::Status::kOk)
            return st;

        current_  = std::move(current.value);
        defaults_ = std::move(defaults.value);
        changed_  = false;
        return infra::Status::kOk;
    }

    void OnStop() override { SaveFile(); }

    // -- 内部辅助 --
    static std::vector<ConfigProc> CopyCbs(
        const std::unordered_map<std::string, std::vector<ConfigProc>>& map,
        const std::string& name) {
        auto it = map.find(name);
        return (it != map.end()) ? it->second : std::vector<ConfigProc>{};
    }

    infra::Status SaveToFile() { return SaveFile(); }

    // -- 数据 --
    ConfigJson      current_  = ConfigJson::object();
    ConfigJson      defaults_ = ConfigJson::object();

    std::unordered_map<std::string, std::vector<ConfigProc>> apply_cbs_;
    std::unordered_map<std::string, std::vector<ConfigProc>> verify_cbs_;

    ConfigServiceOptions opts_;
    infra::Mutex       mutex_;
    bool               changed_ = false;
};

std::unique_ptr<IConfigService> CreateConfigService(
    const ConfigServiceOptions& options) {
    return std::make_unique<ConfigServiceImpl>(options);
}

}  // namespace live_stream

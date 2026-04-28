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
        if (const auto st = cb(value); st != infra::Status::kOk) {
            return st;
        }
    }
    return infra::Status::kOk;
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
    if (auto st = infra::Path::MakeDirs(infra::Path::DirName(path));
        st != infra::Status::kOk) {
        return st;
    }
    const std::string tmp = path + ".tmp";
    if (auto st = infra::File::WriteAll(tmp, root.dump(4));
        st != infra::Status::kOk) {
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
        if (auto st = RunCallbacks(verify_cbs, candidate);
            st != infra::Status::kOk) return st;
        if (auto st = RunCallbacks(apply_cbs, candidate);
            st != infra::Status::kOk) return st;

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
        for (const auto& [k, v] : entries) {
            if (auto st = SetValue(k, v); st != infra::Status::kOk)
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

        auto current = [&] -> infra::Result<ConfigJson> {
            if (opts_.config_path.empty() || !defaults.value.is_object())
                return infra::Result<ConfigJson>::Fail(infra::Status::kInvalidParam);
            if (infra::File::Exists(opts_.config_path))
                return LoadJsonFile(opts_.config_path);
            if (!opts_.create_storage_if_missing)
                return infra::Result<ConfigJson>::Fail(infra::Status::kNotFound);
            // 首次运行：用默认配置创建文件
            if (auto st = AtomicWriteJson(opts_.config_path, defaults.value);
                st != infra::Status::kOk)
                return infra::Result<ConfigJson>::Fail(st);
            return infra::Result<ConfigJson>::Ok(defaults.value);
        }();

        if (!current.IsOk()) return current.status;

        // 用 json原生 update 补齐新增的默认字段（替代手写 MergeMissingFields）
        current.value.update(defaults.value);
        if (auto st = AtomicWriteJson(opts_.config_path, current.value);
            st != infra::Status::kOk)
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

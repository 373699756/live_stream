#include "auth_internal.h"

#include "infra/fs.h"

#include "nlohmann/json.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {
namespace {

using Json = nlohmann::json;

bool ReadStringField(const Json& object,
                     const std::string& key,
                     std::string* value) {
    if (value == nullptr || !object.is_object() || !object.contains(key) ||
        !object.at(key).is_string()) {
        return false;
    }
    *value = object.at(key).get<std::string>();
    return true;
}

bool ReadBoolFieldOrDefault(const Json& object,
                            const std::string& key,
                            bool default_value,
                            bool* value) {
    if (value == nullptr) {
        return false;
    }
    if (!object.is_object()) {
        return false;
    }
    if (!object.contains(key)) {
        *value = default_value;
        return true;
    }
    if (!object.at(key).is_boolean()) {
        return false;
    }
    *value = object.at(key).get<bool>();
    return true;
}

class MemoryAuthUserStore : public IAuthUserStore {
 public:
    explicit MemoryAuthUserStore(const std::vector<AuthUserRecord>& users) {
        for (const AuthUserRecord& user : users) {
            users_[user.user_name] = user;
        }
    }

    infra::Result<AuthUserRecord> FindUser(
        const std::string& user_name) override {
        const auto iter = users_.find(user_name);
        if (iter == users_.end()) {
            return infra::Result<AuthUserRecord>::Fail(infra::Status::kNotFound);
        }
        return infra::Result<AuthUserRecord>::Ok(iter->second);
    }

    infra::Status Reload() override {
        return infra::Status::kOk;
    }

 private:
    std::map<std::string, AuthUserRecord> users_;
};

class JsonAuthUserStore : public IAuthUserStore {
 public:
    explicit JsonAuthUserStore(const std::string& config_path)
        : config_path_(config_path) {}

    infra::Result<AuthUserRecord> FindUser(
        const std::string& user_name) override {
        const infra::Status load_error = EnsureLoaded();
        if (load_error != infra::Status::kOk) {
            return infra::Result<AuthUserRecord>::Fail(load_error);
        }
        const auto iter = users_.find(user_name);
        if (iter == users_.end()) {
            return infra::Result<AuthUserRecord>::Fail(infra::Status::kNotFound);
        }
        return infra::Result<AuthUserRecord>::Ok(iter->second);
    }

    infra::Status Reload() override {
        loaded_ = false;
        load_error_ = infra::Status::kOk;
        users_.clear();
        return EnsureLoaded();
    }

 private:
    infra::Status EnsureLoaded() {
        if (loaded_) {
            return load_error_;
        }
        loaded_ = true;
        if (config_path_.empty()) {
            load_error_ = infra::Status::kInvalidParam;
            return load_error_;
        }
        infra::Result<std::string> content = infra::File::ReadAll(config_path_);
        if (!content.IsOk()) {
            load_error_ = content.status;
            return load_error_;
        }
        if (content.value.size() > auth_internal::kMaxAuthConfigSize) {
            load_error_ = infra::Status::kInvalidParam;
            return load_error_;
        }
        Json document = Json::parse(content.value, nullptr, false);
        if (document.is_discarded()) {
            load_error_ = infra::Status::kInvalidParam;
            return load_error_;
        }
        load_error_ = Parse(document);
        return load_error_;
    }

    infra::Status Parse(const Json& document) {
        if (!document.is_object() || !document.contains("users") ||
            !document.at("users").is_array() ||
            document.at("users").empty()) {
            return infra::Status::kInvalidParam;
        }

        std::map<std::string, AuthUserRecord> parsed_users;
        for (const Json& user_json : document.at("users")) {
            if (!user_json.is_object()) {
                return infra::Status::kInvalidParam;
            }
            if (user_json.contains("password")) {
                return infra::Status::kInvalidParam;
            }
            AuthUserRecord user;
            std::string role;
            if (!ReadStringField(user_json, "user_name", &user.user_name) ||
                !ReadStringField(user_json, "role", &role) ||
                !ReadStringField(user_json, "password_credential",
                                 &user.password_credential) ||
                !ReadBoolFieldOrDefault(user_json, "enabled", true,
                                        &user.enabled) ||
                !auth_internal::ParseRole(role, &user.role) ||
                auth_internal::IsEmptyOrTooLong(
                    user.user_name, auth_internal::kMaxUserNameLength) ||
                user.password_credential.empty()) {
                return infra::Status::kInvalidParam;
            }
            if (parsed_users.find(user.user_name) != parsed_users.end()) {
                return infra::Status::kAlreadyExists;
            }
            parsed_users[user.user_name] = user;
        }
        users_.swap(parsed_users);
        return infra::Status::kOk;
    }

    std::string config_path_;
    std::map<std::string, AuthUserRecord> users_;
    bool loaded_ = false;
    infra::Status load_error_ = infra::Status::kOk;
};

}  // namespace

std::unique_ptr<IAuthUserStore> CreateMemoryAuthUserStore(
    const std::vector<AuthUserRecord>& users) {
    return std::unique_ptr<IAuthUserStore>(new MemoryAuthUserStore(users));
}

std::unique_ptr<IAuthUserStore> CreateJsonAuthUserStore(
    const std::string& config_path) {
    return std::unique_ptr<IAuthUserStore>(new JsonAuthUserStore(config_path));
}

}  // namespace live_stream

#include "config.h"

#include "auth.h"
#include "infra/fs.h"
#include "json_utils.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace live_stream {
namespace {

constexpr std::size_t kMaxAuthConfigSize = 64 * 1024;
constexpr const char *kPasswordCredentialPrefix = "pbkdf2-sha256:";

bool IsEmptyOrTooLong(const std::string &value, std::size_t max_length) {
    return value.empty() || value.size() > max_length;
}

bool IsSupportedPasswordCredential(const std::string &credential) {
    const std::string prefix = kPasswordCredentialPrefix;
    return credential.compare(0, prefix.size(), prefix) == 0;
}

bool ParseRole(const std::string &role, AuthRole *parsed) {
    if (parsed == nullptr) {
        return false;
    }
    if (role == "admin") {
        *parsed = AuthRole::kAdmin;
        return true;
    }
    if (role == "operator") {
        *parsed = AuthRole::kOperator;
        return true;
    }
    if (role == "viewer") {
        *parsed = AuthRole::kViewer;
        return true;
    }
    return false;
}

const char *RoleToConfigString(AuthRole role) {
    switch (role) {
        case AuthRole::kAdmin:
            return "admin";
        case AuthRole::kOperator:
            return "operator";
        case AuthRole::kViewer:
            return "viewer";
    }
    return "viewer";
}

class ConfigAuthUserStore : public IAuthUserStore {
public:
    explicit ConfigAuthUserStore(const std::string &config_path)
        : config_path_(config_path) {}

    AuthUserRecord FindUser(const std::string &user_name) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!EnsureLoaded()) {
            return AuthUserRecord{};
        }
        const auto iter = users_.find(user_name);
        if (iter == users_.end()) {
            return AuthUserRecord{};
        }
        return iter->second;
    }

    bool UpdatePassword(const std::string &user_name,
                        const std::string &password_credential,
                        bool must_change_password) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (password_credential.empty() || !EnsureLoaded()) {
            return false;
        }
        const auto iter = users_.find(user_name);
        if (iter == users_.end()) {
            return false;
        }

        std::map<std::string, AuthUserRecord> updated_users = users_;
        updated_users[user_name].password_credential = password_credential;
        updated_users[user_name].must_change_password = must_change_password;
        if (!Save(updated_users)) {
            return false;
        }
        users_.swap(updated_users);
        return true;
    }

    bool Reload() override {
        std::lock_guard<std::mutex> guard(mutex_);
        loaded_ = false;
        load_ok_ = true;
        users_.clear();
        return EnsureLoaded();
    }

private:
    bool EnsureLoaded() {
        if (loaded_) {
            return load_ok_;
        }
        loaded_ = true;
        if (config_path_.empty()) {
            load_ok_ = false;
            return false;
        }
        std::string content = infra::File::ReadAll(config_path_);
        if (content.empty() || content.size() > kMaxAuthConfigSize) {
            load_ok_ = false;
            return false;
        }
        ConfigJson document = ConfigJson::parse(content, nullptr, false);
        if (document.is_discarded()) {
            load_ok_ = false;
            return false;
        }
        load_ok_ = Parse(document);
        return load_ok_;
    }

    bool Parse(const ConfigJson &document) {
        if (!document.is_object() || !document.contains("users") ||
            !document.at("users").is_array() || document.at("users").empty()) {
            return false;
        }
        const ConfigJson &users = document.at("users");

        std::map<std::string, AuthUserRecord> parsed_users;
        for (const ConfigJson &user_json : users) {
            if (!user_json.is_object()) {
                return false;
            }
            if (user_json.contains("password") ||
                user_json.contains("password_plaintext")) {
                return false;
            }
            AuthUserRecord user;
            std::string role;
            if (!json_utils::ReadField(user_json, "user_name", &user.user_name) ||
                !json_utils::ReadField(user_json, "role", &role) ||
                !json_utils::ReadField(user_json, "password_credential",
                                       &user.password_credential) ||
                !json_utils::ReadField(user_json, "enabled", &user.enabled) ||
                !ParseRole(role, &user.role) ||
                IsEmptyOrTooLong(user.user_name, kMaxAuthUserNameLength) ||
                !IsSupportedPasswordCredential(user.password_credential)) {
                return false;
            }
            if (user_json.contains("must_change_password") &&
                !json_utils::ReadField(user_json, "must_change_password",
                                       &user.must_change_password)) {
                return false;
            }
            if (parsed_users.find(user.user_name) != parsed_users.end()) {
                return false;
            }
            parsed_users[user.user_name] = user;
        }
        users_.swap(parsed_users);
        return true;
    }

    bool Save(const std::map<std::string, AuthUserRecord> &users) const {
        ConfigJson root = ConfigJson::object();
        root["version"] = 1;
        root["credential_format"] =
            "pbkdf2-sha256:<iterations>:<salt_hex>:<hash_hex>";
        root["users"] = ConfigJson::array();
        for (const auto &item : users) {
            const AuthUserRecord &user = item.second;
            ConfigJson user_json = ConfigJson::object();
            user_json["user_name"] = user.user_name;
            user_json["role"] = RoleToConfigString(user.role);
            user_json["enabled"] = user.enabled;
            user_json["must_change_password"] = user.must_change_password;
            user_json["password_credential"] = user.password_credential;
            root["users"].push_back(std::move(user_json));
        }

        const std::string tmp_path = config_path_ + ".tmp";
        if (!infra::File::WriteAll(tmp_path, root.dump(2) + "\n")) {
            return false;
        }
        if (!infra::File::Rename(tmp_path, config_path_)) {
            (void)infra::File::Remove(tmp_path);
            return false;
        }
        return true;
    }

    std::string config_path_;
    std::mutex mutex_;
    std::map<std::string, AuthUserRecord> users_;
    bool loaded_ = false;
    bool load_ok_ = true;
};

}  // namespace

std::unique_ptr<IAuthUserStore> CreateConfigAuthUserStore(
    const std::string &config_path) {
    return std::unique_ptr<IAuthUserStore>(
        new ConfigAuthUserStore(config_path));
}

}  // namespace live_stream

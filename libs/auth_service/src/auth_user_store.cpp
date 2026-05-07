#include <map>
#include <memory>
#include <string>
#include <vector>

#include "auth_internal.h"
#include "infra/fs.h"
#include "live_stream/json_utils.h"

namespace live_stream {
namespace {

class MemoryAuthUserStore : public IAuthUserStore {
public:
  explicit MemoryAuthUserStore(const std::vector<AuthUserRecord> &users) {
    for (const AuthUserRecord &user : users) {
      users_[user.user_name] = user;
    }
  }

  AuthUserRecord FindUser(const std::string &user_name) override {
    const auto iter = users_.find(user_name);
    if (iter == users_.end()) {
      return AuthUserRecord{};
    }
    return iter->second;
  }

  bool Reload() override { return true; }

private:
  std::map<std::string, AuthUserRecord> users_;
};

class JsonAuthUserStore : public IAuthUserStore {
public:
  explicit JsonAuthUserStore(const std::string &config_path)
      : config_path_(config_path) {}

  AuthUserRecord FindUser(const std::string &user_name) override {
    if (!EnsureLoaded()) {
      return AuthUserRecord{};
    }
    const auto iter = users_.find(user_name);
    if (iter == users_.end()) {
      return AuthUserRecord{};
    }
    return iter->second;
  }

  bool Reload() override {
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
    if (content.empty()) {
      load_ok_ = false;
      return false;
    }
    if (content.size() > auth_internal::kMaxAuthConfigSize) {
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
    const ConfigJson *users = nullptr;
    if (!document.is_object() ||
        !json_utils::LoadArray(document, "users", &users) || users->empty()) {
      return false;
    }

    std::map<std::string, AuthUserRecord> parsed_users;
    for (const ConfigJson &user_json : *users) {
      if (!user_json.is_object()) {
        return false;
      }
      if (user_json.contains("password")) {
        return false;
      }
      AuthUserRecord user;
      std::string role;
      if (!json_utils::Load(user_json, "user_name", &user.user_name) ||
          !json_utils::Load(user_json, "role", &role) ||
          !json_utils::Load(user_json, "password_credential",
                            &user.password_credential) ||
          !json_utils::Load(user_json, "enabled", &user.enabled) ||
          !auth_internal::ParseRole(role, &user.role) ||
          auth_internal::IsEmptyOrTooLong(user.user_name,
                                          auth_internal::kMaxUserNameLength) ||
          user.password_credential.empty()) {
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

  std::string config_path_;
  std::map<std::string, AuthUserRecord> users_;
  bool loaded_ = false;
  bool load_ok_ = true;
};

} // namespace

std::unique_ptr<IAuthUserStore>
CreateMemoryAuthUserStore(const std::vector<AuthUserRecord> &users) {
  return std::unique_ptr<IAuthUserStore>(new MemoryAuthUserStore(users));
}

std::unique_ptr<IAuthUserStore>
CreateJsonAuthUserStore(const std::string &config_path) {
  return std::unique_ptr<IAuthUserStore>(new JsonAuthUserStore(config_path));
}

} // namespace live_stream

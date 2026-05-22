#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "auth_internal.h"

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
        std::lock_guard<std::mutex> guard(mutex_);
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
        const auto iter = users_.find(user_name);
        if (iter == users_.end() || password_credential.empty()) {
            return false;
        }
        iter->second.password_credential = password_credential;
        iter->second.must_change_password = must_change_password;
        return true;
    }

    bool Reload() override { return true; }

private:
    std::mutex mutex_;
    std::map<std::string, AuthUserRecord> users_;
};

}  // namespace

std::unique_ptr<IAuthUserStore>
CreateMemoryAuthUserStore(const std::vector<AuthUserRecord> &users) {
    return std::unique_ptr<IAuthUserStore>(new MemoryAuthUserStore(users));
}

}  // namespace live_stream

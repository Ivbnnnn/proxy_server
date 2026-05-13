#pragma once

#include "proxy/Types.h"

#include <optional>
#include <string>

namespace proxy {

class AdminUser {
public:
    AdminUser() = default;
    AdminUser(int id, std::string login, std::string passwordHash, bool isActive);

    [[nodiscard]] int getId() const;
    [[nodiscard]] const std::string& getLogin() const;
    [[nodiscard]] const std::string& getPasswordHash() const;
    [[nodiscard]] const std::string& getRole() const;
    [[nodiscard]] bool getIsActive() const;
    [[nodiscard]] const DateTime& getCreatedAt() const;
    [[nodiscard]] const std::optional<DateTime>& getLastLoginAt() const;
    [[nodiscard]] bool canLogin() const;

    void setId(int id);
    void setLogin(std::string login);
    void setPasswordHash(std::string passwordHash);
    void setRole(std::string role);
    void setIsActive(bool isActive);
    void setCreatedAt(const DateTime& createdAt);
    void setLastLoginAt(const std::optional<DateTime>& lastLoginAt);

private:
    int id{0};
    std::string login;
    std::string passwordHash;
    std::string role{"ADMIN"};
    bool isActive{true};
    DateTime createdAt{nowUtc()};
    std::optional<DateTime> lastLoginAt;
};

}  // namespace proxy

#include "proxy/AdminUser.h"

namespace proxy {

AdminUser::AdminUser(int idValue, std::string loginValue, std::string passwordHashValue, bool isActiveValue)
    : id(idValue),
      login(std::move(loginValue)),
      passwordHash(std::move(passwordHashValue)),
      isActive(isActiveValue),
      createdAt(nowUtc()) {}

int AdminUser::getId() const { return id; }

const std::string& AdminUser::getLogin() const { return login; }

const std::string& AdminUser::getPasswordHash() const { return passwordHash; }

const std::string& AdminUser::getRole() const { return role; }

bool AdminUser::getIsActive() const { return isActive; }

const DateTime& AdminUser::getCreatedAt() const { return createdAt; }

const std::optional<DateTime>& AdminUser::getLastLoginAt() const { return lastLoginAt; }

bool AdminUser::canLogin() const { return isActive && role == "ADMIN" && !login.empty(); }

void AdminUser::setId(int idValue) { id = idValue; }

void AdminUser::setLogin(std::string loginValue) { login = std::move(loginValue); }

void AdminUser::setPasswordHash(std::string passwordHashValue) { passwordHash = std::move(passwordHashValue); }

void AdminUser::setRole(std::string roleValue) { role = std::move(roleValue); }

void AdminUser::setIsActive(bool isActiveValue) { isActive = isActiveValue; }

void AdminUser::setCreatedAt(const DateTime& createdAtValue) { createdAt = createdAtValue; }

void AdminUser::setLastLoginAt(const std::optional<DateTime>& lastLoginAtValue) { lastLoginAt = lastLoginAtValue; }

}  // namespace proxy

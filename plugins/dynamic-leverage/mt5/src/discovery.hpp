#pragma once

#include "adapter.hpp"
#include <limits>
#include <set>

namespace dynamicleverage::mt5 {

// Keep the SDK-owned buffer guarded before UserLogins is called: error returns
// and exceptions may still leave an allocation in its output parameter.
template<class Api> class LoginBuffer {
public:
  explicit LoginBuffer(Api& api) : api_(api) {}
  ~LoginBuffer() noexcept {
    try { if (data) api_.Free(data); } catch (...) {}
  }
  LoginBuffer(const LoginBuffer&) = delete;
  LoginBuffer& operator=(const LoginBuffer&) = delete;
  std::uint64_t* data = nullptr;
private:
  Api& api_;
};

template<class Api, class Stopped, class Error>
std::vector<Account> discover_accounts(Api& api, Stopped stopped, Error error) {
  if (stopped()) return {};
  LoginBuffer<Api> logins(api);
  auto total = std::numeric_limits<std::uint32_t>::max();
  const auto result = api.UserLogins(L"*", logins.data, total);
  if (!(result == 1 && total == 0)) check_result(result, "UserLogins");
  if (total == std::numeric_limits<std::uint32_t>::max() || (total && !logins.data))
    throw std::runtime_error("UserLogins returned an incomplete account list");

  std::vector<Account> accounts;
  accounts.reserve(total);
  std::set<Login> seen;
  for (std::uint32_t i = 0; i < total && !stopped(); ++i) {
    const auto login = logins.data[i];
    if (!login || !seen.insert(login).second)
      throw std::runtime_error("UserLogins returned invalid/duplicate account");
    try {
      auto user = own(api.UserCreate());
      check_result(api.UserGet(login, user.get()), "UserGet during discovery");
      if (user->Login() != login) throw std::runtime_error("UserGet discovery identity mismatch");
      accounts.push_back({login, group_utf8(user->Group())});
    } catch (const std::exception& failure) {
      // A removed or unreadable user must not prevent all other accounts from
      // being managed. The next periodic discovery retries it.
      error(login, failure.what());
    } catch (...) {
      error(login, "Unknown UserGet discovery error");
    }
  }
  return accounts;
}

} // namespace dynamicleverage::mt5

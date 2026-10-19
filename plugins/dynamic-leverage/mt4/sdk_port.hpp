#pragma once
#include <windows.h>
#include <ctime>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <utility>
#include "MT4ServerAPI.h"
#include "model.hpp"

namespace mt4leverage {
template<std::size_t N> std::string fixed(const char (&value)[N]) {
  const auto* end = static_cast<const char*>(std::memchr(value, 0, N));
  if (!end) throw std::runtime_error("unterminated MT4 SDK field");
  return {value, end};
}
void log(CServerInterface* api, const std::string& message) noexcept;
std::string sdk_text_utf8(const std::string& text);

class SdkPort final : public dynamicleverage::Port {
public:
  explicit SdkPort(CServerInterface* api, std::function<bool()> writable)
    : api_(api), writable_(std::move(writable)) {}
  dynamicleverage::Snapshot snapshot(dynamicleverage::Login login) override;
  bool set_leverage(dynamicleverage::Login login, std::uint32_t expected, std::uint32_t target) override;
  bool set_leverage_for_group(dynamicleverage::Login login, std::uint32_t expected,
                              std::uint32_t target, const std::string& expected_group) override;
  std::vector<dynamicleverage::Account> accounts();
private:
  UserRecord user(int login);
  CServerInterface* api_;
  std::function<bool()> writable_;
};
} // namespace mt4leverage

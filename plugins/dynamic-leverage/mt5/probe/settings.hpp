#pragma once

#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dynamicleverage::mt5::probe {

struct Settings {
  bool enabled = false;
  std::uint64_t account = 0;
  std::uint32_t target_leverage = 500;
};

inline bool eligible_request(const Settings& settings, std::uint64_t request_account,
                             std::uint64_t order_account, bool market_action,
                             bool close_request, bool existing_position,
                             std::uint32_t order_type, std::uint64_t volume) {
  return settings.enabled && settings.account && request_account == settings.account &&
    order_account == settings.account && market_action && !close_request &&
    !existing_position && order_type <= 1 && volume;
}

inline std::uint64_t positive_decimal(const std::wstring& value, std::uint64_t maximum,
                                      const char* parameter) {
  if (value.empty()) throw std::invalid_argument(std::string(parameter) + " must contain one positive decimal integer");
  std::uint64_t result = 0;
  for (const auto character : value) {
    if (character < L'0' || character > L'9')
      throw std::invalid_argument(std::string(parameter) + " must contain one positive decimal integer");
    const auto digit = static_cast<std::uint64_t>(character - L'0');
    if (result > (maximum - digit) / 10)
      throw std::invalid_argument(std::string(parameter) + " exceeds its supported range");
    result = result * 10 + digit;
  }
  if (!result) throw std::invalid_argument(std::string(parameter) + " must be positive");
  return result;
}

inline Settings parse_settings(const std::vector<std::pair<std::wstring, std::wstring>>& values) {
  Settings result;
  std::set<std::wstring> seen;
  for (const auto& [name, value] : values) {
    if (!seen.insert(name).second) throw std::invalid_argument("Duplicate margin probe parameter");
    if (name == L"Enabled") {
      if (value != L"0" && value != L"1") throw std::invalid_argument("Enabled must be 0 or 1");
      result.enabled = value == L"1";
    } else if (name == L"ProbeAccount") {
      if (!value.empty()) result.account = positive_decimal(value, std::numeric_limits<std::uint64_t>::max(), "ProbeAccount");
    } else if (name == L"TargetLeverage") {
      result.target_leverage = static_cast<std::uint32_t>(positive_decimal(
        value, std::numeric_limits<std::uint32_t>::max(), "TargetLeverage"));
    } else {
      throw std::invalid_argument("Unknown margin probe parameter; expected Enabled, ProbeAccount or TargetLeverage");
    }
  }
  if (result.enabled && !result.account)
    throw std::invalid_argument("Enabled margin probe requires exactly one positive ProbeAccount");
  return result;
}

} // namespace dynamicleverage::mt5::probe

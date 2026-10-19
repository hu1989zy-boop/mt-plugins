#pragma once

#include "dynamicleverage/core.hpp"
#include "dynamicleverage/execution.hpp"
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace dynamicleverage::mt5 {

inline std::string group_utf8(const wchar_t* group) {
  if (!group || !*group) throw std::runtime_error("MT5 user group is empty");
  // Windows SDK strings are UTF-16. Also accept native wchar_t code points
  // on non-Windows test hosts without a deprecated codecvt dependency.
  std::string result;
  for (const auto* cursor = group; *cursor; ++cursor) {
    auto point = static_cast<std::uint32_t>(*cursor);
    if (point >= 0xd800 && point <= 0xdbff) {
      const auto low = static_cast<std::uint32_t>(*++cursor);
      if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("MT5 group contains invalid UTF-16");
      point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
    } else if ((point >= 0xdc00 && point <= 0xdfff) || point > 0x10ffff) {
      throw std::runtime_error("MT5 group contains invalid Unicode");
    }
    if (point < 0x80) result.push_back(static_cast<char>(point));
    else if (point < 0x800) {
      result.push_back(static_cast<char>(0xc0 | (point >> 6)));
      result.push_back(static_cast<char>(0x80 | (point & 0x3f)));
    } else if (point < 0x10000) {
      result.push_back(static_cast<char>(0xe0 | (point >> 12)));
      result.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | (point & 0x3f)));
    } else {
      result.push_back(static_cast<char>(0xf0 | (point >> 18)));
      result.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | (point & 0x3f)));
    }
  }
  return result;
}

// SDK-independent template, instantiated with IMTServerAPI in the DLL and a
// fake API in tests. MT5's documented OK / OK_NONE codes are 0 / 1.
inline void check_result(std::uint32_t result, const char* operation) {
  if (result != 0)
    throw std::runtime_error(std::string(operation) + " returned " + std::to_string(result));
}
struct Release {
  template<class T> void operator()(T* object) const { if (object) object->Release(); }
};
template<class T> auto own(T* object) {
  if (!object) throw std::runtime_error("MT5 SDK allocation failed");
  return std::unique_ptr<T, Release>(object);
}

template<class Api> class AccountPort final : public Port {
public:
  explicit AccountPort(Api& api, std::function<bool()> can_write = [] { return true; })
    : api_(api), can_write_(std::move(can_write)) {}

  Snapshot snapshot(Login login) override {
    auto user = own(api_.UserCreate());
    check_result(api_.UserGet(login, user.get()), "UserGet");
    if (user->Login() != login || !user->Leverage())
      throw std::runtime_error("MT5 user identity/leverage is invalid");
    auto positions = own(api_.PositionCreateArray());
    const auto result = api_.PositionGet(login, positions.get());
    // Only the documented no-data success may represent an empty account.
    // PARTIAL / NOTFOUND / network errors must never cause a leverage increase.
    if (!(result == 1 && positions->Total() == 0)) check_result(result, "PositionGet");
    Snapshot snapshot{login, user->Leverage(), {}, group_utf8(user->Group())};
    snapshot.positions.reserve(positions->Total());
    std::set<std::uint64_t> tickets;
    for (std::uint32_t i = 0; i < positions->Total(); ++i) {
      const auto* position = positions->Next(i);
      if (!position || position->Login() != login || !position->Position() ||
          position->Action() > 1 || !position->VolumeExt() ||
          !tickets.insert(position->Position()).second)
        throw std::runtime_error("MT5 position snapshot is incomplete or invalid");
      // VolumeExt has 8 decimal places. BUY and SELL both add to gross lots;
      // PositionGet returns open positions only, so pending orders are excluded.
      snapshot.positions.push_back({position->Position(), login, position->VolumeExt()});
    }
    total_volume(snapshot); // validate aggregate overflow before using the snapshot
    return snapshot;
  }

  bool set_leverage(Login, std::uint32_t, std::uint32_t) override {
    require_native_execution();
  }

  bool set_leverage_for_group(Login, std::uint32_t, std::uint32_t,
                              const std::string&) override {
    require_native_execution();
  }
private:
  Api& api_;
  std::function<bool()> can_write_;
};
} // namespace dynamicleverage::mt5

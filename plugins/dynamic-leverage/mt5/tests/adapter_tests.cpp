#include "adapter.hpp"
#include <iostream>
#include <limits>

using namespace dynamicleverage;
namespace {
void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Fn> void rejects(Fn action, const char* message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  expect(rejected, message);
}
template<class Fn> void execution_blocked(Fn action, const char* message) {
  bool blocked = false;
  try { action(); } catch (const NativeExecutionUnavailable& error) {
    blocked = std::string(error.what()).find("preserve existing positions") != std::string::npos;
  }
  expect(blocked, message);
}
struct User {
  Login login = 1001;
  std::uint32_t leverage = 1000, rights = 17, setter_result = 0;
  std::wstring group = L"demo";
  std::string name = "Original customer";
  void Release() { delete this; }
  Login Login() const { return login; }
  const wchar_t* Group() const { return group.c_str(); }
  std::uint32_t Leverage() const { return leverage; }
  std::uint32_t Leverage(std::uint32_t value) {
    if (!setter_result) leverage = value;
    return setter_result;
  }
};
struct FakePosition {
  std::uint64_t ticket;
  dynamicleverage::Login login;
  std::uint32_t action;
  Volume volume;
  std::uint64_t Position() const { return ticket; }
  dynamicleverage::Login Login() const { return login; }
  std::uint32_t Action() const { return action; }
  Volume VolumeExt() const { return volume; }
};
struct Positions {
  std::vector<FakePosition> rows;
  bool null_row = false;
  void Release() { delete this; }
  std::uint32_t Total() const { return static_cast<std::uint32_t>(rows.size()); }
  const FakePosition* Next(std::uint32_t index) const { return null_row ? nullptr : &rows.at(index); }
};
struct Api {
  User user;
  std::vector<FakePosition> positions;
  std::uint32_t user_result = 0, position_result = 0, update_result = 0;
  unsigned writes = 0, reads = 0, sdk_calls = 0;
  bool null_row = false, allocation_fails = false;
  std::function<void()> on_user_read;
  User* UserCreate() { ++sdk_calls; return allocation_fails ? nullptr : new User; }
  Positions* PositionCreateArray() { ++sdk_calls; return new Positions; }
  std::uint32_t UserGet(dynamicleverage::Login, User* target) {
    ++sdk_calls;
    ++reads;
    *target = user;
    if (on_user_read) on_user_read();
    return user_result;
  }
  std::uint32_t PositionGet(dynamicleverage::Login, Positions* target) {
    ++sdk_calls;
    target->rows = positions;
    target->null_row = null_row;
    return position_result;
  }
  std::uint32_t UserUpdate(User* target) {
    ++sdk_calls;
    ++writes;
    if (!update_result) user = *target;
    return update_result;
  }
};
Config configuration() {
  auto values = default_values();
  values["Enabled"] = "1";
  values["Accounts"] = "1001,1002";
  return parse_config(values);
}
void tests() {
  expect(mt5::group_utf8(L"客户") == "客户", "Unicode group names use UTF-8 matching");
  const wchar_t invalid_group[] = {0xd800, 0};
  rejects([&] { mt5::group_utf8(invalid_group); }, "invalid group encoding rejected");
  Api api;
  mt5::AccountPort<Api> port(api);
  expect(total_volume(port.snapshot(1001)) == 0, "empty account");
  expect(port.snapshot(1001).group == "demo", "current group included in snapshot");
  api.user.group = L"real\\VIP";
  expect(port.snapshot(1001).group == "real\\VIP", "group path preserved");
  api.user.group = L"demo";
  api.positions = {{1, 1001, 0, 2 * units_per_lot}, {2, 1001, 1, 3 * units_per_lot + 1}};
  const auto snapshot = port.snapshot(1001);
  expect(total_volume(snapshot) == 5 * units_per_lot + 1, "BUY+SELL gross VolumeExt precision");
  execution_blocked([&] { reconcile(configuration(), 1001, port); },
                    "obsolete reconciliation cannot change native account leverage");
  expect(api.writes == 0 && api.user.leverage == 1000, "old account-wide execution performs no update");
  api.positions = {{1, 1001, 0, 2 * units_per_lot}};
  expect(target_leverage(configuration(), total_volume(port.snapshot(1001))) == 1000, "strict two-lot boundary remains available offline");
  api.positions = {{1, 1001, 1, 5 * units_per_lot}};
  expect(target_leverage(configuration(), total_volume(port.snapshot(1001))) == 500, "strict five-lot boundary remains available offline");
  api.positions.clear();
  api.position_result = 1;
  expect(total_volume(port.snapshot(1001)) == 0, "no-data success produces an empty snapshot");
  api.position_result = 7;
  const auto before_error = api.writes;
  rejects([&] { reconcile(configuration(), 1001, port); }, "failed read must not be treated as flat");
  expect(api.writes == before_error, "failed snapshot must not write");
  api.position_result = 1;
  api.positions = {{1, 1001, 0, units_per_lot}};
  rejects([&] { port.snapshot(1001); }, "contradictory no-data result");
  api.position_result = 0;
  api.positions = {{1, 1002, 0, units_per_lot}};
  rejects([&] { port.snapshot(1001); }, "foreign account rejected");
  api.positions = {{1, 1001, 2, units_per_lot}};
  rejects([&] { port.snapshot(1001); }, "unknown action rejected");
  api.positions = {{1, 1001, 0, units_per_lot}, {1, 1001, 1, units_per_lot}};
  rejects([&] { port.snapshot(1001); }, "duplicate positions rejected");
  api.positions = {{1, 1001, 0, 0}};
  rejects([&] { port.snapshot(1001); }, "zero-volume live position rejected");
  api.positions = {{1, 1001, 0, units_per_lot}};
  api.null_row = true;
  rejects([&] { port.snapshot(1001); }, "null position rejected");
  api.null_row = false;
  api.positions = {{1, 1001, 0, std::numeric_limits<Volume>::max()}, {2, 1001, 1, 1}};
  rejects([&] { port.snapshot(1001); }, "gross volume overflow rejected");
  api.positions.clear();
  api.user_result = 6;
  rejects([&] { port.snapshot(1001); }, "UserGet failure rejected");
  api.user_result = 0;
  api.allocation_fails = true;
  rejects([&] { port.snapshot(1001); }, "allocation failure rejected");
  api.allocation_fails = false;

  const auto calls_before_block = api.sdk_calls;
  for (const auto target : {200u, 1000u, 0u}) {
    execution_blocked([&] { port.set_leverage(1001, 1000, target); },
                      "direct legacy writes are unavailable, including unchanged and invalid targets");
    execution_blocked([&] { port.set_leverage_for_group(1001, 1000, target, "demo"); },
                      "group-guarded legacy writes are unavailable, including unchanged and invalid targets");
  }
  expect(api.sdk_calls == calls_before_block && api.writes == 0 && api.user.leverage == 1000,
         "blocked write entry points make zero SDK calls and preserve account leverage");
  unsigned write_permission_checks = 0;
  mt5::AccountPort<Api> guarded(api, [&] { ++write_permission_checks; return false; });
  execution_blocked([&] { guarded.set_leverage(1001, 1000, 200); },
                    "guarded adapter also reports capability unavailable rather than retry");
  expect(write_permission_checks == 0 && api.sdk_calls == calls_before_block,
         "write execution cannot reach permission checks or SDK operations");
}
} // namespace
int main() {
  try { tests(); std::cout << "MT5 adapter tests passed\n"; return 0; }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

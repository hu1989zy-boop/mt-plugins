#include "settings.hpp"
#include <iostream>

using namespace dynamicleverage::mt5::probe;
namespace {
void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action) {
  bool rejected = false;
  try { action(); } catch (const std::invalid_argument&) { rejected = true; }
  expect(rejected, "invalid margin probe configuration was accepted");
}
}

int main() {
  try {
    const auto defaults = parse_settings({});
    expect(!defaults.enabled && !defaults.account && defaults.target_leverage == 500, "probe must default to inactive");
    const auto enabled = parse_settings({{L"Enabled", L"1"}, {L"ProbeAccount", L"18446744073709551615"},
                                         {L"TargetLeverage", L"200"}});
    expect(enabled.enabled && enabled.account == std::numeric_limits<std::uint64_t>::max() &&
           enabled.target_leverage == 200, "exact account and target must be preserved");
    rejects([] { parse_settings({{L"Enabled", L"1"}}); });
    rejects([] { parse_settings({{L"Enabled", L"true"}}); });
    rejects([] { parse_settings({{L"Enabled", L"0"}, {L"Enabled", L"0"}}); });
    for (const auto* value : {L"0", L"-1", L"+1", L"1,2", L"*", L" 1001", L"1001 ", L"18446744073709551616"})
      rejects([&] { parse_settings({{L"ProbeAccount", value}}); });
    rejects([] { parse_settings({{L"ProbeAccount", std::wstring(L"1\0", 2)}}); });
    for (const auto* value : {L"", L"0", L"-1", L"1.5", L"4294967296"})
      rejects([&] { parse_settings({{L"TargetLeverage", value}}); });
    rejects([] { parse_settings({{L"Accounts", L"1001"}}); });
    const Settings scoped{true, 1001, 500};
    expect(eligible_request(scoped, 1001, 1001, true, false, false, 0, 100), "matching new market order should be eligible");
    expect(!eligible_request(defaults, 1001, 1001, true, false, false, 0, 100), "disabled probe must remain inactive");
    expect(!eligible_request(scoped, 1002, 1001, true, false, false, 0, 100), "foreign request must be ignored");
    expect(!eligible_request(scoped, 1001, 1002, true, false, false, 0, 100), "foreign order must be ignored");
    expect(!eligible_request(scoped, 1001, 1001, false, false, false, 0, 100), "non-market action must be ignored");
    expect(!eligible_request(scoped, 1001, 1001, true, true, false, 0, 100), "close request must be ignored");
    expect(!eligible_request(scoped, 1001, 1001, true, false, true, 0, 100), "existing-position change must be ignored");
    expect(!eligible_request(scoped, 1001, 1001, true, false, false, 2, 100), "pending order must be ignored");
    expect(!eligible_request(scoped, 1001, 1001, true, false, false, 0, 0), "empty order must be ignored");
    std::cout << "MT5 margin probe settings tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

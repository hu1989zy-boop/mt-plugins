#include "model.hpp"
#include "dynamicleverage/execution.hpp"
#include <climits>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action, const std::string& message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  check(rejected, message);
}
mt4leverage::Order order(int ticket, int command, int volume, int state = 0, std::int64_t closed = 0) {
  return {ticket, 12345, command, volume, closed, state};
}
}

int main() {
  try {
    using namespace dynamicleverage;
    using namespace mt4leverage;
    bool blocked = false;
    try { require_native_execution(); }
    catch (const NativeExecutionUnavailable& error) {
      blocked = true;
      check(std::string(error.what()).find("preserve existing positions") != std::string::npos,
            "startup guard explains why the old executor is unavailable");
    }
    check(blocked, "native execution guard rejects obsolete account-wide execution");
    auto values = default_values();
    auto config = parse(values);
    check(!config.enabled && config.accounts.empty(), "default must perform no account writes");
    values["Enabled"] = "1"; values["Accounts"] = "12345,98765";
    config = parse(values);
    check(config.accounts.size() == 2, "accounts parsed independently");
    check(make_snapshot(12345, 1000, {}, "real\\VIP").group == "real\\VIP", "snapshot retains group path");
    check(unchanged_user(500, "real", 500, "real"), "fresh matching user allowed");
    check(!unchanged_user(500, "demo", 500, "real"), "migration rejects stale group rule before write");
    check(!unchanged_user(200, "real", 500, "real"), "concurrent leverage edit rejects write");
    check(!unchanged_user(0, "real", 0, "real"), "invalid native leverage rejects write");
    check(volume_units(100) == units_per_lot && volume_units(1) == 1000000, "MT4 hundredth-lot conversion");
    check(volume_units(INT_MAX) == static_cast<Volume>(INT_MAX) * 1000000, "conversion avoids signed integer overflow");
    rejects([] { volume_units(0); }, "zero live volume rejected");
    rejects([] { volume_units(-1); }, "negative live volume rejected");

    // Buy and sell positions add gross volume, including partial remainders and restores.
    const auto mixed = make_snapshot(12345, 1000, {
      order(1, 0, 200), order(2, 1, 250, 1), order(3, 0, 51, 2),
      order(4, 2, 100000), order(5, 3, 100000), order(6, 4, 100000), order(7, 5, 100000),
      order(8, 6, 0), order(9, 7, 0), order(10, 0, 99999, 3, 123),
      order(11, 1, 99999, 4, 123), order(12, 0, 99999, 5, 123), order(13, 0, 99999, 6)
    });
    check(mixed.positions.size() == 3 && total_volume(mixed) == parse_lots("5.01"), "gross exposure filters pending/history/balance/deleted");
    check(target_leverage(config, total_volume(mixed)) == 200, "5.01 lots switches to 200");
    check(target_leverage(config, total_volume(make_snapshot(12345, 1000, {}))) == 1000, "flat account restores base leverage");
    for (const auto& item : {std::pair<int, unsigned>{200, 1000}, {201, 500}, {500, 500}, {501, 200}})
      check(target_leverage(config, total_volume(make_snapshot(12345, 1000, {order(1, 0, item.first)}))) == item.second,
        "strict tier threshold for MT4 native volume " + std::to_string(item.first));

    auto foreign = order(2, 0, 1); foreign.login = 98765;
    rejects([&] { make_snapshot(12345, 1000, {order(1, 0, 100), foreign}); }, "mixed account snapshot rejected");
    rejects([] { make_snapshot(12345, 1000, {order(1, 0, 100), order(1, 1, 100)}); }, "duplicate ticket rejected");
    rejects([] { make_snapshot(12345, 1000, {order(0, 0, 100)}); }, "invalid ticket rejected");
    rejects([] { make_snapshot(12345, 0, {}); }, "invalid account leverage rejected");
    rejects([] { make_snapshot(12345, 1000, {order(1, 0, -1)}); }, "corrupt live volume cannot appear flat");
    rejects([] { make_snapshot(12345, 1000, {order(1, 8, 100)}); }, "unknown command rejected");
    rejects([] { make_snapshot(12345, 1000, {order(1, 0, 100, 7)}); }, "unknown record state rejected");
    rejects([] { native_login(0); }, "zero login rejected");
    rejects([] { native_login(static_cast<Login>(INT_MAX) + 1); }, "MT4 login overflow rejected");
    check(native_login(INT_MAX) == INT_MAX, "largest MT4 login accepted");

    auto invalid = values; invalid["Accounts"] = "2147483648";
    rejects([&] { parse(invalid); }, "MT4 config login overflow rejected");
    invalid = values; invalid["Accounts"] = std::string(128, '1');
    rejects([&] { parse(invalid); }, "oversized native parameter rejected");
    invalid = values; invalid["Accounts"] = std::string("12345\0,98765", 12);
    rejects([&] { parse(invalid); }, "embedded null rejected");
    std::cout << "MT4 model: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "MT4 model test failed: " << error.what() << '\n';
    return 1;
  }
}

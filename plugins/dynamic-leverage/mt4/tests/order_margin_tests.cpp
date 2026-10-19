#include "order_margin.hpp"

#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace dynamicleverage;
using namespace mt4leverage;
unsigned checks = 0;
#pragma pack(push, 1)
struct TestTrade {
  int order = 17;
  int login = 1001;
  int cmd = 0;
  int state = 0;
  int volume = 100;
  std::int64_t close_time = 0;
  double margin_rate = 1.2;
  int api_data[4]{};
  double open_price = 123.45;
  double sl = 100;
  double tp = 150;
  double profit = 0;
  double commission = -2;
  double storage = -1;
  double conv_rates[2]{1.1, 1.3};
};
#pragma pack(pop)
void check(bool condition, const char* reason) {
  ++checks;
  if (!condition) throw std::runtime_error(reason);
}
bool same(const TestTrade& a, const TestTrade& b) {
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}
template<class Action> void rejects(TestTrade& trade, Action operation, const char* reason) {
  const auto before = trade;
  bool rejected = false;
  try { operation(); } catch (const std::exception&) { rejected = true; }
  check(rejected, reason);
  check(same(trade, before), "rejected transform leaves all order bytes untouched");
}
Config policy() {
  Config config;
  config.platform = "mt4";
  config.enabled = true;
  config.rules = {{"group", "Opening rule", true, Scope::group, "*,!ABC"}};
  return config;
}
OrderMarginContext context() {
  return {{1001, "live"}, 1000, 1000, parse_lots("1.5"), OrderAddMode::new_trade};
}
void new_order_and_duplicates() {
  auto config = policy();
  const auto input = rules_json(config);
  auto ctx = context();
  TestTrade old;
  old.order = 10;
  old.volume = 150;
  const auto old_before = old;
  TestTrade trade;
  auto expected = trade;
  const auto decision = transform_order_margin(trade, config, ctx);
  check(decision.disposition == OrderMarginDisposition::newly_locked && decision.changed,
        "genuine new order receives a lock");
  check(decision.target_leverage == 500 && decision.original_margin_rate == 1.2 && trade.margin_rate == 2.4,
        "1.5 existing plus 1 new uses 500 and preserves original 1.2 conversion factor");
  check(decision.opening_quote.before_volume == parse_lots("1.5") &&
        decision.opening_quote.opening_volume == units_per_lot &&
        decision.opening_quote.after_volume == parse_lots("2.5"), "quote uses explicit before exposure");
  check(trade.api_data[0] == order_margin_lock_magic && trade.api_data[1] == 500, "version marker and target persist in slots");
  double stored = 0;
  std::memcpy(&stored, &trade.api_data[2], sizeof(stored));
  check(stored == 1.2, "double original rate stored without precision loss");
  expected.margin_rate = trade.margin_rate;
  std::memcpy(expected.api_data, trade.api_data, sizeof(expected.api_data));
  check(same(trade, expected), "only margin_rate and owned metadata are changed");
  check(same(old, old_before), "previous order is never mutated");
  check(rules_json(config) == input, "configuration is unchanged");
  const auto locked = trade;
  for (int i = 0; i < 3; ++i) {
    const auto duplicate = transform_order_margin(trade, config, ctx);
    check(duplicate.disposition == OrderMarginDisposition::existing_lock && !duplicate.changed &&
          duplicate.target_leverage == 500 && same(trade, locked), "duplicate callback does not compound factor");
  }
  config.rules[0].name = "Changed";
  config.rules[0].tiers = {{0, 50}};
  config.enabled = false;
  ctx.before_volume = 100 * units_per_lot;
  const auto retained = transform_order_margin(trade, config, ctx);
  check(retained.target_leverage == 500 && same(trade, locked), "existing lock ignores disabled or changed rules and exposure");
  check(decision.opening_quote.rule_name == "Opening rule" && decision.opening_quote.rule_id == "group",
        "returned opening decision owns copied rule strings");
  check(retained.opening_quote.rule_id.empty(), "restored metadata does not invent a persisted rule identity");
}
void inherited_and_unmarked_records() {
  auto config = policy();
  auto ctx = context();
  TestTrade original;
  (void)transform_order_margin(original, config, ctx);
  for (const auto mode : {OrderAddMode::close_remainder, OrderAddMode::restore, OrderAddMode::rollover, OrderAddMode::api}) {
    auto remainder = original;
    remainder.order = 18;
    remainder.volume = 40;
    remainder.state = 1;
    ctx.mode = mode;
    ctx.before_volume = 0;
    const auto before = remainder;
    const auto inherited = transform_order_margin(remainder, config, ctx);
    check(inherited.target_leverage == 500 && !inherited.changed && same(remainder, before),
          "inherited lock retains old factor and does not select a current tier");
    remainder.margin_rate = 1.2;
    const auto reapplied = transform_order_margin(remainder, config, ctx);
    check(reapplied.changed && remainder.margin_rate == 2.4 && reapplied.target_leverage == 500,
          "native-copy reset restores the stored original factor and locked target");
    TestTrade unmarked;
    const auto unmarked_before = unmarked;
    const auto decision = transform_order_margin(unmarked, config, ctx);
    check(decision.disposition == OrderMarginDisposition::unmanaged && same(unmarked, unmarked_before),
          "unmarked old or unverified API order is untouched");
  }
  ctx = context();
  for (const int state : {1, 2, 3, 4, 5, 6}) {
    TestTrade old;
    old.state = state;
    const auto before = old;
    check(transform_order_margin(old, config, ctx).disposition == OrderMarginDisposition::unmanaged && same(old, before),
          "state prevents accidental repricing of old records even in new callback mode");
  }
  for (const int command : {2, 3, 4, 5, 6, 7}) {
    TestTrade other;
    other.cmd = command;
    const auto before = other;
    check(transform_order_margin(other, config, ctx).disposition == OrderMarginDisposition::unmanaged && same(other, before),
          "pending and accounting records are untouched");
  }
  TestTrade closed;
  closed.close_time = 100;
  const auto before = closed;
  check(transform_order_margin(closed, config, ctx).disposition == OrderMarginDisposition::unmanaged && same(closed, before),
        "closed record is untouched");
}
void unmatched_and_invalid() {
  auto config = policy();
  auto ctx = context();
  TestTrade trade;
  const auto pristine = trade;
  config.enabled = false;
  check(transform_order_margin(trade, config, ctx).disposition == OrderMarginDisposition::unmanaged && same(trade, pristine),
        "disabled rules do not claim slots or alter margin");
  config.enabled = true;
  ctx.account.group = "ABC";
  check(transform_order_margin(trade, config, ctx).disposition == OrderMarginDisposition::unmanaged && same(trade, pristine),
        "excluded rule leaves new order untouched");
  ctx = context();
  for (int slot = 0; slot < 4; ++slot) {
    trade = pristine;
    trade.api_data[slot] = 123;
    rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "occupied foreign slot is rejected without overwrite");
  }
  trade = pristine;
  (void)transform_order_margin(trade, config, ctx);
  const auto locked = trade;
  for (const int target : {0, -1, 100001}) {
    trade = locked;
    trade.api_data[1] = target;
    rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "malformed lock target rejected");
  }
  for (const double rate : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    trade = locked;
    std::memcpy(&trade.api_data[2], &rate, sizeof(rate));
    rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "malformed stored original rate rejected");
    trade = pristine;
    trade.margin_rate = rate;
    rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "nonpositive or nonfinite native rate rejected");
  }
  trade = pristine;
  trade.margin_rate = std::numeric_limits<double>::max();
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "finite multiplication overflow rejected");
  trade = pristine;
  trade.margin_rate = 1.2;
  ctx.fixed_account_leverage = ctx.observed_account_leverage = 1;
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "target above fixed baseline rejected without silent clamping");
  ctx = context();
  trade = pristine;
  ctx.observed_account_leverage = 500;
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "changed account baseline rejected");
  ctx = context();
  ctx.fixed_account_leverage = ctx.observed_account_leverage = 0;
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "zero fixed baseline rejected");
  ctx = context();
  ctx.account.login = 2002;
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "foreign order account rejected");
  ctx = context();
  ctx.mode = static_cast<OrderAddMode>(5);
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "unknown callback mode rejected");
  ctx = context();
  ctx.before_volume = std::numeric_limits<Volume>::max();
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "aggregate volume overflow rejected");
  ctx = context();
  trade.volume = 0;
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "zero opening volume rejected");
  trade = pristine;
  trade.cmd = 8;
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "invalid command rejected");
  trade = pristine;
  trade.state = 7;
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "invalid state rejected");
  trade = pristine;
  config.platform = "mt5";
  rejects(trade, [&] { (void)transform_order_margin(trade, config, ctx); }, "other platform rules rejected");
}
} // namespace

int main() {
  try {
    new_order_and_duplicates();
    inherited_and_unmarked_records();
    unmatched_and_invalid();
    std::cout << checks << " MT4 order margin checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "MT4 order margin test failure: " << error.what() << '\n';
    return 1;
  }
}

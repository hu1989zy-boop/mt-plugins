#include "dynamicleverage/opening_policy.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace dynamicleverage;
unsigned checks = 0;

void check(bool condition, const char* reason) {
  ++checks;
  if (!condition) throw std::runtime_error(reason);
}

template<class F> void rejects(F&& operation, const char* reason) {
  bool failed = false;
  try { operation(); } catch (const std::invalid_argument&) { failed = true; }
  check(failed, reason);
}

Config policy(const std::string& platform) {
  Config config;
  config.platform = platform;
  config.enabled = true;
  config.rules = {{"group", "Group rule", true, Scope::group, "*,!ABC"},
                  {"account", "Account rule", true, Scope::account, "1001,1002"}};
  config.rules[1].tiers = {{0, 800}, {2 * units_per_lot, 400}, {5 * units_per_lot, 100}};
  return config;
}

void post_opening_tiers(const std::string& platform) {
  const auto config = policy(platform);
  const Account account{1001, "live"};
  const auto before = rules_json(config);
  const auto quote = quote_opening(config, account, parse_lots("1.5"), 0, units_per_lot);
  check(quote.matched && quote.leverage == 500, "new 1 lot uses post-open 2.5-lot tier");
  check(quote.rule_id == "group" && quote.rule_name == "Group rule", "first matching rule is captured");
  check(quote.before_volume == parse_lots("1.5") && quote.closing_volume == 0 &&
        quote.opening_volume == units_per_lot && quote.after_volume == parse_lots("2.5"),
        "quote reports separate before, closing, opening and after exposure");
  check(rules_json(config) == before && account.login == 1001 && account.group == "live",
        "quote does not mutate input configuration or account identity");
  check(quote_opening(config, account, 0, 0, 2 * units_per_lot).leverage == 1000,
        "opening exactly 2 lots keeps first tier");
  check(quote_opening(config, account, 2 * units_per_lot, 0, 1).leverage == 500,
        "smallest increment beyond 2 lots selects second tier");
  check(quote_opening(config, account, 2 * units_per_lot, 0, 3 * units_per_lot).leverage == 500,
        "opening to exactly 5 lots keeps second tier");
  check(quote_opening(config, account, 5 * units_per_lot, 0, 1).leverage == 200,
        "smallest increment beyond 5 lots selects third tier");
  const auto crossing = quote_opening(config, account, units_per_lot, 0, 5 * units_per_lot);
  check(crossing.opening_volume == 5 * units_per_lot && crossing.leverage == 200,
        "entire new volume uses one final tier, without splitting across thresholds");
}

void priority_and_no_fallback(const std::string& platform) {
  auto config = policy(platform);
  const Account account{1001, "live"};
  std::swap(config.rules[0], config.rules[1]);
  auto quote = quote_opening(config, account, 2 * units_per_lot, 0, units_per_lot);
  check(quote.rule_id == "account" && quote.leverage == 400, "raised account priority wins");
  config.rules[0].enabled = false;
  quote = quote_opening(config, account, 2 * units_per_lot, 0, units_per_lot);
  check(quote.rule_id == "group" && quote.leverage == 500, "disabled priority rule falls through");
  quote = quote_opening(config, {3001, "ABC"}, 0, 0, units_per_lot);
  check(!quote.matched && quote.leverage == 0 && quote.rule_id.empty() && quote.rule_name.empty(),
        "excluded unmatched account has no fallback leverage");
  check(quote.after_volume == units_per_lot, "unmatched quote still reports resulting exposure");
  config.rules[0].enabled = true;
  quote = quote_opening(config, {1002, "ABC"}, 0, 0, units_per_lot);
  check(quote.rule_id == "account" && quote.leverage == 800,
        "explicit account can match separately despite group exclusion");
  config.enabled = false;
  quote = quote_opening(config, account, 0, 0, units_per_lot);
  check(!quote.matched && quote.leverage == 0 && quote.rule_id.empty(),
        "disabled configuration returns unmatched without fallback");
  config.rules.clear();
  check(!quote_opening(config, account, 0, 0, 1).matched, "disabled empty rules return unmatched");
}

void reversals_and_owned_result(const std::string& platform) {
  auto config = policy(platform);
  const Account account{1001, "live"};
  const auto old = quote_opening(config, account, 0, 0, parse_lots("1.5"));
  const auto later = quote_opening(config, account, parse_lots("1.5"), 0, units_per_lot);
  check(old.leverage == 1000 && later.leverage == 500, "later opening quote cannot reprice an earlier result");
  // Existing 4 lots includes 1 opposite-side lot to close. A 2-lot reversal
  // closes that 1 lot and newly opens 1 lot; gross exposure remains 4 lots.
  const auto reversal = quote_opening(config, account, 4 * units_per_lot,
                                       units_per_lot, units_per_lot);
  check(reversal.after_volume == 4 * units_per_lot && reversal.leverage == 500,
        "reversal subtracts closing part and adds only new opening part");
  const auto reduction = quote_opening(config, account, 6 * units_per_lot,
                                        6 * units_per_lot, units_per_lot);
  check(reduction.after_volume == units_per_lot && reduction.leverage == 1000,
        "full old exposure closure and small reopening select post-trade tier");
  check(later.leverage == 500, "subsequent lower-tier quote leaves earlier returned result unchanged");
  config.rules[0].id = "replacement";
  config.rules[0].name = "Changed";
  config.rules[0].tiers = {{0, 50}};
  config.rules.clear();
  check(later.rule_id == "group" && later.rule_name == "Group rule" && later.leverage == 500,
        "returned rule identity, name and leverage are owned values independent of config lifetime");
}

void invalid_inputs_and_limits(const std::string& platform) {
  auto config = policy(platform);
  const Account account{1001, "live"};
  rejects([&] { (void)quote_opening(config, account, units_per_lot, 0, 0); },
          "zero opening volume is rejected, including pure close requests");
  rejects([&] { (void)quote_opening(config, account, 1, 2, 1); }, "over-closing is rejected");
  constexpr auto maximum = std::numeric_limits<Volume>::max();
  rejects([&] { (void)quote_opening(config, account, maximum, 0, 1); }, "post-open volume overflow is rejected");
  const auto exact = quote_opening(config, account, maximum, 1, 1);
  check(exact.after_volume == maximum && exact.leverage == 200, "maximum result is accepted without intermediate overflow");
  check(quote_opening(config, account, maximum, maximum, 1).after_volume == 1,
        "closing subtraction occurs before opening addition");
  rejects([&] { (void)quote_opening(config, {0, "live"}, 0, 0, 1); }, "zero account is rejected");
  rejects([&] { (void)quote_opening(config, {1001, "bad\n"}, 0, 0, 1); }, "invalid group metadata is rejected");
  rejects([&] { (void)quote_opening(config, {1001, std::string(256, 'a')}, 0, 0, 1); },
          "overlong group metadata is rejected");
  if (platform == "mt4") {
    check(quote_opening(config, {2147483647, "live"}, 0, 0, 1).matched, "maximum MT4 account is accepted");
    rejects([&] { (void)quote_opening(config, {2147483648ULL, "live"}, 0, 0, 1); },
            "MT4 account above INT_MAX is rejected");
  } else {
    check(quote_opening(config, {std::numeric_limits<Login>::max(), "live"}, 0, 0, 1).matched,
          "maximum MT5 account retains integer precision");
  }
  config.rules[0].tiers = {{0, 1000}, {units_per_lot, 1000}};
  rejects([&] { (void)quote_opening(config, account, 0, 0, 1); }, "invalid rule configuration is rejected");
}
} // namespace

int main() {
  try {
    for (const std::string platform : {"mt4", "mt5"}) {
      post_opening_tiers(platform);
      priority_and_no_fallback(platform);
      reversals_and_owned_result(platform);
      invalid_inputs_and_limits(platform);
    }
    auto legacy = parse_config({{"Enabled", "1"}, {"Accounts", "1001"}});
    rejects([&] { (void)quote_opening(legacy, {1001, "live"}, 0, 0, 1); },
            "legacy account-leverage configuration cannot be used as an opening policy");
    auto unknown = policy("MT5");
    rejects([&] { (void)quote_opening(unknown, {1001, "live"}, 0, 0, 1); }, "unknown platform is rejected");
    std::cout << checks << " opening policy checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "opening policy test failure: " << error.what() << '\n';
    return 1;
  }
}

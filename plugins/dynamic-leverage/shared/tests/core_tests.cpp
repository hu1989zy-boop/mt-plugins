#include "dynamicleverage/core.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace dynamicleverage;

unsigned checks = 0;

void check(bool condition, const std::string& description) {
  ++checks;
  if (!condition) throw std::runtime_error(description);
}

template<class F> void throws(F&& operation, const std::string& description) {
  bool rejected = false;
  try { operation(); } catch (const std::exception&) { rejected = true; }
  check(rejected, description);
}

Config config() {
  return parse_config({{"Enabled", "1"}, {"Accounts", "1001,1002"}});
}

Snapshot snapshot(Login login, std::uint32_t leverage,
                  const std::vector<Volume>& volumes = {}) {
  Snapshot result{login, leverage, {}};
  for (const auto volume : volumes)
    result.positions.push_back({result.positions.size() + 1, login, volume});
  return result;
}

struct Write {
  Login login;
  std::uint32_t expected;
  std::uint32_t target;
};

struct ScriptPort : Port {
  std::vector<Snapshot> reads;
  std::vector<Login> requested;
  std::vector<Write> writes;
  std::size_t throw_read_at = std::numeric_limits<std::size_t>::max();
  bool write_allowed = true;
  bool throw_write = false;

  explicit ScriptPort(std::vector<Snapshot> values = {}) : reads(std::move(values)) {}

  Snapshot snapshot(Login login) override {
    const auto index = requested.size();
    requested.push_back(login);
    if (index == throw_read_at) throw std::runtime_error("simulated read failure");
    if (reads.empty()) throw std::runtime_error("unexpected account read");
    return reads[std::min(index, reads.size() - 1)];
  }

  bool set_leverage(Login login, std::uint32_t expected, std::uint32_t target) override {
    writes.push_back({login, expected, target});
    if (throw_write) throw std::runtime_error("simulated write failure");
    return write_allowed;
  }
};

void default_configuration_is_safe_and_explicit() {
  const auto defaults = default_values();
  check(defaults.at("Enabled") == "0", "plugin starts disabled");
  check(defaults.at("Accounts").empty(), "default configuration targets no account");
  check(defaults.at("Tiers") == "0:1000;2:500;5:200", "default tiers match requested policy");
  check(defaults.at("PollIntervalMs") == "1000", "default polling interval is one second");
  const auto disabled = parse_config({});
  check(!disabled.enabled && disabled.accounts.empty(), "empty values use safe defaults");
  check(!manages(disabled, 1001), "disabled configuration does not manage any account");
  const auto active = config();
  check(manages(active, 1001) && manages(active, 1002), "explicit accounts are individually managed");
  check(!manages(active, 1003) && !manages(active, 0), "unlisted and zero accounts are excluded");
  const auto trimmed = parse_config({{"Enabled", " 1 "}, {"Accounts", " 1001 , 1002 "},
                                    {"Tiers", " 0 : 1000 ; 2 : 500 ; 5 : 200 "}});
  check(trimmed.accounts.size() == 2 && trimmed.tiers.size() == 3,
        "human-readable whitespace around configuration tokens is accepted");
}

void numeric_parsing_preserves_exact_volume() {
  check(parse_lots("0") == 0, "zero lots is exact");
  check(parse_lots("2") == 2 * units_per_lot, "whole lots use common fixed-point units");
  check(parse_lots("0.00000001") == 1, "smallest fractional lot is retained");
  check(parse_lots("2.00000001") == 2 * units_per_lot + 1, "threshold plus one unit is retained");
  check(parse_lots("5.001") == 5 * units_per_lot + 100000, "fractional lots are right-padded");
  check(parse_lots(" 1.25 ") == units_per_lot + units_per_lot / 4, "outer whitespace is accepted");
  const auto maximum = std::numeric_limits<Volume>::max();
  check(parse_lots("184467440737.09551615") == maximum, "maximum volume parses without rounding");
  for (const Volume value : {Volume{0}, Volume{1}, units_per_lot, units_per_lot + 1,
                             5 * units_per_lot + 12345678, maximum}) {
    check(parse_lots(format_lots(value)) == value, "formatted lot volume round-trips exactly");
  }
  check(format_lots(125000000) == "1.25", "lot formatter removes insignificant trailing zeroes");
  for (const std::string bad : {"", "-1", "+1", "NaN", "nan", "inf", "1e2", "0x10",
                                ".5", "1.", "1.2.3", "1 0", "0.000000001",
                                "184467440737.09551616", "184467440738", "18446744073709551616"}) {
    throws([&] { (void)parse_lots(bad); }, "invalid or overflowing lot string rejected: " + bad);
  }
}

void malformed_configuration_is_rejected() {
  for (const std::string bad : {"", "0", "1001,1001", "1001,0", "1001,", ",1001", "1001,,1002",
                                "-1", "+1001", "1e3", "NaN", "18446744073709551616"}) {
    throws([&] { (void)parse_config({{"Enabled", "1"}, {"Accounts", bad}}); },
           "enabled account list rejected: " + bad);
  }
  for (const std::string bad : {"", "true", "-1", "2", "1.0"}) {
    throws([&] { (void)parse_config({{"Enabled", bad}}); }, "invalid enabled value rejected: " + bad);
  }
  for (const std::string bad : {"", "99", "60001", "-100", "1e3", "100.0", "NaN", "18446744073709551616"}) {
    throws([&] { (void)parse_config({{"PollIntervalMs", bad}}); },
           "invalid polling interval rejected: " + bad);
  }
  check(parse_config({{"PollIntervalMs", "100"}}).poll_interval_ms == 100, "minimum polling interval accepted");
  check(parse_config({{"PollIntervalMs", "60000"}}).poll_interval_ms == 60000, "maximum polling interval accepted");
  for (const std::string bad : {"", "0", "0:", ":1000", "0:1000;", "0:1000;;2:500", "0:1000:500",
                                "1:1000", "0:1000;0:500", "0:1000;5:500;2:200",
                                "0:1000;2:1000", "0:1000;2:2000", "0:0", "0:100001",
                                "0:-1", "0:1e3", "0:NaN", "0:18446744073709551616",
                                "0:1000;2.000000001:500"}) {
    throws([&] { (void)parse_config({{"Tiers", bad}}); }, "invalid leverage tiers rejected: " + bad);
  }
  throws([] { (void)parse_config({{"Account", "1001"}}); }, "unknown parameter catches configuration typo");
  check(parse_config({{"Tiers", "0:1"}}).tiers.front().leverage == 1, "minimum leverage accepted");
  check(parse_config({{"Tiers", "0:100000"}}).tiers.front().leverage == 100000, "maximum leverage accepted");
}

void programmatic_configuration_is_validated() {
  const auto good = config();
  std::vector<Config> invalid;
  auto bad = good;
  bad.accounts.clear(); invalid.push_back(bad);
  bad = good; bad.accounts = {0}; invalid.push_back(bad);
  bad = good; bad.accounts = {1001, 1001}; invalid.push_back(bad);
  bad = good; bad.poll_interval_ms = 99; invalid.push_back(bad);
  bad = good; bad.poll_interval_ms = 60001; invalid.push_back(bad);
  bad = good; bad.tiers.clear(); invalid.push_back(bad);
  bad = good; bad.tiers.front().above = 1; invalid.push_back(bad);
  bad = good; bad.tiers[1].above = 0; invalid.push_back(bad);
  bad = good; bad.tiers[1].leverage = 1000; invalid.push_back(bad);
  bad = good; bad.tiers[1].leverage = 1001; invalid.push_back(bad);
  bad = good; bad.tiers.front().leverage = 0; invalid.push_back(bad);
  bad = good; bad.tiers.front().leverage = 100001; invalid.push_back(bad);
  for (const auto& value : invalid) throws([&] { validate(value); }, "invalid programmatic configuration rejected");
  auto many = good;
  many.tiers.clear();
  for (Volume i = 0; i < 32; ++i) many.tiers.push_back({i, static_cast<std::uint32_t>(1000 - i)});
  validate(many);
  check(many.tiers.size() == 32, "maximum 32 valid leverage tiers accepted");
  many.tiers.push_back({32, 968});
  throws([&] { validate(many); }, "more than 32 leverage tiers rejected");
}

void strict_tier_boundaries_and_recovery() {
  const auto cfg = config();
  for (const auto& row : std::vector<std::pair<Volume, std::uint32_t>>{
         {0, 1000}, {1, 1000}, {2 * units_per_lot - 1, 1000}, {2 * units_per_lot, 1000},
         {2 * units_per_lot + 1, 500}, {5 * units_per_lot - 1, 500}, {5 * units_per_lot, 500},
         {5 * units_per_lot + 1, 200}, {std::numeric_limits<Volume>::max(), 200}}) {
    check(target_leverage(cfg, row.first) == row.second, "strict threshold mapping at " + format_lots(row.first));
  }
  const auto before = snapshot(1001, 200, {units_per_lot});
  const auto after = snapshot(1001, 1000, {units_per_lot});
  ScriptPort port({before, before, after});
  const auto restored = reconcile(cfg, 1001, port);
  check(restored.status == Status::updated && restored.previous == 200 && restored.target == 1000,
        "reducing exposure restores the higher leverage tier");
  const auto empty_before = snapshot(1001, 200);
  const auto empty_after = snapshot(1001, 1000);
  ScriptPort flat_port({empty_before, empty_before, empty_after});
  check(reconcile(cfg, 1001, flat_port).status == Status::updated,
        "closing every position restores base leverage");
}

void gross_volume_and_snapshot_validation() {
  const auto combined = snapshot(1001, 1000, {units_per_lot, units_per_lot, 1});
  check(total_volume(combined) == 2 * units_per_lot + 1, "all positions are summed using gross volume");
  check(target_leverage(config(), total_volume(combined)) == 500, "separate positions together cross account threshold");
  check(total_volume(snapshot(1001, 1000)) == 0, "valid empty account has zero exposure");
  std::vector<Snapshot> invalid;
  auto bad = combined;
  bad.login = 0; invalid.push_back(bad);
  bad = combined; bad.leverage = 0; invalid.push_back(bad);
  bad = combined; bad.positions[0].login = 0; invalid.push_back(bad);
  bad = combined; bad.positions[0].login = 1002; invalid.push_back(bad);
  bad = combined; bad.positions[0].ticket = 0; invalid.push_back(bad);
  bad = combined; bad.positions[1].ticket = bad.positions[0].ticket; invalid.push_back(bad);
  bad = combined; bad.positions[0].volume = 0; invalid.push_back(bad);
  invalid.push_back(snapshot(1001, 1000, {std::numeric_limits<Volume>::max(), 1}));
  for (const auto& value : invalid) throws([&] { (void)total_volume(value); }, "malformed snapshot rejected");
  check(total_volume(snapshot(1001, 1000, {std::numeric_limits<Volume>::max()})) ==
        std::numeric_limits<Volume>::max(), "maximum total volume is accepted without overflow");
  ScriptPort wrong_account({snapshot(1002, 1000, {3 * units_per_lot})});
  throws([&] { (void)reconcile(config(), 1001, wrong_account); }, "unexpected account snapshot rejected");
  check(wrong_account.writes.empty(), "wrong-account snapshot never causes a write");
}

void disabled_unlisted_and_unchanged_accounts_do_not_write() {
  auto cfg = config();
  cfg.enabled = false;
  ScriptPort forbidden;
  check(reconcile(cfg, 1001, forbidden).status == Status::skipped, "disabled account is skipped");
  cfg.enabled = true;
  check(reconcile(cfg, 1003, forbidden).status == Status::skipped, "unlisted account is skipped");
  check(forbidden.requested.empty() && forbidden.writes.empty(), "skipped accounts never call the API");
  ScriptPort matched({snapshot(1001, 500, {3 * units_per_lot})});
  const auto result = reconcile(cfg, 1001, matched);
  check(result.status == Status::unchanged && result.volume == 3 * units_per_lot &&
        result.previous == 500 && result.target == 500, "already-correct leverage reports unchanged exposure");
  check(matched.writes.empty(), "already-correct leverage is never rewritten");
}

void stable_update_preserves_account_identity_and_is_idempotent() {
  const auto before = snapshot(1001, 1000, {units_per_lot, 2 * units_per_lot});
  auto confirmation = before;
  std::reverse(confirmation.positions.begin(), confirmation.positions.end());
  auto after = confirmation;
  after.leverage = 500;
  ScriptPort port({before, confirmation, after});
  const auto result = reconcile(config(), 1001, port);
  check(result.status == Status::updated, "position enumeration order does not prevent stable update");
  check(result.volume == 3 * units_per_lot && result.previous == 1000 && result.target == 500,
        "result records exposure and leverage transition");
  check(port.requested.size() == 3, "successful write is preceded by two reads and followed by confirmation");
  check(port.writes.size() == 1 && port.writes[0].login == 1001 &&
        port.writes[0].expected == 1000 && port.writes[0].target == 500,
        "write addresses only requested account with expected old leverage");
  check(std::all_of(port.requested.begin(), port.requested.end(), [](Login value) { return value == 1001; }),
        "all snapshots belong to requested account");
  check(reconcile(config(), 1001, port).status == Status::unchanged && port.writes.size() == 1,
        "next polling pass does not repeat a confirmed write");
}

void changing_preflight_snapshots_never_write() {
  const auto before = snapshot(1001, 1000, {units_per_lot, 2 * units_per_lot});
  std::vector<Snapshot> changed;
  auto next = before;
  next.leverage = 200; changed.push_back(next);
  next = before; ++next.positions[0].volume; changed.push_back(next);
  next = before; next.positions.erase(next.positions.begin()); changed.push_back(next);
  next = before; next.positions.push_back({3, 1001, units_per_lot}); changed.push_back(next);
  next = before; next.positions[0].ticket = 99; changed.push_back(next);
  next = before; next.positions[0].volume += units_per_lot;
  next.positions[1].volume -= units_per_lot; changed.push_back(next);
  for (const auto& value : changed) {
    ScriptPort port({before, value});
    check(reconcile(config(), 1001, port).status == Status::retry,
          "preflight change requests a fresh pass even when total exposure is unchanged");
    check(port.writes.empty(), "changed preflight snapshot cannot trigger a write");
  }
  ScriptPort compare_failed({before, before});
  compare_failed.write_allowed = false;
  check(reconcile(config(), 1001, compare_failed).status == Status::retry,
        "write-time leverage comparison failure requests retry");
  check(compare_failed.writes.size() == 1 && compare_failed.requested.size() == 2,
        "rejected compare-and-set has no post-write success assumption");
}

void unconfirmed_writes_require_retry() {
  const auto before = snapshot(1001, 1000, {3 * units_per_lot});
  auto changed_positions = snapshot(1001, 500, {4 * units_per_lot});
  ScriptPort moved({before, before, changed_positions});
  check(reconcile(config(), 1001, moved).status == Status::retry,
        "positions changing after a write require reevaluation");
  ScriptPort ignored({before, before, before});
  check(reconcile(config(), 1001, ignored).status == Status::retry,
        "API success without observed target leverage is not reported as updated");
  auto changed_ticket = snapshot(1001, 500, {3 * units_per_lot});
  changed_ticket.positions[0].ticket = 99;
  ScriptPort replaced({before, before, changed_ticket});
  check(reconcile(config(), 1001, replaced).status == Status::retry,
        "replaced position with same volume is detected after a write");
}

void api_errors_propagate_and_later_passes_recover() {
  const auto before = snapshot(1001, 1000, {3 * units_per_lot});
  const auto after = snapshot(1001, 500, {3 * units_per_lot});
  for (const std::size_t fail_at : {std::size_t{0}, std::size_t{1}, std::size_t{2}}) {
    ScriptPort port({before, before, after});
    port.throw_read_at = fail_at;
    throws([&] { (void)reconcile(config(), 1001, port); }, "snapshot API failure propagates to worker");
    check(port.writes.size() == (fail_at == 2 ? 1U : 0U),
          "read failures never create writes before valid preflight");
  }
  ScriptPort uncertain({before, before, after});
  uncertain.throw_write = true;
  throws([&] { (void)reconcile(config(), 1001, uncertain); }, "write API failure propagates to worker");
  uncertain.throw_write = false;
  check(reconcile(config(), 1001, uncertain).status == Status::unchanged && uncertain.writes.size() == 1,
        "uncertain write followed by observed target does not duplicate update");

  ScriptPort first_account({before});
  first_account.throw_read_at = 0;
  const auto second_before = snapshot(1002, 1000, {6 * units_per_lot});
  const auto second_after = snapshot(1002, 200, {6 * units_per_lot});
  ScriptPort second_account({second_before, second_before, second_after});
  unsigned failed_accounts = 0;
  unsigned updated_accounts = 0;
  const auto cfg = config();
  for (const auto login : cfg.accounts) {
    try {
      Port& port = login == 1001 ? static_cast<Port&>(first_account) : static_cast<Port&>(second_account);
      if (reconcile(cfg, login, port).status == Status::updated) ++updated_accounts;
    } catch (const std::exception&) { ++failed_accounts; }
  }
  check(failed_accounts == 1 && updated_accounts == 1, "per-account worker catch isolates account failures");
  check(second_account.writes.size() == 1 && second_account.writes.front().login == 1002,
        "healthy account remains independently managed");
}
} // namespace

int main() {
  const std::vector<std::pair<const char*, std::function<void()>>> tests{
    {"default_configuration_is_safe_and_explicit", default_configuration_is_safe_and_explicit},
    {"numeric_parsing_preserves_exact_volume", numeric_parsing_preserves_exact_volume},
    {"malformed_configuration_is_rejected", malformed_configuration_is_rejected},
    {"programmatic_configuration_is_validated", programmatic_configuration_is_validated},
    {"strict_tier_boundaries_and_recovery", strict_tier_boundaries_and_recovery},
    {"gross_volume_and_snapshot_validation", gross_volume_and_snapshot_validation},
    {"disabled_unlisted_and_unchanged_accounts_do_not_write", disabled_unlisted_and_unchanged_accounts_do_not_write},
    {"stable_update_preserves_account_identity_and_is_idempotent", stable_update_preserves_account_identity_and_is_idempotent},
    {"changing_preflight_snapshots_never_write", changing_preflight_snapshots_never_write},
    {"unconfirmed_writes_require_retry", unconfirmed_writes_require_retry},
    {"api_errors_propagate_and_later_passes_recover", api_errors_propagate_and_later_passes_recover},
  };
  unsigned failures = 0;
  for (const auto& test : tests) {
    try {
      test.second();
      std::cout << "PASS " << test.first << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() << " cases, " << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}

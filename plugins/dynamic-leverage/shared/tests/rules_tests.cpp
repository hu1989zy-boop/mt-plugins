#include "dynamicleverage/core.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dynamicleverage;
unsigned checks = 0;
void check(bool condition, const char* reason) {
  ++checks;
  if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F&& operation, const char* reason) {
  bool failed = false;
  try { operation(); } catch (const std::exception&) { failed = true; }
  check(failed, reason);
}
Config policy() {
  Config c;
  c.enabled = true;
  c.platform = "mt5";
  c.rules = {{"group", "组别规则", true, Scope::group, "!demo*,*"},
             {"account", "账户规则", true, Scope::account, "1001,1002"}};
  c.rules[1].tiers = {{0, 800}, {2 * units_per_lot, 400}};
  return c;
}
std::string replace(std::string text, const std::string& from, const std::string& to) {
  const auto at = text.find(from);
  if (at == std::string::npos) throw std::runtime_error("missing replacement fixture");
  text.replace(at, from.size(), to);
  return text;
}
void masks_and_priority() {
  check(group_matches("*", "ABC"), "wildcard selects all groups");
  check(!group_matches("*", "") && !group_matches("!ABC", ""), "empty group metadata cannot match");
  check(group_matches("ABC", "ABC") && !group_matches("ABC", "ABC-live"), "literal group is exact");
  check(group_matches("ABC*", "ABC-live") && !group_matches("ABC*", "abc-live"), "matching is case-sensitive");
  check(group_matches("*ABC*", "live-ABC-pro"), "contains wildcard works");
  check(group_matches("*,!ABC", "DEF") && !group_matches("*,!ABC", "ABC"), "exclusion overrides earlier include");
  check(group_matches("!ABC,*", "DEF") && !group_matches("!ABC,*", "ABC"), "exclusion order is irrelevant");
  check(group_matches("!ABC", "DEF") && !group_matches("!ABC", "ABC"), "only excludes imply all remaining groups");
  check(group_matches("A*,B*", "BBB") && !group_matches("A*,B*", "CCC"), "includes are OR conditions");
  check(group_matches("live\\客户\\*", "live\\客户\\专业"), "backslash paths and UTF-8 match literally");
  check(group_matches("a**b*c", "a-long-b-c"), "consecutive stars and backtracking work");
  for (const std::string bad : {"", "!", "A?", "A!B", "*,,A", "*,", ",*", "A\t", "A\n"})
    rejects([&] { (void)group_matches(bad, "A"); }, "invalid group mask is rejected");
  auto c = policy();
  check(select_rule(c, 1001, "live")->id == "group", "earlier group takes priority over account");
  std::swap(c.rules[0], c.rules[1]);
  check(select_rule(c, 1001, "live")->id == "account", "moving account up changes winner");
  check(select_rule(c, 3001, "live")->id == "group", "nonmatching earlier rule falls through");
  c.rules[0].enabled = false;
  check(select_rule(c, 1001, "live")->id == "group", "disabled earlier rule falls through");
  check(select_rule(c, 3001, "demo-test") == nullptr, "excluded group has no matching rule");
  check(select_rule(c, 3001, "") == nullptr, "missing group does not match wildcard");
  c.enabled = false;
  check(select_rule(c, 1001, "live") == nullptr && !has_group_rules(c), "disabled config selects nothing");
}
void discovery_and_legacy() {
  auto c = policy();
  check(has_group_rules(c), "enabled group rule triggers discovery");
  const auto logins = candidate_accounts(c, {{3001, "live"}, {3001, "live"}, {4001, "demo"}, {1001, "live"}});
  check(logins == std::vector<Login>({1001, 1002, 3001}), "explicit and matching discovered accounts are deduplicated");
  rejects([&] { (void)candidate_accounts(c, {{3001, "live"}, {3001, "demo"}}); }, "ambiguous discovered group is rejected");
  rejects([&] { (void)candidate_accounts(c, {{0, "live"}}); }, "zero discovered login is rejected");
  c.enabled = false;
  check(candidate_accounts(c, {{3001, "live"}}).empty(), "disabled policy produces no candidate accounts");
  const auto legacy = parse_config({{"Enabled", "1"}, {"Accounts", "1002,1001"}});
  check(candidate_accounts(legacy, {{3001, "live"}}) == std::vector<Login>({1001, 1002}), "legacy only manages explicit accounts");
  const auto file = parse_config({{"Enabled", "1"}, {"RulesFile", "C:\\rules\\mt5.json"}});
  check(file.enabled && file.accounts.empty() && !file.rules_file.empty(), "RulesFile bootstraps enabled configuration");
  rejects([] { (void)parse_config({{"RulesFile", "rules.json"}, {"Accounts", "1001"}}); }, "RulesFile and legacy Accounts cannot mix");
  check(default_values().at("RulesFile").empty(), "legacy defaults have empty RulesFile");
}
void json_roundtrip_and_limits() {
  const auto text = rules_json(policy());
  const auto parsed = parse_rules_json(text, "mt5");
  check(parsed.rules.size() == 2 && parsed.rules[0].name == "组别规则", "Unicode rule names round-trip");
  check(rules_json(parsed) == text, "JSON serializer is deterministic and round-trips");
  auto c = policy();
  c.rules[0].selector = "live\\客户\\*";
  c.rules[1].selector = "18446744073709551615";
  c.rules[1].tiers.back().above = std::numeric_limits<Volume>::max();
  auto max = parse_rules_json(rules_json(c), "mt5");
  check(select_rule(max, std::numeric_limits<Login>::max(), "demo")->id == "account", "UINT64 login retains precision");
  check(max.rules[1].tiers.back().above == std::numeric_limits<Volume>::max(), "maximum lots retain precision");
  c.platform = "mt4";
  rejects([&] { (void)rules_json(c); }, "MT4 account greater than INT_MAX rejected");
  c.rules[1].selector = "2147483647";
  check(parse_rules_json(rules_json(c), "mt4").rules.size() == 2, "maximum MT4 login accepted");
  rejects([&] { (void)parse_rules_json(text, "mt4"); }, "platform mismatch rejected");
  rejects([&] { (void)parse_rules_json(text, "MT5"); }, "unknown expected platform rejected");
  c = policy(); c.rules.clear(); c.enabled = false;
  check(parse_rules_json(rules_json(c), "mt5").rules.empty(), "disabled empty rules file is supported");
  c.enabled = true;
  rejects([&] { validate(c); }, "enabled configuration requires an enabled rule");
  c = policy(); for (auto& r : c.rules) r.enabled = false;
  rejects([&] { validate(c); }, "enabled config with only disabled rules rejected");
  c.enabled = false; validate(c);
  c = policy(); c.rules[1].selector = "1001,01001";
  rejects([&] { validate(c); }, "duplicate numeric logins rejected despite leading zeroes");
  c = policy(); c.rules[1].id = c.rules[0].id;
  rejects([&] { validate(c); }, "duplicate IDs rejected");
  c = policy(); c.rules[1].id = "规则";
  rejects([&] { validate(c); }, "non ASCII ID rejected");
  c = policy(); c.rules[0].name.clear();
  rejects([&] { validate(c); }, "empty rule name rejected");
  for (const std::string blank : {" ", "　", " ", "\xef\xbb\xbf", "　 \xef\xbb\xbf"}) {
    c = policy(); c.rules[0].name = blank;
    rejects([&] { validate(c); }, "Unicode-whitespace-only name rejected");
  }
  c = policy(); c.rules[0].selector = "　ABC　";
  check(group_matches(c.rules[0].selector, "　ABC　") && !group_matches(c.rules[0].selector, "ABC"),
        "Unicode selector whitespace remains literal");
  c = policy(); c.rules[1].selector = std::string(254, '0') + "1";
  check(parse_rules_json(rules_json(c), "mt5").rules.size() == 2, "255-character numeric selector preserves valid leading zeroes");
  c.rules[1].selector = std::string(255, '0') + "1";
  rejects([&] { validate(c); }, "256-character numeric selector rejected");
  c = policy(); c.rules[0].name = std::string(81, 'a');
  rejects([&] { validate(c); }, "overlong rule name rejected");
  c = policy(); c.rules[0].name.clear(); for (unsigned i = 0; i < 80; ++i) c.rules[0].name += "规";
  validate(c); check(parse_rules_json(rules_json(c), "mt5").rules[0].name == c.rules[0].name, "80 Unicode characters accepted");
  c.rules[0].name += "规";
  rejects([&] { validate(c); }, "81 Unicode characters rejected");
  c = policy(); c.rules[0].selector = std::string(256, 'a');
  rejects([&] { validate(c); }, "selector token length bounded");
  c = policy(); c.rules[0].selector.clear(); for (unsigned i = 0; i < 257; ++i) c.rules[0].selector += i ? ",*" : "*";
  rejects([&] { validate(c); }, "selector token count bounded");
  c = policy(); c.rules[0].selector = std::string(4097, 'a');
  rejects([&] { validate(c); }, "selector length bounded");
  c = policy(); c.rules[0].tiers = {{0, 1000}, {1, 1000}};
  rejects([&] { validate(c); }, "tier leverage must decrease");
  c = policy(); c.rules.clear();
  for (unsigned i = 0; i < 256; ++i) {
    Rule r{"r" + std::to_string(i), "rule", true, Scope::group, "*"};
    r.tiers.clear(); for (unsigned t = 0; t < 32; ++t) r.tiers.push_back({t, 1000 - t});
    c.rules.push_back(r);
  }
  check(parse_rules_json(rules_json(c), "mt5").rules.size() == 256, "maximum rules and tiers fit parser node budget");
  c.rules.push_back({"overflow", "rule", true, Scope::group, "*"});
  rejects([&] { validate(c); }, "rule count bounded");
}
void hostile_json_is_rejected() {
  const auto text = rules_json(policy());
  const std::vector<std::string> bad{
    "", "[]", "{}", text + "false", text + std::string(1, '\0'),
    replace(text, "\"schema_version\": 1", "\"schema_version\": 2"),
    replace(text, "\"schema_version\": 1", "\"schema_version\": 1.0"),
    replace(text, "\"schema_version\": 1", "\"schema_version\": 1, \"schema_version\": 1"),
    replace(text, "\"schema_version\": 1", "\"schema_version\": 1, \"schem\\u0061_version\": 1"),
    replace(text, "\"platform\": \"mt5\"", "\"platform\": \"mt5\", \"extra\": 1"),
    replace(text, "\"enabled\": true", "\"enabled\": 1"),
    replace(text, "\"enabled\": true", "\"enabled\": null"),
    replace(text, "\"poll_interval_ms\": 1000", "\"poll_interval_ms\": 99"),
    replace(text, "\"poll_interval_ms\": 1000", "\"poll_interval_ms\": 1e3"),
    replace(text, "\"poll_interval_ms\": 1000", "\"poll_interval_ms\": 01000"),
    replace(text, "\"poll_interval_ms\": 1000", "\"poll_interval_ms\": -1000"),
    replace(text, "\"selector\": \"1001,1002\"", "\"selector\": 1001"),
    replace(text, "\"scope\": \"group\"", "\"scope\": \"unknown\""),
    replace(text, "\"above_lots\": \"2\"", "\"above_lots\": 2"),
    replace(text, "\"above_lots\": \"2\"", "\"above_lots\": \" 2\""),
    replace(text, "\"above_lots\": \"2\"", "\"above_lots\": \"0.000000001\""),
    replace(text, "\"above_lots\": \"2\"", "\"above_lots\": \"" + std::string(40, '0') + "2\""),
    replace(text, "\"leverage\": 1000", "\"leverage\": \"1000\""),
    replace(text, "\"leverage\": 1000", "\"leverage\": 0"),
    replace(text, "\"name\": \"组别规则\"", "\"name\": \"bad\\u0000\""),
    replace(text, "\"name\": \"组别规则\"", "\"name\": \"bad\\u0085\""),
    replace(text, "\"name\": \"组别规则\"", "\"name\": \"\\ud800\""),
    replace(text, "\"name\": \"组别规则\"", "\"name\": \"\\udc00\""),
    replace(text, "\"name\": \"组别规则\"", "\"name\": \"\\ud800\\u0000\""),
    replace(text, "\"name\": \"组别规则\"", std::string("\"name\": \"") + char(0xc0) + char(0x80) + "\""),
    replace(text, "\"name\": \"组别规则\"", std::string("\"name\": \"") + char(0xed) + char(0xa0) + char(0x80) + "\""),
    replace(text, "\"name\": \"组别规则\"", std::string("\"name\": \"") + char(0xf4) + char(0x90) + char(0x80) + char(0x80) + "\""),
    std::string(1024 * 1024 + 1, ' '), std::string(100, '[') + std::string(100, ']'),
  };
  for (const auto& value : bad) rejects([&] { (void)parse_rules_json(value, "mt5"); }, "malformed JSON/config rejected");
  auto escaped = replace(text, "\"name\": \"组别规则\"", "\"name\": \"\\ud83d\\ude00\"");
  check(parse_rules_json(escaped, "mt5").rules[0].name == "😀", "valid surrogate pair decoded");
  const auto padded = replace(text, "\"above_lots\": \"2\"", "\"above_lots\": \"" + std::string(39, '0') + "2\"");
  check(parse_rules_json(padded, "mt5").rules[0].tiers[1].above == 2 * units_per_lot,
        "40-character lot string preserves valid leading zeroes");
}

struct GroupPort : Port {
  std::vector<Snapshot> reads;
  std::size_t count = 0;
  std::size_t writes = 0;
  std::string group_expected;
  bool changed_at_write = false;
  Snapshot snapshot(Login) override { return reads.at(std::min(count++, reads.size() - 1)); }
  bool set_leverage(Login, std::uint32_t, std::uint32_t) override { throw std::runtime_error("legacy write used"); }
  bool set_leverage_for_group(Login, std::uint32_t, std::uint32_t, const std::string& expected) override {
    ++writes; group_expected = expected; return !changed_at_write;
  }
};
void group_move_races_do_not_report_success() {
  const Snapshot initial{1001, 1000, {{1, 1001, 3 * units_per_lot}}, "live"};
  auto moved = initial; moved.group = "demo";
  auto after = initial; after.leverage = 500;
  GroupPort pre; pre.reads = {initial, moved};
  check(reconcile(policy(), 1001, pre).status == Status::retry && pre.writes == 0, "preflight group move blocks write even if account fallback exists");
  GroupPort fresh; fresh.reads = {initial, initial}; fresh.changed_at_write = true;
  check(reconcile(policy(), 1001, fresh).status == Status::retry && fresh.group_expected == "live", "fresh group compare prevents stale write");
  GroupPort post; auto after_move = after; after_move.group = "demo"; post.reads = {initial, initial, after_move};
  check(reconcile(policy(), 1001, post).status == Status::retry, "postwrite group change requires reevaluation");
  GroupPort stable; stable.reads = {initial, initial, after};
  const auto result = reconcile(policy(), 1001, stable);
  check(result.status == Status::updated && result.rule_id == "group" && result.target == 500, "winning rule and account snapshot drive stable update");
  GroupPort fallback; moved.leverage = 400; fallback.reads = {moved};
  check(reconcile(policy(), 1001, fallback).rule_id == "account", "live snapshot group is used to select account fallback");
  GroupPort excluded; moved.login = 9001; moved.positions[0].login = 9001; excluded.reads = {moved};
  check(reconcile(policy(), 9001, excluded).status == Status::skipped && excluded.writes == 0, "unmatched fresh snapshot causes no write");
}

struct LiveAccountPort : Port {
  Snapshot current{1001, 100, {}, "live"};
  unsigned writes = 0;
  bool readable = true;
  Snapshot snapshot(Login) override {
    if (!readable) throw std::runtime_error("account temporarily unavailable");
    return current;
  }
  bool set_leverage(Login, std::uint32_t, std::uint32_t) override {
    throw std::runtime_error("legacy write used for rules");
  }
  bool set_leverage_for_group(Login login, std::uint32_t expected, std::uint32_t target,
                              const std::string& expected_group) override {
    if (login != current.login || expected != current.leverage || expected_group != current.group) return false;
    current.leverage = target;
    ++writes;
    return true;
  }
  void lots(Volume volume) {
    current.positions.clear();
    if (volume) current.positions.push_back({1, current.login, volume});
  }
};

void native_preview_matches_account_reconciliation() {
  const std::vector<std::string> lots{"0", "2", "2.00000001", "5", "5.00000001", "184467440737.09551615"};
  for (const std::string platform : {"mt4", "mt5"}) {
    auto config = policy(); config.platform = platform;
    for (const std::string group : {"live", "demo", ""}) {
      for (const auto& volume : lots) {
        const auto preview = preview_rules(config, "1001", group, volume);
        LiveAccountPort port; port.current.group = group; port.lots(parse_lots(volume));
        const auto applied = reconcile(config, 1001, port);
        check(preview.matched && preview.leverage == applied.target && preview.rule_id == applied.rule_id &&
              preview.volume == applied.volume && applied.status == Status::updated,
              "offline preview and account writes agree for every tier boundary and scope");
      }
    }
    config.enabled = false;
    check(!preview_rules(config, "1001", "live", "3").matched, "disabled preview never matches");
    config.enabled = true;
    check(!preview_rules(config, "9999", "demo", "3").matched, "excluded account preview never matches");
  }
  auto config = policy(); config.rules[1].selector = "18446744073709551615";
  const auto large = preview_rules(config, "18446744073709551615", "demo", "2.00000001");
  check(large.matched && large.rule_id == "account" && large.leverage == 400, "MT5 maximum login remains precise in preview");
  for (const std::string login : {"", "0", "-1", "1.0", " 1", "1 ", "1,2", "18446744073709551616"})
    rejects([&] { (void)preview_rules(config, login, "live", "0"); }, "preview rejects malformed or out-of-range login");
  check(preview_rules(config, std::string(254, '0') + "1", "live", "0").matched, "preview supports maximum-length padded login");
  rejects([&] { (void)preview_rules(config, std::string(255, '0') + "1", "live", "0"); }, "preview login length bounded");
  for (const std::string volume : {"", " 2", "2 ", ".2", "2.", "-1", "1e2", "0.000000001", "184467440737.09551616"})
    rejects([&] { (void)preview_rules(config, "1", "live", volume); }, "preview rejects noncontract lots");
  rejects([&] { (void)preview_rules(config, "1", "live", std::string(40, '0') + "1"); }, "preview lots length bounded");
  rejects([&] { (void)preview_rules(config, "1", std::string(256, 'a'), "0"); }, "preview group length bounded");
  rejects([&] { (void)preview_rules(config, "1", "invalid\n", "0"); }, "preview rejects group control characters");
  rejects([&] { (void)preview_rules(config, "1", std::string(1, char(0xff)), "0"); }, "preview rejects group invalid UTF-8");
  config.platform = "mt4"; config.rules[1].selector = "1001";
  rejects([&] { (void)preview_rules(config, "2147483648", "live", "0"); }, "preview bounds MT4 login independently of matching");
}

void json_rules_drive_priority_migration_and_position_changes() {
  auto source = policy();
  auto loaded = parse_rules_json(rules_json(source), "mt5");
  LiveAccountPort port;
  auto apply = [&] { return reconcile(loaded, port.current.login, port); };
  check(apply().target == 1000 && port.current.leverage == 1000, "loaded first group rule sets zero-position leverage");
  port.lots(2 * units_per_lot);
  check(apply().status == Status::unchanged && port.writes == 1, "exact two lots remains in base tier");
  port.lots(2 * units_per_lot + 1);
  check(apply().target == 500 && port.current.leverage == 500, "opening beyond threshold lowers leverage");
  port.lots(5 * units_per_lot + 1);
  check(apply().target == 200 && port.current.leverage == 200, "larger aggregate lowers leverage again");
  port.lots(5 * units_per_lot);
  check(apply().target == 500 && port.current.leverage == 500, "partial close restores higher tier");
  std::swap(source.rules[0], source.rules[1]);
  loaded = parse_rules_json(rules_json(source), "mt5");
  check(apply().rule_id == "account" && port.current.leverage == 400, "loaded reordered rules alter winner on same account");
  port.lots(0);
  check(apply().target == 800 && port.current.leverage == 800, "closing all restores winning account rule base leverage");
  source.rules[0].enabled = false;
  loaded = parse_rules_json(rules_json(source), "mt5");
  check(apply().rule_id == "group" && port.current.leverage == 1000, "disabled higher-priority rule falls through after reload");
  port.current.group = "demo-excluded";
  const auto before_exclusion = port.writes;
  check(apply().status == Status::skipped && port.writes == before_exclusion,
        "moving out of all rules stops management and does not invent a restore target");
  source.rules[0].enabled = true;
  loaded = parse_rules_json(rules_json(source), "mt5");
  check(apply().rule_id == "account" && port.current.leverage == 800, "account selector remains effective after group migration");
  source.enabled = false;
  loaded = parse_rules_json(rules_json(source), "mt5");
  port.readable = false;
  check(apply().status == Status::skipped, "disabled config stops even account reads");
  source.enabled = true;
  loaded = parse_rules_json(rules_json(source), "mt5");
  const auto before_failure = port.writes;
  rejects([&] { (void)apply(); }, "unreadable positions propagate without treating them as closed");
  check(port.writes == before_failure && port.current.leverage == 800, "read failure never changes leverage");
  port.readable = true; port.lots(3 * units_per_lot);
  check(apply().status == Status::updated && port.current.leverage == 400, "next healthy snapshot resumes current rule");
}

void file_loading_is_bounded_and_fails_closed() {
  const auto dir = std::filesystem::temp_directory_path() /
    ("dynamic-leverage-rule-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(dir);
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{dir};
  const auto file = dir / std::filesystem::u8path("规则.json");
  auto write = [&](const std::string& contents) { std::ofstream output(file, std::ios::binary); output << contents; };
  rejects([&] { (void)load_rules_file(file.u8string(), "mt5"); }, "missing file does not produce defaults");
  rejects([&] { (void)load_rules_file("rules.json", "mt5"); }, "relative deployment rules path rejected");
  rejects([&] { (void)load_rules_file(dir.u8string(), "mt5"); }, "directory read rejected");
  write(rules_json(policy()));
  const auto loaded = load_rules_file(file.u8string(), "mt5");
  check(loaded.rules.size() == 2 && loaded.rules_file == file.u8string(), "file loads valid Unicode path and preserves source");
  write("{"); rejects([&] { (void)load_rules_file(file.u8string(), "mt5"); }, "truncated file does not produce defaults");
  write(std::string(1024 * 1024 + 1, ' ')); rejects([&] { (void)load_rules_file(file.u8string(), "mt5"); }, "oversized file rejected");
  write(""); rejects([&] { (void)load_rules_file(file.u8string(), "mt5"); }, "empty file rejected");
}
}
int main() {
  const std::vector<std::pair<const char*, std::function<void()>>> tests{
    {"masks_and_priority", masks_and_priority}, {"discovery_and_legacy", discovery_and_legacy},
    {"json_roundtrip_and_limits", json_roundtrip_and_limits}, {"hostile_json_is_rejected", hostile_json_is_rejected},
    {"group_move_races_do_not_report_success", group_move_races_do_not_report_success},
    {"native_preview_matches_account_reconciliation", native_preview_matches_account_reconciliation},
    {"json_rules_drive_priority_migration_and_position_changes", json_rules_drive_priority_migration_and_position_changes},
    {"file_loading_is_bounded_and_fails_closed", file_loading_is_bounded_and_fails_closed},
  };
  unsigned failed = 0;
  for (const auto& test : tests) {
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
  }
  std::cout << tests.size() << " cases, " << checks << " checks, " << failed << " failures\n";
  return failed ? 1 : 0;
}

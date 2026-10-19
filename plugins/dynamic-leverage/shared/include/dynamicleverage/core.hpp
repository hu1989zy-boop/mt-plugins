#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dynamicleverage {

using Login = std::uint64_t;
using Volume = std::uint64_t;
constexpr Volume units_per_lot = 100000000;

struct Tier {
  Volume above = 0;
  std::uint32_t leverage = 1000;
};

enum class Scope { group, account };
struct Rule {
  std::string id;
  std::string name;
  bool enabled = true;
  Scope scope = Scope::group;
  std::string selector;
  std::vector<Tier> tiers{{0, 1000}, {2 * units_per_lot, 500}, {5 * units_per_lot, 200}};
};

using Values = std::map<std::string, std::string>;
struct Config {
  bool enabled = false;
  std::vector<Login> accounts;
  std::vector<Tier> tiers{{0, 1000}, {2 * units_per_lot, 500}, {5 * units_per_lot, 200}};
  std::uint32_t poll_interval_ms = 1000;
  std::string platform;
  std::vector<Rule> rules;
  std::string rules_file;
};

struct Account {
  Login login = 0;
  std::string group;
};

Values default_values();
Config parse_config(const Values& values);
void validate(const Config& config);
Volume parse_lots(const std::string& text);
std::string format_lots(Volume volume);
std::uint32_t target_leverage(const Config& config, Volume volume);
bool manages(const Config& config, Login login);
// Within one mask, includes are OR'ed and any exclusion vetoes a match.
// An exclusion-only mask implicitly includes all remaining groups.
bool group_matches(const std::string& mask, const std::string& group);
// Rule array order is priority across both scopes; the first enabled match wins.
const Rule* select_rule(const Config& config, Login login, const std::string& group);
std::vector<Login> candidate_accounts(const Config& config, const std::vector<Account>& discovered);
bool has_group_rules(const Config& config);
Config parse_rules_json(const std::string& text, const std::string& expected_platform);
std::string rules_json(const Config& config);
Config load_rules_file(const std::string& path, const std::string& expected_platform);

struct RulePreview {
  bool matched = false;
  std::string rule_id;
  std::string rule_name;
  std::uint32_t leverage = 0;
  Volume volume = 0;
};
// Exact offline simulation using the same selection and tier implementation as
// reconciliation. String input preserves all 64-bit login and volume values.
RulePreview preview_rules(const Config& config, const std::string& login,
                          const std::string& group, const std::string& lots);

struct Position {
  std::uint64_t ticket = 0;
  Login login = 0;
  Volume volume = 0;
};
struct Snapshot {
  Login login = 0;
  std::uint32_t leverage = 0;
  std::vector<Position> positions;
  std::string group;
};
Volume total_volume(const Snapshot& snapshot);

class Port {
public:
  virtual ~Port() = default;
  // Return a complete account snapshot or throw. Never convert read errors to an empty account.
  virtual Snapshot snapshot(Login login) = 0;
  // Re-read the user immediately before writing, preserve all other fields, and
  // return false if its leverage no longer equals expected. Throw on API failure.
  virtual bool set_leverage(Login login, std::uint32_t expected, std::uint32_t target) = 0;
  // Native ports must compare the freshly read group as well as leverage before updating.
  virtual bool set_leverage_for_group(Login login, std::uint32_t expected,
                                      std::uint32_t target, const std::string& expected_group) {
    (void)expected_group;
    return set_leverage(login, expected, target);
  }
};

enum class Status { skipped, unchanged, updated, retry };
struct Result {
  Status status = Status::skipped;
  Volume volume = 0;
  std::uint32_t previous = 0;
  std::uint32_t target = 0;
  std::string rule_id;
};
// Worker-thread only. Fresh reads make startup/restart and retries idempotent.
// A second snapshot avoids writing a tier observed during a changing position set.
Result reconcile(const Config& config, Login login, Port& port);

} // namespace dynamicleverage

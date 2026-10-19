#include "dynamicleverage/core.hpp"

#include <algorithm>
#include <climits>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

namespace dynamicleverage {
namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::invalid_argument(message);
}

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::uint64_t number(const std::string& text) {
  require(!text.empty(), "empty numeric value");
  std::uint64_t result = 0;
  for (const char c : text) {
    require(c >= '0' && c <= '9', "expected an unsigned decimal integer");
    const auto digit = static_cast<std::uint64_t>(c - '0');
    require(result <= (std::numeric_limits<std::uint64_t>::max() - digit) / 10,
            "numeric overflow");
    result = result * 10 + digit;
  }
  return result;
}

std::vector<std::string> split(const std::string& text, char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  for (;;) {
    const auto end = text.find(delimiter, start);
    auto part = trim(text.substr(start, end == std::string::npos ? end : end - start));
    require(!part.empty(), "empty configuration list item");
    parts.push_back(std::move(part));
    if (end == std::string::npos) return parts;
    start = end + 1;
  }
}

// Count Unicode scalar values while rejecting malformed UTF-8 and control codes.
std::size_t text_length(const std::string& text, bool* nonblank = nullptr) {
  std::size_t count = 0;
  if (nonblank) *nonblank = false;
  for (std::size_t i = 0; i < text.size(); ++count) {
    const auto first = static_cast<unsigned char>(text[i++]);
    std::uint32_t point = first;
    unsigned continuation = 0;
    std::uint32_t minimum = 0;
    if (first >= 0x80) {
      if (first >= 0xc2 && first <= 0xdf) { continuation = 1; point &= 0x1f; minimum = 0x80; }
      else if (first >= 0xe0 && first <= 0xef) { continuation = 2; point &= 0x0f; minimum = 0x800; }
      else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; point &= 0x07; minimum = 0x10000; }
      else require(false, "invalid UTF-8 leading byte");
      for (unsigned n = 0; n < continuation; ++n) {
        require(i < text.size(), "truncated UTF-8 sequence");
        const auto next = static_cast<unsigned char>(text[i++]);
        require((next & 0xc0) == 0x80, "invalid UTF-8 continuation byte");
        point = (point << 6) | (next & 0x3f);
      }
      require(point >= minimum && point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff),
              "invalid Unicode scalar value");
    }
    require(point >= 0x20 && !(point >= 0x7f && point <= 0x9f), "control characters are prohibited");
    // Unicode White_Space plus the BOM/zero-width no-break space, matching the UI.
    const bool blank = point == 0x20 || point == 0xa0 || point == 0x1680 ||
      (point >= 0x2000 && point <= 0x200a) || point == 0x2028 || point == 0x2029 ||
      point == 0x202f || point == 0x205f || point == 0x3000 || point == 0xfeff;
    if (nonblank && !blank) *nonblank = true;
  }
  return count;
}

std::vector<std::string> selector_parts(const std::string& selector, Scope scope) {
  require(!selector.empty() && text_length(selector) <= 4096, "selector requires 1 to 4096 characters");
  auto parts = split(selector, ',');
  require(parts.size() <= 256, "selector allows at most 256 items");
  for (const auto& part : parts) {
    require(text_length(part) <= 255, "selector item allows at most 255 characters");
    if (scope == Scope::group) {
      const auto start = part.front() == '!' ? 1U : 0U;
      require(start < part.size(), "bare group exclusion is prohibited");
      require(part.find('?', start) == std::string::npos && part.find('!', start) == std::string::npos,
              "only * and a leading ! are supported in group patterns");
    }
  }
  return parts;
}

std::vector<Login> account_selector(const std::string& selector, const std::string& platform) {
  std::vector<Login> result;
  std::set<Login> unique;
  for (const auto& part : selector_parts(selector, Scope::account)) {
    const auto login = number(part);
    require(login != 0, "account login must be positive");
    require(platform != "mt4" || login <= INT_MAX, "MT4 account login exceeds INT_MAX");
    require(unique.insert(login).second, "duplicate account selector login");
    result.push_back(login);
  }
  return result;
}

bool wildcard_matches(const std::string& pattern, const std::string& value) {
  // Byte matching is equivalent for valid UTF-8 when * is the only wildcard.
  std::size_t p = 0, v = 0, star = std::string::npos, retry = 0;
  while (v < value.size()) {
    if (p < pattern.size() && pattern[p] != '*' && pattern[p] == value[v]) { ++p; ++v; }
    else if (p < pattern.size() && pattern[p] == '*') { star = p++; retry = v; }
    else if (star != std::string::npos) { p = star + 1; v = ++retry; }
    else return false;
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

void validate_tiers(const std::vector<Tier>& tiers) {
  require(!tiers.empty() && tiers.size() <= 32, "require 1 to 32 leverage tiers");
  require(tiers.front().above == 0, "first leverage tier must start at zero lots");
  for (std::size_t i = 0; i < tiers.size(); ++i) {
    require(tiers[i].leverage >= 1 && tiers[i].leverage <= 100000,
            "leverage must be between 1 and 100000");
    if (i) {
      require(tiers[i].above > tiers[i - 1].above, "tier thresholds must strictly increase");
      require(tiers[i].leverage < tiers[i - 1].leverage, "tier leverage must strictly decrease");
    }
  }
}

std::uint32_t tier_target(const std::vector<Tier>& tiers, Volume volume) {
  require(!tiers.empty(), "leverage tiers are empty");
  auto result = tiers.front().leverage;
  for (std::size_t i = 1; i < tiers.size() && volume > tiers[i].above; ++i) result = tiers[i].leverage;
  return result;
}

bool rules_mode(const Config& config) { return !config.platform.empty() || !config.rules.empty(); }

bool same_positions(const Snapshot& left, const Snapshot& right) {
  if (left.positions.size() != right.positions.size()) return false;
  using Key = std::tuple<std::uint64_t, Login, Volume>;
  const auto keys = [](const Snapshot& s) {
    std::vector<Key> result;
    result.reserve(s.positions.size());
    for (const auto& p : s.positions) result.emplace_back(p.ticket, p.login, p.volume);
    std::sort(result.begin(), result.end());
    return result;
  };
  return keys(left) == keys(right);
}

Snapshot read(Port& port, Login login) {
  auto result = port.snapshot(login);
  require(result.login == login, "snapshot belongs to another account");
  total_volume(result); // Validate even when the volume is not used by the caller.
  return result;
}
} // namespace

Values default_values() {
  return {{"Enabled", "0"}, {"Accounts", ""},
          {"Tiers", "0:1000;2:500;5:200"}, {"PollIntervalMs", "1000"}, {"RulesFile", ""}};
}

Volume parse_lots(const std::string& text) {
  const auto value = trim(text);
  const auto point = value.find('.');
  const auto whole = number(value.substr(0, point));
  Volume fraction = 0;
  if (point != std::string::npos) {
    const auto decimals = value.substr(point + 1);
    require(!decimals.empty() && decimals.size() <= 8, "lots require 1 to 8 fractional digits");
    fraction = number(decimals);
    for (std::size_t i = decimals.size(); i < 8; ++i) fraction *= 10;
  }
  require(whole <= (std::numeric_limits<Volume>::max() - fraction) / units_per_lot,
          "lot volume overflow");
  return whole * units_per_lot + fraction;
}

std::string format_lots(Volume volume) {
  auto result = std::to_string(volume / units_per_lot);
  const auto fraction = volume % units_per_lot;
  if (fraction) {
    auto digits = std::to_string(units_per_lot + fraction).substr(1);
    while (digits.back() == '0') digits.pop_back();
    result += "." + digits;
  }
  return result;
}

Config parse_config(const Values& values) {
  auto merged = default_values();
  for (const auto& entry : values) {
    require(merged.count(entry.first) != 0, "unknown dynamic leverage parameter");
    merged[entry.first] = trim(entry.second);
  }
  Config config;
  require(merged.at("Enabled") == "0" || merged.at("Enabled") == "1", "Enabled must be 0 or 1");
  config.enabled = merged.at("Enabled") == "1";
  config.rules_file = merged.at("RulesFile");
  require(config.rules_file.find('\0') == std::string::npos, "RulesFile cannot contain NUL");
  if (!merged.at("Accounts").empty()) {
    for (const auto& account : split(merged.at("Accounts"), ',')) config.accounts.push_back(number(account));
  }
  const auto interval = number(merged.at("PollIntervalMs"));
  require(interval >= 100 && interval <= 60000, "PollIntervalMs must be between 100 and 60000");
  config.poll_interval_ms = static_cast<std::uint32_t>(interval);
  config.tiers.clear();
  for (const auto& tier : split(merged.at("Tiers"), ';')) {
    const auto parts = split(tier, ':');
    require(parts.size() == 2, "Tiers must use lots:leverage pairs separated by semicolons");
    const auto leverage = number(parts[1]);
    require(leverage >= 1 && leverage <= 100000, "leverage must be between 1 and 100000");
    config.tiers.push_back({parse_lots(parts[0]), static_cast<std::uint32_t>(leverage)});
  }
  validate(config);
  return config;
}

void validate(const Config& config) {
  require(config.rules_file.empty() || config.accounts.empty(), "RulesFile cannot be combined with legacy Accounts");
  require(config.poll_interval_ms >= 100 && config.poll_interval_ms <= 60000,
          "PollIntervalMs must be between 100 and 60000");
  if (rules_mode(config)) {
    require(config.platform == "mt4" || config.platform == "mt5", "platform must be mt4 or mt5");
    require(config.accounts.empty(), "rules cannot be combined with legacy Accounts");
    require(config.rules.size() <= 256, "at most 256 rules are supported");
    std::set<std::string> ids;
    bool any_enabled = false;
    for (const auto& rule : config.rules) {
      require(!rule.id.empty() && rule.id.size() <= 64, "rule ID requires 1 to 64 ASCII characters");
      for (const char c : rule.id)
        require((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-', "invalid rule ID");
      require(ids.insert(rule.id).second, "duplicate rule ID");
      bool nonblank = false;
      const auto name_size = text_length(rule.name, &nonblank);
      require(name_size >= 1 && name_size <= 80, "rule name requires 1 to 80 characters");
      require(nonblank, "rule name cannot contain only whitespace");
      require(rule.scope == Scope::group || rule.scope == Scope::account, "invalid rule scope");
      if (rule.scope == Scope::group) (void)selector_parts(rule.selector, rule.scope);
      else (void)account_selector(rule.selector, config.platform);
      validate_tiers(rule.tiers);
      any_enabled = any_enabled || rule.enabled;
    }
    require(!config.enabled || any_enabled, "enabled configuration requires an enabled rule");
    return;
  }
  require(!config.enabled || !config.accounts.empty() || !config.rules_file.empty(), "Enabled requires explicit Accounts or RulesFile");
  require(config.accounts.size() <= 10000, "at most 10000 accounts are supported");
  std::set<Login> unique;
  for (const auto login : config.accounts) {
    require(login != 0, "account login must be positive");
    require(unique.insert(login).second, "duplicate account login");
  }
  validate_tiers(config.tiers);
}

std::uint32_t target_leverage(const Config& config, Volume volume) {
  return tier_target(config.tiers, volume);
}

bool manages(const Config& config, Login login) {
  if (rules_mode(config)) return select_rule(config, login, {}) != nullptr;
  return config.enabled && std::find(config.accounts.begin(), config.accounts.end(), login) != config.accounts.end();
}

bool group_matches(const std::string& mask, const std::string& group) {
  const auto parts = selector_parts(mask, Scope::group);
  text_length(group);
  if (group.empty()) return false;
  bool includes = false, matched = false;
  for (const auto& part : parts) {
    if (part.front() == '!') {
      if (wildcard_matches(part.substr(1), group)) return false;
    } else {
      includes = true;
      matched = matched || wildcard_matches(part, group);
    }
  }
  return matched || !includes;
}

const Rule* select_rule(const Config& config, Login login, const std::string& group) {
  if (!config.enabled || login == 0) return nullptr;
  for (const auto& rule : config.rules) {
    if (!rule.enabled) continue;
    if (rule.scope == Scope::group) {
      // Missing group metadata must never accidentally match a wildcard rule.
      if (!group.empty() && group_matches(rule.selector, group)) return &rule;
    } else {
      const auto accounts = account_selector(rule.selector, config.platform);
      if (std::find(accounts.begin(), accounts.end(), login) != accounts.end()) return &rule;
    }
  }
  return nullptr;
}

bool has_group_rules(const Config& config) {
  return config.enabled && std::any_of(config.rules.begin(), config.rules.end(), [](const Rule& rule) {
    return rule.enabled && rule.scope == Scope::group;
  });
}

RulePreview preview_rules(const Config& config, const std::string& login,
                          const std::string& group, const std::string& lots) {
  validate(config);
  require(rules_mode(config), "preview requires a rules configuration");
  require(!login.empty() && login.size() <= 255, "preview login requires 1 to 255 decimal characters");
  const auto number = account_selector(login, config.platform);
  require(number.size() == 1 && login.find_first_not_of("0123456789") == std::string::npos,
          "preview login must be one positive decimal integer");
  require(text_length(group) <= 255, "preview group allows at most 255 characters");
  require(!lots.empty() && lots.size() <= 40 && lots.find_first_not_of("0123456789.") == std::string::npos,
          "preview lots requires 1 to 40 decimal characters");
  RulePreview result;
  result.volume = parse_lots(lots);
  const auto* rule = select_rule(config, number.front(), group);
  if (rule) {
    result.matched = true;
    result.rule_id = rule->id;
    result.rule_name = rule->name;
    result.leverage = tier_target(rule->tiers, result.volume);
  }
  return result;
}

std::vector<Login> candidate_accounts(const Config& config, const std::vector<Account>& discovered) {
  std::map<Login, std::string> groups;
  for (const auto& account : discovered) {
    require(account.login != 0, "discovered account login must be positive");
    require(config.platform != "mt4" || account.login <= INT_MAX, "discovered MT4 account exceeds INT_MAX");
    text_length(account.group);
    const auto inserted = groups.emplace(account.login, account.group);
    require(inserted.second || inserted.first->second == account.group, "inconsistent groups for discovered account");
  }
  if (!config.enabled) return {};
  std::set<Login> result;
  if (!rules_mode(config)) result.insert(config.accounts.begin(), config.accounts.end());
  else {
    for (const auto& rule : config.rules) {
      if (!rule.enabled || rule.scope != Scope::account) continue;
      const auto accounts = account_selector(rule.selector, config.platform);
      result.insert(accounts.begin(), accounts.end());
    }
    for (const auto& account : groups)
      if (select_rule(config, account.first, account.second)) result.insert(account.first);
  }
  return {result.begin(), result.end()};
}

Volume total_volume(const Snapshot& snapshot) {
  require(snapshot.login != 0 && snapshot.leverage != 0, "invalid account snapshot");
  std::set<std::uint64_t> tickets;
  Volume sum = 0;
  for (const auto& position : snapshot.positions) {
    require(position.login == snapshot.login, "position belongs to another account");
    require(position.ticket != 0 && tickets.insert(position.ticket).second, "zero or duplicate position ticket");
    require(position.volume != 0, "open position has zero volume");
    require(sum <= std::numeric_limits<Volume>::max() - position.volume, "total position volume overflow");
    sum += position.volume;
  }
  return sum;
}

Result reconcile(const Config& config, Login login, Port& port) {
  const bool uses_rules = rules_mode(config);
  if (!config.enabled || login == 0 || (!uses_rules && !manages(config, login))) return {};
  const auto before = read(port, login);
  const auto* rule = uses_rules ? select_rule(config, login, before.group) : nullptr;
  if (uses_rules && rule == nullptr) return {};
  Result result;
  if (rule) result.rule_id = rule->id;
  result.volume = total_volume(before);
  result.previous = before.leverage;
  result.target = tier_target(rule ? rule->tiers : config.tiers, result.volume);
  result.status = Status::unchanged;
  if (result.previous == result.target) return result;

  result.status = Status::retry;
  const auto confirmed = read(port, login);
  if (confirmed.leverage != before.leverage || !same_positions(before, confirmed) ||
      (uses_rules && confirmed.group != before.group)) return result;
  if (uses_rules ? !port.set_leverage_for_group(login, result.previous, result.target, before.group)
                 : !port.set_leverage(login, result.previous, result.target)) return result;

  const auto after = read(port, login);
  if (after.leverage != result.target || !same_positions(confirmed, after) ||
      (uses_rules && after.group != confirmed.group)) return result;
  result.status = Status::updated;
  return result;
}
} // namespace dynamicleverage

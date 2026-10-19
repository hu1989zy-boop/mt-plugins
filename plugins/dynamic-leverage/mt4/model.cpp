#include "model.hpp"
#include <climits>
#include <set>
#include <stdexcept>

namespace mt4leverage {
dynamicleverage::Config parse(const dynamicleverage::Values& values) {
  for (const auto& item : values) {
    if (item.first.size() >= 32 || item.second.size() >= 128 ||
        item.first.find('\0') != std::string::npos || item.second.find('\0') != std::string::npos)
      throw std::runtime_error("MT4 parameter exceeds PluginCfg field capacity");
  }
  auto config = dynamicleverage::parse_config(values);
  for (const auto login : config.accounts) native_login(login);
  for (const auto& tier : config.tiers)
    if (tier.leverage > INT_MAX) throw std::runtime_error("MT4 leverage exceeds signed integer range");
  return config;
}

int native_login(dynamicleverage::Login login) {
  if (!login || login > INT_MAX) throw std::runtime_error("MT4 login must be in [1, INT_MAX]");
  return static_cast<int>(login);
}

dynamicleverage::Volume volume_units(int native_volume) {
  if (native_volume <= 0) throw std::runtime_error("MT4 open position volume must be positive");
  static_assert(dynamicleverage::units_per_lot % 100 == 0);
  return static_cast<dynamicleverage::Volume>(native_volume) * (dynamicleverage::units_per_lot / 100);
}

bool unchanged_user(int leverage, const std::string& group, std::uint32_t expected,
                    const std::string& expected_group) {
  return leverage > 0 && static_cast<std::uint32_t>(leverage) == expected && group == expected_group;
}

dynamicleverage::Snapshot make_snapshot(int login, int leverage, const std::vector<Order>& orders,
                                       const std::string& group) {
  native_login(static_cast<dynamicleverage::Login>(login));
  if (leverage <= 0) throw std::runtime_error("MT4 account has invalid leverage");
  dynamicleverage::Snapshot result{static_cast<dynamicleverage::Login>(login),
    static_cast<std::uint32_t>(leverage), {}, group};
  std::set<int> tickets;
  for (const auto& order : orders) {
    if (order.login != login || order.ticket <= 0 || !tickets.insert(order.ticket).second)
      throw std::runtime_error("MT4 order snapshot account/ticket mismatch");
    if (order.command < 0 || order.command > 7 || order.state < 0 || order.state > 6 || order.closed < 0)
      throw std::runtime_error("MT4 order snapshot contains invalid metadata");
    // OP_BUY/OP_SELL only. Include open normal, partial remainder, restored;
    // pending orders, balance/credit and all historical/deleted records are excluded.
    if (order.command > 1 || order.closed != 0 || order.state > 2) continue;
    result.positions.push_back({static_cast<std::uint64_t>(order.ticket), result.login, volume_units(order.volume)});
  }
  return result;
}
} // namespace mt4leverage

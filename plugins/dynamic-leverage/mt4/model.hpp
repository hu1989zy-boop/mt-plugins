#pragma once

#include "dynamicleverage/core.hpp"
#include <cstdint>
#include <vector>

namespace mt4leverage {
// SDK-neutral projection; native TradeRecord volumes are hundredths of a lot.
struct Order {
  int ticket = 0;
  int login = 0;
  int command = 0;
  int volume = 0;
  std::int64_t closed = 0;
  int state = 0;
};

dynamicleverage::Config parse(const dynamicleverage::Values& values);
int native_login(dynamicleverage::Login login);
dynamicleverage::Volume volume_units(int native_volume);
dynamicleverage::Snapshot make_snapshot(int login, int leverage, const std::vector<Order>& orders,
                                       const std::string& group = {});
bool unchanged_user(int leverage, const std::string& group, std::uint32_t expected,
                    const std::string& expected_group);
} // namespace mt4leverage

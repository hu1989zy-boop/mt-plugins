#pragma once

#include "model.hpp"
#include "dynamicleverage/opening_policy.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

struct TradeRecord;
struct UserInfo;
struct ConSymbol;

namespace mt4leverage {

// These values are checked against the original SDK in order_margin.cpp.
enum class OrderAddMode { new_trade = 0, close_remainder = 1, restore = 2, api = 3, rollover = 4 };
enum class OrderMarginDisposition { unmanaged, newly_locked, existing_lock };

struct OrderMarginContext {
  dynamicleverage::Account account;
  std::uint32_t fixed_account_leverage = 0;
  std::uint32_t observed_account_leverage = 0;
  // Gross open volume immediately BEFORE this new order. The caller must
  // subtract this order if its native snapshot already includes it.
  dynamicleverage::Volume before_volume = 0;
  OrderAddMode mode = OrderAddMode::new_trade;
};

struct OrderMarginDecision {
  OrderMarginDisposition disposition = OrderMarginDisposition::unmanaged;
  bool changed = false;
  std::uint32_t target_leverage = 0;
  double original_margin_rate = 0;
  double applied_margin_rate = 0;
  // Present only for a new lock. Four native slots cannot also persist the
  // rule's name/ID, so restoring an existing lock never invents that identity.
  dynamicleverage::OpeningQuote opening_quote;
};

// Versioned local metadata: [magic, target leverage, original double bits x2].
// Only claim all-zero slots. This marker is not a corruption checksum or a
// cross-plugin allocation protocol; native persistence must be verified.
constexpr int order_margin_lock_magic = 0x444C4B31; // DLK1

namespace order_margin_detail {
inline void require(bool condition, const char* reason) {
  if (!condition) throw std::invalid_argument(reason);
}
inline double rate(double original, std::uint32_t baseline, std::uint32_t target) {
  require(std::isfinite(original) && original > 0, "invalid original MT4 margin rate");
  require(target >= 1 && target <= 100000, "invalid locked MT4 leverage");
  require(target <= baseline, "MT4 target leverage exceeds fixed account baseline");
  const auto result = original * (static_cast<double>(baseline) / target);
  require(std::isfinite(result) && result > 0, "MT4 margin rate overflow or underflow");
  return result;
}
} // namespace order_margin_detail

// Transform a caller-owned native order record only. No SDK calls, account
// mutation, requests, persistence flush, pre-trade validation or concurrency
// reservation occur here. This is a probe component, not an execution engine.
// The caller must establish that the hook's mutation is persisted BEFORE the
// native margin calculation, and enforce the same margin before execution.
template<class NativeTrade>
OrderMarginDecision transform_order_margin(NativeTrade& trade,
                                            const dynamicleverage::Config& config,
                                            const OrderMarginContext& context) {
  using order_margin_detail::require;
  static_assert(sizeof(int) == 4 && sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                "MT4 metadata requires 32-bit int and IEEE 64-bit double");
  static_assert(std::is_same_v<decltype(trade.api_data), int[4]>, "original MT4 api_data[4] layout required");
  require(config.platform == "mt4", "MT4 order margin requires mt4 platform rules");
  require(context.mode >= OrderAddMode::new_trade && context.mode <= OrderAddMode::rollover,
          "unknown MT4 order add mode");
  require(trade.login == native_login(context.account.login), "MT4 order account identity mismatch");
  require(trade.cmd >= 0 && trade.cmd <= 7, "unknown MT4 order command");
  require(trade.state >= 0 && trade.state <= 6 && trade.close_time >= 0, "invalid MT4 order state");
  OrderMarginDecision result;
  // Pending, history and accounting records never receive a lock.
  if (trade.cmd > 1 || trade.close_time != 0 || trade.state >= 3) return result;

  const bool locked = trade.api_data[0] == order_margin_lock_magic;
  const bool genuinely_new = context.mode == OrderAddMode::new_trade && trade.state == 0;
  // Unmarked remainder/restoration/rollover/API records retain their original
  // native margin. Restoring metadata is separate from applying current rules.
  if (!locked && !genuinely_new) return result;
  require(context.fixed_account_leverage >= 1 && context.fixed_account_leverage <= 100000,
          "invalid fixed MT4 account leverage");
  require(context.observed_account_leverage == context.fixed_account_leverage,
          "MT4 account leverage changed from fixed baseline");
  require(std::isfinite(trade.margin_rate) && trade.margin_rate > 0, "invalid native MT4 margin rate");
  (void)volume_units(trade.volume);

  if (locked) {
    require(trade.api_data[1] >= 1 && trade.api_data[1] <= 100000, "malformed MT4 margin lock target");
    double original = 0;
    std::memcpy(&original, &trade.api_data[2], sizeof(original));
    result.disposition = OrderMarginDisposition::existing_lock;
    result.target_leverage = static_cast<std::uint32_t>(trade.api_data[1]);
    result.original_margin_rate = original;
    result.applied_margin_rate = order_margin_detail::rate(original, context.fixed_account_leverage,
                                                          result.target_leverage);
  } else {
    result.opening_quote = dynamicleverage::quote_opening(config, context.account,
                              context.before_volume, 0, volume_units(trade.volume));
    if (!result.opening_quote.matched) return result;
    for (const auto slot : trade.api_data)
      require(slot == 0, "MT4 api_data slots belong to another producer");
    result.disposition = OrderMarginDisposition::newly_locked;
    result.target_leverage = result.opening_quote.leverage;
    result.original_margin_rate = trade.margin_rate;
    result.applied_margin_rate = order_margin_detail::rate(result.original_margin_rate,
                                      context.fixed_account_leverage, result.target_leverage);
  }

  // All throwing validation/calculation completes before either field changes.
  // Store the original conversion factor, never the already-multiplied factor.
  result.changed = !locked || trade.margin_rate != result.applied_margin_rate;
  if (!locked) {
    trade.api_data[0] = order_margin_lock_magic;
    trade.api_data[1] = static_cast<int>(result.target_leverage);
    std::memcpy(&trade.api_data[2], &result.original_margin_rate, sizeof(double));
  }
  trade.margin_rate = result.applied_margin_rate;
  return result;
}

// Built against the actual Server API 5 when the Windows DLL target is enabled.
// This wrapper is deliberately not registered by plugin.cpp. The caller owns
// hook ordering and supplies the consistent BEFORE snapshot. It validates the
// supplied SDK identities and supported margin mode, then transforms only trade.
OrderMarginDecision probe_sdk_order_margin(TradeRecord& trade,
                                           const UserInfo& user, const ConSymbol& symbol,
                                           const dynamicleverage::Config& config,
                                           const OrderMarginContext& context);

} // namespace mt4leverage

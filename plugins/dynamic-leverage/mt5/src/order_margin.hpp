#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace dynamicleverage::mt5 {

// Evidence from a Demo server, not permission to apply the candidate to a
// live order. Hooks, persistence, EOD and gateway fills need separate validation.
enum class OrderMarginProbeStatus { unsupported, sdk_error, sdk_exception, validated };

struct OrderMarginProbeScope {
  bool new_unhedged_position = false;
  bool leverage_sensitive_formula = false;
  bool fixed_account_leverage = false;
  bool no_floating_margin = false;
  bool rate_recalculation_disabled = false;
  bool original_rate_unmodified = false;
};

struct OrderMarginProbeRelease {
  template<class T> void operator()(T* value) const { if (value) value->Release(); }
};
template<class T> using OrderMarginProbeOwner = std::unique_ptr<T, OrderMarginProbeRelease>;

struct OrderMarginSnapshot {
  double margin = std::numeric_limits<double>::quiet_NaN();
  double free_margin = std::numeric_limits<double>::quiet_NaN();
  std::uint64_t login = 0;
  std::uint32_t currency_digits = 0;
};

template<class Order> struct OrderMarginProbe {
  OrderMarginProbeStatus status = OrderMarginProbeStatus::unsupported;
  std::string reason;
  std::optional<std::uint32_t> assign_code, rate_code, baseline_code, adjusted_code;
  OrderMarginSnapshot baseline_current, baseline_future, adjusted_current, adjusted_future;
  double original_rate = std::numeric_limits<double>::quiet_NaN();
  double candidate_rate = std::numeric_limits<double>::quiet_NaN();
  double multiplier = std::numeric_limits<double>::quiet_NaN();
  double expected_margin = std::numeric_limits<double>::quiet_NaN();
  double tolerance = std::numeric_limits<double>::quiet_NaN();
  bool scope_confirmed = false;
  OrderMarginProbeOwner<Order> candidate;
};

template<class Account> OrderMarginSnapshot order_margin_snapshot(const Account& value) {
  return {value.Margin(), value.MarginFree(), value.Login(), value.CurrencyDigits()};
}

// Only the SDK-shaped factory/Assign/RateMargin/TradeMarginCheck APIs are used.
// No PositionUpdate, OrderUpdate or UserUpdate is performed. The const original
// is never changed, including failures, exceptions and successful validation.
template<class Api, class Order>
auto prepare_order_margin_probe(Api& api, const Order& original,
                                std::uint32_t fixed_account_leverage,
                                std::uint32_t target_leverage,
                                const OrderMarginProbeScope& scope) {
  using NativeOrder = std::remove_pointer_t<decltype(api.OrderCreate())>;
  using Account = std::remove_pointer_t<decltype(api.UserCreateAccount())>;
  OrderMarginProbe<NativeOrder> result;
  // An unconfirmed scope may still collect read-only SDK evidence. It can
  // never receive a validated candidate, even when the arithmetic matches.
  result.scope_confirmed = scope.new_unhedged_position && scope.leverage_sensitive_formula &&
    scope.fixed_account_leverage && scope.no_floating_margin &&
    scope.rate_recalculation_disabled && scope.original_rate_unmodified;
  if (!target_leverage || target_leverage >= fixed_account_leverage) {
    result.reason = "probe requires a target leverage strictly below the fixed account leverage";
    return result;
  }
  try {
    result.original_rate = original.RateMargin();
    result.multiplier = static_cast<double>(fixed_account_leverage) / target_leverage;
    result.candidate_rate = result.original_rate * result.multiplier;
    if (!original.Login() || original.Type() > 1 || !original.VolumeCurrentExt() ||
        !std::isfinite(result.original_rate) || result.original_rate <= 0 ||
        !std::isfinite(result.candidate_rate) || result.candidate_rate <= result.original_rate) {
      result.reason = "invalid original market order or unrepresentable margin rate";
      return result;
    }
    OrderMarginProbeOwner<NativeOrder> candidate(api.OrderCreate());
    OrderMarginProbeOwner<Account> baseline_future(api.UserCreateAccount());
    OrderMarginProbeOwner<Account> baseline_current(api.UserCreateAccount());
    OrderMarginProbeOwner<Account> adjusted_future(api.UserCreateAccount());
    OrderMarginProbeOwner<Account> adjusted_current(api.UserCreateAccount());
    if (!candidate || !baseline_future || !baseline_current || !adjusted_future || !adjusted_current) {
      result.status = OrderMarginProbeStatus::sdk_error;
      result.reason = "SDK allocation failed";
      return result;
    }
    result.assign_code = candidate->Assign(&original);
    if (*result.assign_code != 0) {
      result.status = OrderMarginProbeStatus::sdk_error;
      result.reason = "Order.Assign failed";
      return result;
    }
    result.baseline_code = api.TradeMarginCheck(&original, baseline_future.get(), baseline_current.get());
    result.baseline_future = order_margin_snapshot(*baseline_future);
    result.baseline_current = order_margin_snapshot(*baseline_current);
    if (*result.baseline_code != 0) {
      result.status = OrderMarginProbeStatus::sdk_error;
      result.reason = "original TradeMarginCheck did not confirm sufficient margin";
      return result;
    }
    result.rate_code = candidate->RateMargin(result.candidate_rate);
    if (*result.rate_code != 0) {
      result.status = OrderMarginProbeStatus::sdk_error;
      result.reason = "candidate RateMargin setter failed";
      return result;
    }
    if (candidate->RateMargin() != result.candidate_rate) {
      result.reason = "candidate RateMargin setter did not retain the requested rate";
      return result;
    }
    result.adjusted_code = api.TradeMarginCheck(candidate.get(), adjusted_future.get(), adjusted_current.get());
    result.adjusted_future = order_margin_snapshot(*adjusted_future);
    result.adjusted_current = order_margin_snapshot(*adjusted_current);
    if (*result.adjusted_code != 0) {
      result.status = OrderMarginProbeStatus::sdk_error;
      result.reason = "adjusted TradeMarginCheck did not confirm sufficient margin";
      return result;
    }
    if (!result.scope_confirmed) {
      result.reason = "read-only evidence collected; additive new position and stable rate scope are unconfirmed";
      return result;
    }
    const auto digits = result.baseline_current.currency_digits;
    for (const auto* value : {&result.baseline_current, &result.baseline_future,
                              &result.adjusted_current, &result.adjusted_future}) {
      if (value->login != original.Login() || value->currency_digits != digits || digits > 8 ||
          !std::isfinite(value->margin) || value->margin < 0 ||
          !std::isfinite(value->free_margin) || value->free_margin < 0) {
        result.reason = "SDK margin snapshots have invalid identity, precision, values or insufficient funds";
        return result;
      }
    }
    const auto quantum = std::pow(10.0, -static_cast<int>(digits));
    const auto current = result.baseline_current.margin;
    const auto delta = result.baseline_future.margin - current;
    result.expected_margin = current + delta * result.multiplier;
    const auto scale = std::max({1.0, result.expected_margin, result.adjusted_future.margin});
    const auto roundoff = 16 * std::numeric_limits<double>::epsilon() * scale;
    // Each rounded delta can differ by one currency unit in its last place.
    result.tolerance = (result.multiplier + 1) * quantum + roundoff;
    if (!std::isfinite(result.expected_margin) || !std::isfinite(result.tolerance) ||
        std::abs(result.adjusted_current.margin - current) > quantum + roundoff ||
        std::abs(result.adjusted_current.free_margin - result.baseline_current.free_margin) > quantum + roundoff) {
      result.reason = "account state changed between the two SDK checks";
      return result;
    }
    const auto expected_increase = delta * (result.multiplier - 1);
    const auto actual_increase = result.adjusted_future.margin - result.baseline_future.margin;
    if (delta <= quantum || expected_increase <= 2 * result.tolerance) {
      result.reason = "margin delta is non-additive or too small to verify at account precision";
      return result;
    }
    if (actual_increase <= result.tolerance ||
        std::abs(result.adjusted_future.margin - result.expected_margin) > result.tolerance) {
      result.reason = "SDK ignored RateMargin or returned a non-additive margin change";
      return result;
    }
    const auto free_decrease = result.baseline_future.free_margin - result.adjusted_future.free_margin;
    if (std::abs(free_decrease - actual_increase) > result.tolerance) {
      result.reason = "free margin change is inconsistent with the candidate margin";
      return result;
    }
    result.status = OrderMarginProbeStatus::validated;
    result.reason = "dual SDK checks confirmed the candidate margin; persistence remains unverified";
    result.candidate = std::move(candidate);
    return result;
  } catch (const std::exception& error) {
    result.status = OrderMarginProbeStatus::sdk_exception;
    result.reason = std::string("SDK probe exception: ") + error.what();
  } catch (...) {
    result.status = OrderMarginProbeStatus::sdk_exception;
    result.reason = "SDK probe raised an unknown exception";
  }
  return result;
}

} // namespace dynamicleverage::mt5

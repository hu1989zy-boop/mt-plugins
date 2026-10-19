#include "sdk_port.hpp"
#include "order_margin.hpp"

namespace mt4leverage {
static_assert(ServerApiVersion == 5);
static_assert(OPEN_NEW == static_cast<int>(OrderAddMode::new_trade));
static_assert(OPEN_CLOSE == static_cast<int>(OrderAddMode::close_remainder));
static_assert(OPEN_RESTORE == static_cast<int>(OrderAddMode::restore));
static_assert(OPEN_API == static_cast<int>(OrderAddMode::api));
static_assert(OPEN_ROLLOVER == static_cast<int>(OrderAddMode::rollover));
static_assert(OP_BUY == 0 && OP_SELL == 1 && TS_OPEN_NORMAL == 0 && TS_DELETED == 6);

OrderMarginDecision probe_sdk_order_margin(TradeRecord& trade,
                                           const UserInfo& user, const ConSymbol& symbol,
                                           const dynamicleverage::Config& config,
                                           const OrderMarginContext& context) {
  using order_margin_detail::require;
  require(user.login == trade.login && user.leverage > 0 &&
          static_cast<std::uint32_t>(user.leverage) == context.observed_account_leverage,
          "MT4 hook user identity or observed leverage mismatch");
  require(sdk_text_utf8(fixed(user.group)) == context.account.group,
          "MT4 hook user group mismatch");
  require(fixed(symbol.symbol) == fixed(trade.symbol), "MT4 hook symbol mismatch");
  require(symbol.margin_mode == MARGIN_CALC_FOREX || symbol.margin_mode == MARGIN_CALC_CFDLEVERAGE,
          "MT4 probe requires a leverage-based margin calculation mode");
  require(symbol.margin_initial == 0 && symbol.margin_maintenance == 0,
          "MT4 probe does not support fixed initial or maintenance margin overrides");
  return transform_order_margin(trade, config, context);
}
} // namespace mt4leverage

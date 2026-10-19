#include "dynamicleverage/opening_policy.hpp"

#include <limits>
#include <stdexcept>

namespace dynamicleverage {

OpeningQuote quote_opening(const Config& config, const Account& account,
                           Volume current_gross_volume, Volume closing_volume,
                           Volume new_opening_volume) {
  if (config.platform != "mt4" && config.platform != "mt5")
    throw std::invalid_argument("opening quote requires an mt4 or mt5 rules configuration");
  if (!new_opening_volume)
    throw std::invalid_argument("opening quote requires positive new opening volume");
  if (closing_volume > current_gross_volume)
    throw std::invalid_argument("closing volume exceeds current gross volume");
  const auto remaining = current_gross_volume - closing_volume;
  if (new_opening_volume > std::numeric_limits<Volume>::max() - remaining)
    throw std::invalid_argument("post-opening gross volume overflow");
  const auto after = remaining + new_opening_volume;

  // Reuse the canonical platform/account/group validation, priority matching,
  // exclusions and strict-greater tier boundaries. There is no legacy fallback.
  const auto preview = preview_rules(config, std::to_string(account.login),
                                     account.group, format_lots(after));
  return {preview.matched, preview.rule_id, preview.rule_name, preview.leverage,
          current_gross_volume, closing_volume, new_opening_volume, after};
}

} // namespace dynamicleverage

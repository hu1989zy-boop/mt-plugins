#pragma once

#include "dynamicleverage/core.hpp"

namespace dynamicleverage {

// An owned, offline quote for the entire newly opened volume. This does not
// reserve account exposure, execute a trade, update account leverage, or persist
// a position's locked leverage. Existing positions are not repriced here.
struct OpeningQuote {
  bool matched = false;
  std::string rule_id;
  std::string rule_name;
  std::uint32_t leverage = 0;
  Volume before_volume = 0;
  Volume closing_volume = 0;
  Volume opening_volume = 0;
  Volume after_volume = 0;
};

// Uses post-trade gross exposure (current - closing + new opening) to choose one
// tier for all new opening volume. For a netting reversal, the caller supplies
// the closing and newly opened parts separately, not the total order volume.
// The caller must provide a consistent account snapshot; this pure calculation
// provides no concurrency control or SDK margin-check/settlement guarantees.
// Requires a platform-specific rules config and positive new opening volume.
// Invalid identities, configuration, over-closing and overflow throw.
OpeningQuote quote_opening(const Config& config, const Account& account,
                           Volume current_gross_volume, Volume closing_volume,
                           Volume new_opening_volume);

} // namespace dynamicleverage

#pragma once

#include "tradecopier/executor.hpp"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tradecopier::execution {

// MT5 SDK 6182: these bits restrict client access. The copier never changes them.
constexpr std::uint64_t kUserEnabled = 0x1;
constexpr std::uint64_t kClientTradingBlocked = 0x4 | 0x8 | 0x200;
constexpr std::uint32_t kClientMarketAction = 3;
constexpr std::uint32_t kDealerExecuteAction = 200;

inline bool ClientTradingBlocked(std::uint64_t rights) noexcept {
    return (rights & kClientTradingBlocked) != 0;
}

inline void CheckAccountAccess(std::uint64_t rights, bool follower, Id dealer, Id login) {
    const std::string account = " login=" + std::to_string(login);
    if (!login || !(rights & kUserEnabled))
        throw std::runtime_error("account unavailable or disabled" + account);
    if (!ClientTradingBlocked(rights)) return;
    if (!follower)
        throw std::runtime_error("source account trading unavailable" + account);
    if (!dealer)
        throw std::runtime_error("readonly follower requires configured DealerLogin" + account);
}

inline std::uint32_t CopyRequestAction(std::uint64_t follower_rights, Id dealer, Id login) {
    CheckAccountAccess(follower_rights, true, dealer, login);
    return ClientTradingBlocked(follower_rights) ? kDealerExecuteAction : kClientMarketAction;
}

inline std::uint64_t CopyRequestFlags(std::uint32_t action, bool close) {
    // TA_FLAG_MARKET (2) is retained only on the existing client request path.
    // SDK documentation says that flag is obsolete; dealer execution supplies
    // PriceOrder explicitly. Never use TA_FLAG_SKIP_MARGIN_CHECK (1024).
    if (action == kClientMarketAction) return 0x2 | (close ? 0x1 : 0x0);
    if (action == kDealerExecuteAction) return close ? 0x1 : 0x0;
    throw std::runtime_error("unsupported copier trade request action=" + std::to_string(action));
}

inline double DealerExecutionPrice(Side side, double bid, double ask,
                                   std::int64_t tick_ms, std::int64_t now_ms) {
    if (side != Side::Buy && side != Side::Sell)
        throw std::runtime_error("invalid dealer request side");
    if (!std::isfinite(bid) || !std::isfinite(ask) || bid <= 0 || ask <= 0 || bid > ask)
        throw std::runtime_error("invalid dealer execution bid/ask quote");
    // Positive ordered timestamps make subtraction safe even near INT64_MAX.
    if (tick_ms <= 0 || tick_ms > now_ms || now_ms - tick_ms > 5000)
        throw std::runtime_error("dealer execution quote missing, future or older than 5000 ms");
    return side == Side::Buy ? ask : bid;
}

// Group-mask syntax follows the supplied SDK's CMTStr::CheckGroupMask:
// case-sensitive literals, '*', comma-separated terms and leading '!'.
// Backslashes are literal path separators; '?' is not a wildcard.
inline bool DealerGroupPatternMatches(std::wstring_view pattern, std::wstring_view group) {
    std::size_t p = 0, g = 0, star = std::wstring_view::npos, retry = 0;
    while (g < group.size()) {
        if (p < pattern.size() && pattern[p] == L'*') {
            star = p++;
            retry = g;
        } else if (p < pattern.size() && pattern[p] == group[g]) {
            ++p;
            ++g;
        } else if (star != std::wstring_view::npos) {
            p = star + 1;
            g = ++retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == L'*') ++p;
    return p == pattern.size();
}

inline bool DealerGroupAllowed(const std::vector<std::wstring>& groups, std::wstring_view target) {
    if (target.empty()) return false;
    for (wchar_t c : target)
        if (c < 32 || c == 127) throw std::runtime_error("invalid follower group path");
    bool included = false;
    for (const auto& entry : groups) {
        for (wchar_t c : entry)
            if (c < 32 || c == 127) throw std::runtime_error("invalid dealer group mask");
        std::wstring_view remaining(entry);
        while (!remaining.empty()) {
            const auto separator = remaining.find(L',');
            auto term = remaining.substr(0, separator);
            remaining = separator == std::wstring_view::npos ? std::wstring_view{} : remaining.substr(separator + 1);
            while (!term.empty() && term.front() == L' ') term.remove_prefix(1);
            while (!term.empty() && term.back() == L' ') term.remove_suffix(1);
            if (term.empty()) continue;
            // The SDK helper uses a 256-wchar buffer. Reject oversize terms
            // instead of letting truncation change an authorization rule.
            if (term.size() > 255) throw std::runtime_error("dealer group mask exceeds 255 characters");
            const bool excluded = term.front() == L'!';
            while (!term.empty() && term.front() == L'!') term.remove_prefix(1);
            if (term.empty()) throw std::runtime_error("dealer group exclusion has no pattern");
            if (DealerGroupPatternMatches(term, target)) {
                // Conservative plugin precheck across rows: never let an
                // allow in an earlier row bypass an explicit exclusion.
                if (excluded) return false;
                included = true;
            }
        }
    }
    return included;
}

inline void RequireDealerGroup(Id requested, Id actual, bool enabled, bool dealing_granted,
                               const std::vector<std::wstring>& groups, std::wstring_view target) {
    const std::string dealer = " DealerLogin=" + std::to_string(requested);
    if (!requested || requested != actual)
        throw std::runtime_error("dealer account identity mismatch" + dealer);
    if (!enabled)
        throw std::runtime_error("dealer account disabled" + dealer);
    if (!dealing_granted)
        throw std::runtime_error("dealer trading permission missing" + dealer);
    // Administrator stores both explicit paths and masks such as '*'.
    if (DealerGroupAllowed(groups, target)) return;
    throw std::runtime_error("follower group unavailable to dealer" + dealer);
}

} // namespace tradecopier::execution

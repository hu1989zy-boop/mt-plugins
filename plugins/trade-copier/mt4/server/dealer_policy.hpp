#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mt4copier {

// MT4 Server API: ConManager.groups[1024], UserRecord.group[16]. Match
// the bounded native byte strings without locale conversion or case folding.
// The supplied DealerHelper CheckGroup example uses comma-separated terms,
// a leading '!' exclusion, '*' wildcards and the FIRST matching term. Spaces
// are literal; '?' is not a wildcard. Validate the entire expression first.
// The legacy CheckTemplate sample sometimes accepts overlapping literals or
// drops a suffix after target exhaustion, and has a recursion limit. Compare
// its bounded behavior with an anchored glob; a disagreement DENIES access,
// including for exclusions. A stricter exclusion alone could expand access.
inline bool DealerGroupPatternMatches(std::string_view pattern, std::string_view group) noexcept {
    std::size_t p=0, g=0, star=std::string_view::npos, retry=0;
    while (g<group.size()) {
        if (p<pattern.size() && pattern[p]=='*') { star=p++; retry=g; }
        else if (p<pattern.size() && pattern[p]==group[g]) { ++p; ++g; }
        else if (star!=std::string_view::npos) { p=star+1; g=++retry; }
        else return false;
    }
    while (p<pattern.size() && pattern[p]=='*') ++p;
    return p==pattern.size();
}

inline bool DealerLegacyGroupPatternMatches(std::string_view pattern, std::string_view group) noexcept {
    std::size_t p=0, g=0;
    while (p<pattern.size() && g<group.size()) {
        if (pattern[p]=='*') {
            // Safe index-based equivalent of the SDK CheckTemplate helper.
            // Preserve its first-substring choice, one-byte overlap and ten
            // calls limit; never mutate input strings or form invalid pointers.
            auto previous=std::string_view::npos;
            for (unsigned depth=0; depth<10; ++depth) {
                while (p<pattern.size() && pattern[p]=='*') ++p;
                if (p==pattern.size()) return true;
                const auto next_star=pattern.find('*',p);
                if (next_star==std::string_view::npos) {
                    const auto literal=pattern.substr(p);
                    return literal.size()<=group.size()-g &&
                        group.substr(group.size()-literal.size())==literal;
                }
                const auto literal=pattern.substr(p,next_star-p);
                const auto found=group.find(literal,g);
                if (found==std::string_view::npos ||
                    (previous!=std::string_view::npos && found<=previous)) return false;
                previous=found;
                g=found+literal.size()-1;
                p=next_star;
            }
            return false;
        }
        if (pattern[p]!=group[g]) return false;
        ++p; ++g;
    }
    // The sample's final condition treats a leading remaining '*' as a match
    // even when it is followed by literals. Preserve that behavior only for
    // detecting ambiguity, never use it alone to authorize a request.
    return g==group.size() && (p==pattern.size() || pattern[p]=='*');
}

inline bool DealerGroupAllowed(std::string_view masks, std::string_view target) {
    if (target.empty() || target.size()>15 || masks.empty() || masks.size()>1023)
        throw std::invalid_argument("invalid MT4 dealer group mask or target size");
    for (const auto text:{masks,target}) for (unsigned char c:text)
        if (c<32 || c==127) throw std::invalid_argument("invalid MT4 dealer group control character");
    // A comma is the list delimiter, and a wildcard cannot identify an actual
    // group. Reject them in target names instead of broadening access.
    if (target.find_first_of(",*!")!=std::string_view::npos)
        throw std::invalid_argument("invalid MT4 target group name");
    std::size_t start=0;
    do {
        const auto end=masks.find(',',start);
        auto term=masks.substr(start,end==std::string_view::npos?end:end-start);
        if (!term.empty() && term.front()=='!') term.remove_prefix(1);
        if (term.empty() || term.find('!')!=std::string_view::npos)
            throw std::invalid_argument("invalid MT4 dealer group term");
        if (end==std::string_view::npos) break;
        start=end+1;
    } while (true);
    start=0;
    do {
        const auto end=masks.find(',',start);
        auto term=masks.substr(start,end==std::string_view::npos?end:end-start);
        const bool excluded=term.front()=='!';
        if (excluded) term.remove_prefix(1);
        const bool anchored=DealerGroupPatternMatches(term,target);
        const bool legacy=DealerLegacyGroupPatternMatches(term,target);
        if (anchored!=legacy) return false;
        if (anchored) return !excluded;
        if (end==std::string_view::npos) break;
        start=end+1;
    } while (true);
    return false;
}

inline void RequireDealerAccount(std::uint32_t requested, std::uint32_t actual,
                                 bool enabled, bool read_only, bool broker,
                                 std::string_view masks, std::string_view target) {
    const auto suffix=" DealerLogin="+std::to_string(requested);
    if (!requested || requested>static_cast<std::uint32_t>(INT32_MAX) || requested!=actual)
        throw std::runtime_error("MT4 dealer identity mismatch"+suffix);
    if (!enabled || read_only)
        throw std::runtime_error("MT4 dealer account unavailable"+suffix);
    if (!broker)
        throw std::runtime_error("MT4 dealer dealing permission missing"+suffix);
    if (!DealerGroupAllowed(masks,target))
        throw std::runtime_error("MT4 follower group unavailable to dealer"+suffix);
}

inline void RequireDealerVolume(bool security_enabled, int minimum_lots,
                                int maximum_lots, int volume) {
    // ConManagerSec values use native integer MT4 volume: 100 == 1 lot.
    // Zero minimum is a literal lower bound; a positive request volume is
    // still required. Do not infer an undocumented zero/unlimited maximum.
    if (!security_enabled)
        throw std::runtime_error("MT4 dealer security group unavailable");
    if (minimum_lots<0 || maximum_lots<=0 || maximum_lots<minimum_lots || volume<=0 ||
        volume<minimum_lots || volume>maximum_lots)
        throw std::runtime_error("MT4 dealer volume outside security limits");
}

} // namespace mt4copier

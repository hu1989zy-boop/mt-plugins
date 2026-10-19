#pragma once
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tradecopier::execution {

// Row numbers belong to one immutable SDK array. Never deduplicate rows: two
// rows with the same key are separate evidence and must both be reconciled.
// A null row taints the whole snapshot, as it did in the full-history scan.
class HistoryIndex {
public:
    using Key = std::uint64_t;
    using Rows = std::vector<std::uint32_t>;
    struct Keys {
        bool present = true;
        std::optional<Key> primary, secondary;
    };

    template<class Reader>
    static HistoryIndex Build(std::uint32_t count, Reader read) {
        HistoryIndex next;
        for (std::uint32_t row = 0; row < count; ++row) {
            const auto keys = read(row);
            if (!keys.present) { next.null_rows_ = true; continue; }
            if (keys.primary) next.primary_[*keys.primary].push_back(row);
            if (keys.secondary) next.secondary_[*keys.secondary].push_back(row);
        }
        return next;
    }

    const Rows& Primary(Key key) const noexcept { return Find(primary_, key); }
    const Rows& Secondary(Key key) const noexcept { return Find(secondary_, key); }
    bool HasNullRows() const noexcept { return null_rows_; }

private:
    using Map = std::unordered_map<Key, Rows>;
    static const Rows& Find(const Map& rows, Key key) noexcept {
        const auto found = rows.find(key);
        static const Rows empty;
        return found == rows.end() ? empty : found->second;
    }
    Map primary_, secondary_;
    bool null_rows_ = false;
};

// Worker-thread cumulative counters. The host may log deltas periodically;
// reading these is not a substitute for observing actual deal timestamps.
struct HistoryMetrics {
    std::uint64_t cycle_count = 0;
    std::uint64_t deal_queries = 0, order_queries = 0, cached_windows = 0;
    std::uint64_t indexed_deal_rows = 0, indexed_order_rows = 0;
    std::uint64_t index_lookups = 0, index_hits = 0, candidate_rows = 0;
    std::uint64_t reconcile_calls = 0, reconcile_us = 0;
};

} // namespace tradecopier::execution

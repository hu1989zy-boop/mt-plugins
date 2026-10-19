#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace mt4copier {
// Worker-only admission retry storage. Callback evidence and close hints run
// once when a callback is consumed; only source-opening lookup may be retried.
// Neither the original queue sequence nor the first-attempt deadline changes.
template<class Event, std::size_t Capacity>
class DeferredAdmissions {
    static_assert(Capacity > 0);
public:
    struct Item {
        Event event{};
        std::uint64_t sequence = 0;
        std::int64_t first_attempt_ms = 0;
    };
    enum class Skip { Expired, BeforeBaseline, CapacityReached };
    static constexpr std::int64_t kLifetimeMs = 30000;
    std::size_t size() const noexcept { return size_; }

    // pop copies one fresh callback and its assigned sequence. observe performs
    // one-shot side effects and returns whether this callback is an opening.
    // admit returns false only for a temporary, incomplete source snapshot.
    // now must be monotonic; a regressed clock fails closed. The initial retry
    // share is at most half the budget, so an unstable head cannot starve fresh
    // callbacks. Spare budget serves untouched old items, never the same item
    // twice in one cycle. All successful admissions belong to one engine batch.
    template<class Pop, class Observe, class Admit, class Skipped, class Now>
    void Process(std::size_t budget, std::uint64_t baseline_sequence,
                 Pop pop, Observe observe, Admit admit, Skipped skipped, Now now) {
        const auto old_count = size_;
        const auto first_retries = std::min(old_count, budget / 2);
        std::size_t used = 0;
        const auto attempt = [&](const Item& item) {
            const auto current = now();
            if (item.sequence <= baseline_sequence) { skipped(item, Skip::BeforeBaseline); return; }
            if (current < item.first_attempt_ms || current - item.first_attempt_ms >= kLifetimeMs) {
                skipped(item, Skip::Expired); return;
            }
            if (admit(item)) return;
            if (size_ == Capacity) { skipped(item, Skip::CapacityReached); return; }
            items_[(head_ + size_) % Capacity] = item;
            ++size_;
        };
        const auto retry = [&] {
            const auto item = items_[head_];
            head_ = (head_ + 1) % Capacity;
            --size_;
            attempt(item);
            ++used;
        };
        for (std::size_t n = 0; n < first_retries; ++n) retry();
        while (used < budget) {
            Item item;
            if (!pop(item.event, item.sequence)) break;
            ++used;
            item.first_attempt_ms = now();
            if (observe(item.event)) attempt(item);
        }
        const auto extra_retries = std::min(old_count - first_retries, budget - used);
        for (std::size_t n = 0; n < extra_retries; ++n) retry();
    }
private:
    std::array<Item, Capacity> items_{};
    std::size_t head_ = 0, size_ = 0;
};
} // namespace mt4copier

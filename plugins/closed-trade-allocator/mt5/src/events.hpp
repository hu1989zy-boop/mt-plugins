#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace closedalloc::mt5 {

enum class EventKind { deal, history, resync };
struct SourceEvent {
  EventKind kind = EventKind::resync;
  std::uint64_t ticket = 0;
  std::uint64_t position = 0;
  bool closing = false;
};

// Multiple callback producers, one worker consumer. A failed push must cause
// the caller to request a resync: full queues and contention never silently
// discard evidence. No producer allocates, waits for a slot, or takes a mutex.
template<std::size_t Capacity>
class SourceEvents {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "event capacity must be a power of two and at least two");
  static_assert(Capacity < (std::uint64_t{1} << 63), "event capacity is too large");
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "callbacks require lock-free 64-bit atomics");
  struct Cell {
    std::atomic<std::uint64_t> sequence{0};
    SourceEvent event{};
  };
  std::array<Cell, Capacity> cells_{};
  alignas(64) std::atomic<std::uint64_t> write_{0};
  alignas(64) std::uint64_t read_ = 0;

public:
  SourceEvents() noexcept {
    for (std::size_t i = 0; i < Capacity; ++i)
      cells_[i].sequence.store(i, std::memory_order_relaxed);
  }
  SourceEvents(const SourceEvents&) = delete;
  SourceEvents& operator=(const SourceEvents&) = delete;

  bool push(SourceEvent event) noexcept {
    auto position = write_.load(std::memory_order_relaxed);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
      auto& cell = cells_[position & (Capacity - 1)];
      const auto sequence = cell.sequence.load(std::memory_order_acquire);
      const auto difference = sequence - position;
      if (difference == 0) {
        if (write_.compare_exchange_strong(position, position + 1,
                                          std::memory_order_relaxed)) {
          cell.event = event;
          cell.sequence.store(position + 1, std::memory_order_release);
          return true;
        }
      } else if ((difference & (std::uint64_t{1} << 63)) != 0) {
        return false;
      } else {
        position = write_.load(std::memory_order_relaxed);
      }
    }
    return false;
  }

  bool pop(SourceEvent& event) noexcept {
    auto& cell = cells_[read_ & (Capacity - 1)];
    if (cell.sequence.load(std::memory_order_acquire) != read_ + 1)
      return false;
    event = cell.event;
    cell.sequence.store(read_ + Capacity, std::memory_order_release);
    ++read_;
    return true;
  }
};

// The sink must remain alive until the SDK has removed its subscription.
// close() rejects new leases; idle() then permits releasing callback-accessed
// resources once existing leases have returned. Reopen only during startup.
class CallbackGate {
  static constexpr std::uint64_t closed_bit = std::uint64_t{1} << 63;
  static constexpr std::uint64_t count_mask = closed_bit - 1;
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "callbacks require lock-free 64-bit atomics");
  std::atomic<std::uint64_t> state_{closed_bit};

public:
  void open() noexcept { state_.fetch_and(count_mask, std::memory_order_release); }
  bool enter() noexcept {
    // One atomic operation orders admission against close(). Rejected entries
    // roll back immediately; there is no CAS retry loop on a server thread.
    const auto previous = state_.fetch_add(1, std::memory_order_acq_rel);
    if ((previous & closed_bit) == 0) return true;
    state_.fetch_sub(1, std::memory_order_release);
    return false;
  }
  void leave() noexcept { state_.fetch_sub(1, std::memory_order_release); }
  void close() noexcept { state_.fetch_or(closed_bit, std::memory_order_acq_rel); }
  bool idle() const noexcept {
    return (state_.load(std::memory_order_acquire) & count_mask) == 0;
  }
};

} // namespace closedalloc::mt5

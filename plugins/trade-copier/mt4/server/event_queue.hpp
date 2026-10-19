#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace tradecopier::execution {
// Bounded MPSC ring, one worker consumer. Producers never wait for a slot or
// allocate. The caller reports overflow; source-history polling remains the
// fallback for close hints and independent mappings continue running.
template<typename T, std::size_t Capacity>
class EventQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity-1)) == 0);
    static_assert(std::is_trivially_copyable_v<T>);
    struct Cell { std::atomic<std::uint64_t> sequence{0}; T data{}; };
    std::array<Cell,Capacity> cells_{};
    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::uint64_t read_=0;
public:
    EventQueue() { for (std::size_t i=0;i<Capacity;++i) cells_[i].sequence.store(i); }
    bool Push(const T& value) noexcept {
        auto pos=write_.load(std::memory_order_relaxed);
        for (;;) {
            auto& cell=cells_[pos & (Capacity-1)];
            const auto seq=cell.sequence.load(std::memory_order_acquire);
            if (seq==pos) {
                if (write_.compare_exchange_weak(pos,pos+1,std::memory_order_relaxed)) {
                    cell.data=value;
                    cell.sequence.store(pos+1,std::memory_order_release);
                    return true;
                }
            } else if (seq<pos) return false;
            else pos=write_.load(std::memory_order_relaxed);
        }
    }
    bool Pop(T& value,std::uint64_t& sequence) noexcept {
        auto& cell=cells_[read_ & (Capacity-1)];
        if (cell.sequence.load(std::memory_order_acquire)!=read_+1) return false;
        value=cell.data; sequence=read_+1;
        cell.sequence.store(read_+Capacity,std::memory_order_release); ++read_;
        return true;
    }
    std::uint64_t Watermark() const noexcept { return write_.load(std::memory_order_acquire); }
};
} // namespace tradecopier::execution

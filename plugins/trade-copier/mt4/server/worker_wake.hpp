#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace tradecopier::execution {
// A callback that arrives during SDK work must also interrupt the next wait.
// The generation and wait predicate use the same mutex, avoiding lost wakeups.
class WorkerWake {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t generation_=0;
public:
    std::uint64_t Snapshot() { std::lock_guard<std::mutex> lock(mutex_); return generation_; }
    void Signal() {
        { std::lock_guard<std::mutex> lock(mutex_); ++generation_; }
        changed_.notify_one();
    }
    bool WaitSince(std::uint64_t observed,std::chrono::milliseconds timeout,const std::atomic<bool>& stopping) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock,timeout,[&] {
            return generation_!=observed || stopping.load(std::memory_order_acquire);
        });
    }
};
} // namespace tradecopier::execution

#pragma once

#include "dynamicleverage/core.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>

namespace dynamicleverage::mt5 {

// Callbacks only coalesce account IDs. All SDK reads and writes run on one worker.
class Events {
public:
  using Clock = std::chrono::steady_clock;
  struct Batch {
    bool stopped = false, full = false, reload = false;
    std::vector<Login> accounts;

    std::vector<Login> targets(const std::vector<Login>& candidates) const {
      if (!full) return accounts;
      auto result = candidates;
      // User/position events may refer to an account created or migrated after
      // the discovery snapshot. A simultaneous full scan must not drop them.
      result.insert(result.end(), accounts.begin(), accounts.end());
      std::sort(result.begin(), result.end());
      result.erase(std::unique(result.begin(), result.end()), result.end());
      return result;
    }
  };
  void reset(std::vector<Login> accounts) {
    std::lock_guard<std::mutex> lock(mutex_);
    watched_ = std::move(accounts);
    std::sort(watched_.begin(), watched_.end());
    pending_.clear(); stopped_ = false; full_ = true; reload_ = true; all_positions_ = false;
  }
  void watch(std::vector<Login> accounts, bool group_rules = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    watched_ = std::move(accounts);
    std::sort(watched_.begin(), watched_.end());
    all_positions_ = group_rules;
    // Preserve callbacks received while the worker was discovering accounts.
    // Reconciliation uses the fresh configuration and discards stale matches.
  }
  void position(Login login) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) return;
    if (!login) full_ = true;
    else if (all_positions_ || std::binary_search(watched_.begin(), watched_.end(), login)) pending_.insert(login);
    else return;
    changed_.notify_one();
  }
  void user(Login login) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) return;
    // A new or migrated user may not yet be in the periodic candidate cache.
    // Reconcile just that account without turning every user edit into a
    // full-server discovery (our own leverage writes also emit user events).
    if (login) pending_.insert(login);
    else { full_ = true; reload_ = true; }
    changed_.notify_one();
  }
  void configuration() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) return;
    reload_ = true; full_ = true;
    changed_.notify_one();
  }
  void stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    changed_.notify_all();
  }
  Batch wait_until(Clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_until(lock, deadline, [&] { return stopped_ || full_ || reload_ || !pending_.empty(); });
    Batch batch{stopped_, full_, reload_, {pending_.begin(), pending_.end()}};
    full_ = false; reload_ = false; pending_.clear();
    return batch;
  }
private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<Login> watched_;
  std::set<Login> pending_;
  bool stopped_ = true, full_ = false, reload_ = false;
  bool all_positions_ = false;
};
} // namespace dynamicleverage::mt5

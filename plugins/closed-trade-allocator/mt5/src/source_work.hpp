#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace closedalloc::mt5 {
// Worker-owned, bounded and coalesced by PositionID. Event hints can advance a
// read but cannot reset an unchanged snapshot's stability window.
class SourceWork {
  struct Item {
    std::int64_t due=0,since=0;
    bool queued=false,observed=false;
    std::string payload;
  };
  std::map<std::uint64_t,Item> items_;
  std::set<std::pair<std::int64_t,std::uint64_t>> due_;
  std::size_t limit_;
public:
  explicit SourceWork(std::size_t limit=100000):limit_(limit) {}
  std::size_t size() const {return items_.size();}
  bool ready(std::int64_t now) const {return !due_.empty()&&due_.begin()->first<=now;}
  void touch(std::uint64_t id,std::int64_t now) {
    if(!id)return;
    auto it=items_.find(id);
    if(it==items_.end()) {
      if(items_.size()>=limit_)throw std::runtime_error("source work limit reached; restart with smaller backlog after review");
      it=items_.emplace(id,Item{}).first;
    }
    auto& item=it->second;
    defer(id,item.queued?std::min(item.due,now):now);
  }
  void defer(std::uint64_t id,std::int64_t when) {
    auto& item=items_.at(id);
    if(item.queued)due_.erase({item.due,id});
    item.due=when;item.queued=true;due_.emplace(when,id);
  }
  std::uint64_t take(std::int64_t now) {
    if(due_.empty()||due_.begin()->first>now)return 0;
    const auto id=due_.begin()->second;due_.erase(due_.begin());items_.at(id).queued=false;return id;
  }
  void erase(std::uint64_t id) {
    const auto it=items_.find(id);if(it==items_.end())return;
    if(it->second.queued)due_.erase({it->second.due,id});
    items_.erase(it);
  }
  // First observation and close-age waiting happen together, not serially.
  // A fresh read with the same payload is still required at/after the deadline.
  bool stable(std::uint64_t id,const std::string& payload,std::int64_t now,
              std::int64_t close_age_ms,std::int64_t settle_ms) {
    auto& item=items_.at(id);
    if(!item.observed||item.payload!=payload||now<item.since) {
      item.payload=payload;item.since=now;item.observed=true;
      defer(id,now+std::max(settle_ms,settle_ms-close_age_ms));return false;
    }
    const auto wait=std::max(settle_ms-(now-item.since),settle_ms-close_age_ms);
    if(wait>0) {defer(id,now+wait);return false;}
    return true;
  }
};
} // namespace closedalloc::mt5

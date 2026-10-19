#include "events.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace closedalloc::mt5;

namespace {
void expect(bool result, const char* message) {
  if (!result) throw std::runtime_error(message);
}

void capacity_and_reuse() {
  SourceEvents<4> queue;
  SourceEvent output;
  expect(!queue.pop(output), "new queue must be empty");
  for (std::uint64_t cycle = 0; cycle < 1000; ++cycle) {
    const auto base = cycle * 6;
    for (std::uint64_t i = 0; i < 4; ++i)
      expect(queue.push({EventKind::deal, base + i, base + i + 100}), "available slot rejected");
    expect(!queue.push({EventKind::resync, 0, 0}), "full queue accepted an extra event");
    for (std::uint64_t i = 0; i < 2; ++i) {
      expect(queue.pop(output), "queued event missing");
      expect(output.ticket == base + i && output.position == base + i + 100,
             "queue changed event payload or order");
    }
    for (std::uint64_t i = 4; i < 6; ++i)
      expect(queue.push({EventKind::history, base + i, base + i + 100}), "consumed slot not reusable");
    for (std::uint64_t i = 2; i < 6; ++i) {
      expect(queue.pop(output), "reused slot missing");
      expect(output.ticket == base + i && output.position == base + i + 100,
             "queue reordered events across ring wrap");
      expect(output.kind == (i < 4 ? EventKind::deal : EventKind::history), "event kind changed");
    }
    expect(!queue.pop(output), "queue retained an already consumed event");
  }
}

void concurrent_producers() {
  constexpr std::size_t producer_count = 4, per_producer = 20000;
  constexpr std::size_t total = producer_count * per_producer;
  SourceEvents<64> queue;
  std::vector<unsigned char> accepted(total), received(total);
  std::atomic<unsigned> finished{0};
  std::atomic<bool> start{false};
  std::vector<std::thread> producers;
  for (std::size_t producer = 0; producer < producer_count; ++producer) {
    producers.emplace_back([&, producer] {
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for (std::size_t i = 0; i < per_producer; ++i) {
        const auto id = producer * per_producer + i;
        accepted[id] = queue.push({EventKind::deal, id, id ^ 0x5a5a5a5aULL}) ? 1 : 0;
      }
      finished.fetch_add(1, std::memory_order_release);
    });
  }
  start.store(true, std::memory_order_release);
  bool valid = true;
  SourceEvent event;
  while (true) {
    if (queue.pop(event)) {
      if (event.ticket >= total || event.kind != EventKind::deal ||
          event.position != (event.ticket ^ 0x5a5a5a5aULL)) {
        valid = false;
      } else if (received[static_cast<std::size_t>(event.ticket)]++ != 0) {
        valid = false;
      }
    } else if (finished.load(std::memory_order_acquire) == producer_count) {
      // All publishers have returned, so an empty head cannot be unpublished.
      if (!queue.pop(event)) break;
      if (event.ticket >= total || event.kind != EventKind::deal ||
          event.position != (event.ticket ^ 0x5a5a5a5aULL)) valid = false;
      else if (received[static_cast<std::size_t>(event.ticket)]++ != 0) valid = false;
    } else {
      std::this_thread::yield();
    }
  }
  for (auto& producer : producers) producer.join();
  expect(valid, "concurrent producers corrupted or duplicated an event");
  std::size_t accepted_count = 0;
  for (std::size_t i = 0; i < total; ++i) {
    expect(accepted[i] == received[i], "accepted event lost or rejected event delivered");
    accepted_count += accepted[i];
  }
  expect(accepted_count >= 64, "concurrent queue made insufficient progress");
  expect(queue.push({EventKind::resync, 123, 456}), "queue unusable after contention");
  expect(queue.pop(event) && event.kind == EventKind::resync && event.ticket == 123,
         "post-contention queue reuse failed");
}

void gate_lifecycle() {
  CallbackGate gate;
  expect(gate.idle() && !gate.enter(), "new gate must be closed and idle");
  gate.open();
  expect(gate.enter() && gate.enter(), "open gate refused callbacks");
  gate.close();
  expect(!gate.idle() && !gate.enter(), "close lost existing leases or admitted a new one");
  gate.leave();
  expect(!gate.idle(), "gate reported idle with an active callback");
  gate.leave();
  expect(gate.idle(), "closed gate did not drain");
  gate.open();
  expect(gate.enter(), "gate could not reopen after draining");
  gate.leave();
  gate.close();
  expect(gate.idle(), "reopened gate count corrupted");
}

void gate_close_race() {
  CallbackGate gate;
  gate.open();
  expect(gate.enter(), "could not retain shutdown lease");
  std::atomic<bool> start{false}, closed{false}, invalid{false};
  std::atomic<unsigned> entries{0};
  std::vector<std::thread> callbacks;
  for (unsigned thread = 0; thread < 4; ++thread) {
    callbacks.emplace_back([&] {
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for (unsigned i = 0; i < 20000; ++i) {
        const bool already_closed = closed.load(std::memory_order_acquire);
        if (gate.enter()) {
          if (already_closed) invalid.store(true, std::memory_order_relaxed);
          entries.fetch_add(1, std::memory_order_relaxed);
          gate.leave();
        }
      }
    });
  }
  start.store(true, std::memory_order_release);
  while (entries.load(std::memory_order_relaxed) < 100) std::this_thread::yield();
  gate.close();
  closed.store(true, std::memory_order_release);
  expect(!gate.idle(), "concurrent close forgot retained shutdown lease");
  for (auto& callback : callbacks) callback.join();
  expect(!invalid.load(std::memory_order_relaxed), "callback admitted after close completed");
  expect(!gate.enter() && !gate.idle(), "closed callback gate lost retained lease");
  gate.leave();
  expect(gate.idle(), "callback count leaked during close race");
}
} // namespace

int main() {
  try {
    capacity_and_reuse();
    concurrent_producers();
    gate_lifecycle();
    gate_close_race();
    std::cout << "MT5 source event queue tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

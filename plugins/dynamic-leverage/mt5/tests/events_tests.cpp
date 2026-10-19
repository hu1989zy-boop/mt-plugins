#include "events.hpp"
#include <future>
#include <iostream>
#include <stdexcept>

using namespace dynamicleverage;
namespace {
void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void tests() {
  mt5::Events events;
  events.reset({1002, 1001});
  auto batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.full && batch.reload && !batch.stopped, "startup requires full scan/config read");
  events.position(1001);
  events.position(1001);
  events.position(1002);
  events.position(9999);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.accounts == std::vector<Login>({1001, 1002}), "events coalesce and filter explicit accounts");
  events.watch({1003});
  events.position(1001);
  events.position(1003);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.accounts == std::vector<Login>({1003}), "hot configuration updates account filtering");
  events.user(9999);
  events.user(9999);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.accounts == std::vector<Login>({9999}) && !batch.full && !batch.reload,
         "new/migrated user reconciles individually without a full-server discovery");
  events.user(9999);
  events.watch({1003}, true);
  events.position(8888);
  events.position(1003);
  events.position(0);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.accounts == std::vector<Login>({1003, 8888, 9999}),
         "config discovery preserves concurrent callbacks and groups admit newly active accounts");
  expect(batch.targets({1003, 1004}) == std::vector<Login>({1003, 1004, 8888, 9999}),
         "full discovery and individual events are merged without duplicate reconciliation");
  events.watch({1003});
  events.position(8888);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.accounts.empty(), "account-only config restores explicit position filtering");
  events.user(0);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.full && batch.reload, "user synchronization immediately refreshes group discovery");
  events.position(0);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.full && !batch.reload, "position sync requests full scan");
  events.configuration();
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.full && batch.reload, "configuration wakes worker");
  const auto before = mt5::Events::Clock::now();
  batch = events.wait_until(before + std::chrono::milliseconds(5));
  expect(!batch.stopped && !batch.full && batch.accounts.empty() && mt5::Events::Clock::now() >= before + std::chrono::milliseconds(5),
         "idle deadline enables periodic fallback");
  auto waiting = std::async(std::launch::async, [&] { return events.wait_until(mt5::Events::Clock::now() + std::chrono::seconds(10)); });
  events.stop();
  expect(waiting.wait_for(std::chrono::seconds(1)) == std::future_status::ready && waiting.get().stopped,
         "stop interrupts waiting worker");
  events.position(1003);
  events.user(9999);
  events.configuration();
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.stopped && batch.accounts.empty() && !batch.reload, "stopped callbacks do not enqueue");
  events.reset({1001});
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(!batch.stopped && batch.full && batch.reload, "restart clears stopped state");
  events.position(8888);
  batch = events.wait_until(mt5::Events::Clock::now());
  expect(batch.accounts.empty(), "restart clears prior group event mode");
}
}
int main() {
  try { tests(); std::cout << "MT5 event tests passed\n"; return 0; }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

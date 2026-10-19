#include "worker.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {
using namespace dynamicleverage;
using namespace std::chrono_literals;
int checks = 0;
void check(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
struct FakePort final : Port {
  std::mutex mutex;
  std::condition_variable changed;
  std::map<Login, Snapshot> users;
  std::map<Login, unsigned> reads;
  std::vector<std::string> logs;
  std::function<void()> before_write;
  mt4leverage::Worker* worker = nullptr;
  unsigned writes = 0, rejected = 0, discoveries = 0;
  Login broken = 0;
  bool discovery_fails = false;

  void account(Login login, const std::string& group, std::uint32_t leverage, const std::string& lots) {
    std::lock_guard<std::mutex> lock(mutex);
    auto& user = users[login];
    user = {login, leverage, {}, group};
    const auto volume = parse_lots(lots);
    if (volume) user.positions.push_back({login, login, volume});
  }
  Snapshot snapshot(Login login) override {
    std::lock_guard<std::mutex> lock(mutex);
    ++reads[login]; changed.notify_all();
    if (login == broken) throw std::runtime_error("simulated unavailable account");
    return users.at(login);
  }
  bool set_leverage(Login login, std::uint32_t expected, std::uint32_t target) override {
    return set_leverage_for_group(login, expected, target, "real");
  }
  bool set_leverage_for_group(Login login, std::uint32_t expected, std::uint32_t target,
                              const std::string& group) override {
    std::function<void()> hook;
    { std::lock_guard<std::mutex> lock(mutex); hook = before_write; }
    if (hook) hook();
    const bool permitted = worker->write_permitted();
    std::lock_guard<std::mutex> lock(mutex);
    if (!permitted || users.at(login).leverage != expected || users.at(login).group != group) {
      ++rejected; changed.notify_all(); return false;
    }
    users.at(login).leverage = target;
    ++writes; changed.notify_all();
    return true;
  }
  std::vector<Account> discover() {
    std::lock_guard<std::mutex> lock(mutex);
    ++discoveries; changed.notify_all();
    if (discovery_fails) throw std::runtime_error("simulated discovery failure");
    std::vector<Account> accounts;
    for (const auto& item : users) accounts.push_back({item.first, item.second.group});
    return accounts;
  }
  void log(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex);
    logs.push_back(message); changed.notify_all();
  }
  template<class Predicate> void wait(Predicate predicate, const char* message) {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, predicate), message);
  }
};
Config rules_config(std::uint32_t interval = 100) {
  Config config; config.enabled = true; config.platform = "mt4"; config.poll_interval_ms = interval;
  Rule rule; rule.id = "all"; rule.name = "all groups"; rule.selector = "*,!excluded";
  config.rules.push_back(rule);
  return config;
}
struct RulesFile {
  std::filesystem::path directory, file;
  RulesFile() {
    static std::atomic<unsigned> sequence{0};
    directory = std::filesystem::temp_directory_path() /
      ("mt4-worker-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
       "-" + std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directory(directory);
    file = directory / "mt4.rules.json";
  }
  ~RulesFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
  void write(const std::string& data) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << data; stream.close();
  }
  void write(const Config& config) { write(rules_json(config)); }
  Config bootstrap() const { Config config; config.enabled = true; config.rules_file = file.u8string(); return config; }
};

void callbacks_and_stop() {
  FakePort port;
  port.account(1, "real", 1000, "3");
  port.account(2, "excluded", 1000, "7");
  port.account(4, "real", 1000, "4"); port.broken = 4;
  auto config = rules_config(60000);
  mt4leverage::Worker worker(config, port, [&] { return port.discover(); }, [&](const auto& s) { port.log(s); });
  port.worker = &worker; worker.start();
  port.wait([&] { return port.writes == 1 && !port.logs.empty() && port.reads[4] > 0; }, "startup handles one failing account independently");
  {
    std::lock_guard<std::mutex> lock(port.mutex);
    check(port.users[1].leverage == 500 && port.users[2].leverage == 1000 && port.reads[2] == 0,
      "group exclusion does not read or update excluded account");
  }
  port.account(3, "real", 1000, "6"); worker.wake(3);
  port.wait([&] { return port.users[3].leverage == 200; }, "new-account callback reconciles before next full discovery");
  {
    std::lock_guard<std::mutex> lock(port.mutex);
    check(port.discoveries == 1, "trade and user events do not enumerate all accounts");
  }
  port.account(1, "real", 500, "0"); worker.wake(1);
  port.wait([&] { return port.users[1].leverage == 1000; }, "close callback restores flat-account leverage");
  const auto start = std::chrono::steady_clock::now(); worker.stop();
  check(std::chrono::steady_clock::now() - start < 1s, "stop interrupts a 60-second polling wait");
  bool rejected = false;
  try { worker.configure(config); } catch (const std::exception&) { rejected = true; }
  check(rejected, "stopped worker rejects configuration changes");
}

void reload_and_recovery() {
  RulesFile file; auto config = rules_config(); file.write(config);
  FakePort port; port.account(1, "real", 1000, "3");
  const auto bootstrap = file.bootstrap();
  mt4leverage::Worker worker(bootstrap, port, [&] { return port.discover(); }, [&](const auto& s) { port.log(s); });
  port.worker = &worker; worker.start();
  port.wait([&] { return port.users[1].leverage == 500; }, "file startup applies tier");
  Rule specific; specific.id = "specific"; specific.name = "account first";
  specific.scope = Scope::account; specific.selector = "1"; specific.tiers = {{0, 300}};
  config.rules.insert(config.rules.begin(), specific); file.write(config);
  port.wait([&] { return port.users[1].leverage == 300; }, "JSON reorder gives first matching account rule priority");
  std::swap(config.rules[0], config.rules[1]); file.write(config);
  port.wait([&] { return port.users[1].leverage == 500; }, "group above account overrides account rule");
  port.account(7, "real", 1000, "6");
  port.wait([&] { return port.users[7].leverage == 200; }, "poll discovers accounts without events");

  file.write("{invalid");
  port.wait([&] {
    for (const auto& s : port.logs) if (s.find("suspended:") != std::string::npos) return true;
    return false;
  }, "invalid JSON suspends worker");
  port.account(1, "real", 500, "0"); worker.wake(1);
  file.write(config);
  port.wait([&] { return port.users[1].leverage == 1000; }, "repaired configuration resumes automatically");
  worker.stop();
}

void configuration_change_during_write(bool file_change) {
  RulesFile file; auto config = rules_config(60000); file.write(config);
  FakePort port; port.account(1, "real", 1000, "3");
  std::mutex gate_mutex; std::condition_variable gate_changed;
  bool entered = false, released = false;
  port.before_write = [&] {
    std::unique_lock<std::mutex> lock(gate_mutex);
    entered = true; gate_changed.notify_all();
    gate_changed.wait(lock, [&] { return released; });
  };
  auto bootstrap = file.bootstrap();
  mt4leverage::Worker worker(bootstrap, port, [&] { return port.discover(); }, [&](const auto& s) { port.log(s); });
  port.worker = &worker; worker.start();
  {
    std::unique_lock<std::mutex> lock(gate_mutex);
    // Always release the gate before throwing, so a failed test cannot hang in join.
    const bool ready = gate_changed.wait_for(lock, 5s, [&] { return entered; });
    if (!ready) { released = true; gate_changed.notify_all(); }
    check(ready, "worker reaches final write gate");
  }
  if (file_change) { config.enabled = false; file.write(config); }
  else { bootstrap.enabled = false; worker.configure(bootstrap); }
  { std::lock_guard<std::mutex> lock(gate_mutex); released = true; gate_changed.notify_all(); }
  port.wait([&] { return port.rejected > 0; }, "in-flight old batch rejected after configuration change");
  worker.stop();
  check(port.writes == 0 && port.users[1].leverage == 1000,
    "no stale write after native disable or file replacement");
}

void discovery_failure_is_not_flat() {
  FakePort port; port.account(1, "real", 200, "0"); port.discovery_fails = true;
  mt4leverage::Worker worker(rules_config(), port, [&] { return port.discover(); }, [&](const auto& s) { port.log(s); });
  port.worker = &worker; worker.start();
  port.wait([&] { return !port.logs.empty(); }, "discovery error is reported");
  {
    std::lock_guard<std::mutex> lock(port.mutex);
    check(port.writes == 0 && port.reads.empty(), "failed discovery cannot produce a flat-account write");
    port.discovery_fails = false;
  }
  port.wait([&] { return port.users[1].leverage == 1000; }, "discovery failure retries and recovers");
  worker.stop();
}

void polling_boundary_keeps_events() {
  FakePort port; port.account(1, "real", 1000, "3"); port.account(3, "real", 1000, "6");
  // Discovery is a complete earlier snapshot taken before account 3 appeared.
  mt4leverage::Worker worker(rules_config(60000), port,
    [] { return std::vector<Account>{{1, "real"}}; }, [&](const auto& s) { port.log(s); });
  port.worker = &worker;
  worker.wake(3);
  worker.start();
  port.wait([&] { return port.users[1].leverage == 500 && port.users[3].leverage == 200; },
    "full scan and simultaneous new-account event are both reconciled");
  worker.stop();
}
} // namespace

int main() {
  try {
    callbacks_and_stop(); reload_and_recovery();
    configuration_change_during_write(false); configuration_change_during_write(true);
    discovery_failure_is_not_flat(); polling_boundary_keeps_events();
    std::cout << "MT4 production worker: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "MT4 worker test failed: " << error.what() << '\n'; return 1;
  }
}

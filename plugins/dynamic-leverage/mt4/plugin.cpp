// Broker-side Server API DLL, running one serialized account reconciliation worker.
#if !defined(_WIN32) || !defined(_MSC_VER) || !defined(_M_IX86)
#error MT4DynamicLeverage requires Windows MSVC Win32.
#endif
#include "sdk_port.hpp"
#include "worker.hpp"
#include "dynamicleverage/execution.hpp"
#include <atomic>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <utility>

namespace {
using namespace mt4leverage;
using dynamicleverage::Config;
using dynamicleverage::Values;

Config utf8_config(Config config) {
  config.rules_file = sdk_text_utf8(config.rules_file);
  return config;
}

class Instance {
public:
  Instance(CServerInterface* api, Config config)
    : api_(api), port_(api, [this] { return worker_.write_permitted(); }),
      worker_(utf8_config(std::move(config)), port_, [this] { return port_.accounts(); },
        [api](const std::string& message) { log(api, message); }) {}
  void start() {
    if (!api_ || api_->Version() != ServerApiVersion) throw std::runtime_error("MT4 Server API version mismatch");
    worker_.start();
  }
  void stop() noexcept { worker_.stop(); }
  void wake(dynamicleverage::Login login) noexcept { worker_.wake(login); }
  void configure(Config config) { worker_.configure(utf8_config(std::move(config))); }
private:
  CServerInterface* api_;
  SdkPort port_;
  Worker worker_;
};

// Trade/user callbacks only load a shared owner and signal it: they never take
// lifecycle or call SDK APIs. Cleanup may safely join while holding lifecycle.
std::mutex lifecycle;
CServerInterface* server = nullptr;
Values values = dynamicleverage::default_values();
std::shared_ptr<Instance> current;

void close() {
  auto previous = std::atomic_exchange(&current, std::shared_ptr<Instance>{});
  if (previous) previous->stop();
}

void wake(int login = 0) noexcept {
  try { const auto active = std::atomic_load(&current); if (active) active->wake(login > 0 ? static_cast<dynamicleverage::Login>(login) : 0); } catch (...) {}
}
} // namespace

void APIENTRY MtSrvAbout(PluginInfo* info) {
  if (!info) return;
  std::memset(info, 0, sizeof(*info));
  std::strcpy(info->name, "MT4 Dynamic Leverage");
  info->version = 200;
  std::strcpy(info->copyright, "Dynamic Leverage project");
}

int APIENTRY MtSrvStartup(CServerInterface* api) {
  try {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (!api || server) return FALSE;
    // This rejects plugin activation, never an incoming customer trade.
    dynamicleverage::require_native_execution();
    auto instance = std::make_shared<Instance>(api, parse(values));
    server = api;
    std::atomic_store(&current, instance);
    try { instance->start(); }
    catch (...) { close(); server = nullptr; throw; }
    log(api, "MT4 Dynamic Leverage started; RulesFile or legacy Accounts and Enabled=1 required for writes");
    return TRUE;
  } catch (const std::exception& error) {
    log(api, error.what());
    return FALSE;
  } catch (...) {
    log(api, "Dynamic Leverage MT4 startup failed");
    return FALSE;
  }
}

void APIENTRY MtSrvCleanup() {
  try { std::lock_guard<std::mutex> lock(lifecycle); close(); server = nullptr; } catch (...) {}
}

int APIENTRY MtSrvPluginCfgSet(const PluginCfg* incoming, const int total) {
  try {
    auto next = dynamicleverage::default_values();
    if (total < 0 || static_cast<std::size_t>(total) > next.size() || (total && !incoming)) return FALSE;
    std::set<std::string> seen;
    for (int i = 0; i < total; ++i) {
      const auto name = fixed(incoming[i].name);
      if (!next.count(name) || !seen.insert(name).second) return FALSE;
      next[name] = fixed(incoming[i].value);
    }
    auto config = parse(next);
    std::lock_guard<std::mutex> lock(lifecycle);
    if (next == values) return TRUE;
    const auto active = std::atomic_load(&current);
    if (active) active->configure(std::move(config));
    values.swap(next);
    return TRUE;
  } catch (...) { return FALSE; }
}

int APIENTRY MtSrvPluginCfgNext(const int index, PluginCfg* value) {
  try {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (!value || index < 0 || static_cast<std::size_t>(index) >= values.size()) return FALSE;
    auto item = values.begin(); std::advance(item, index);
    std::memset(value, 0, sizeof(*value));
    std::memcpy(value->name, item->first.c_str(), item->first.size() + 1);
    std::memcpy(value->value, item->second.c_str(), item->second.size() + 1);
    return TRUE;
  } catch (...) { return FALSE; }
}

int APIENTRY MtSrvPluginCfgTotal() {
  try { std::lock_guard<std::mutex> lock(lifecycle); return static_cast<int>(values.size()); } catch (...) { return 0; }
}

void APIENTRY MtSrvTradesAdd(TradeRecord* trade, const UserInfo*, const ConSymbol*) { wake(trade ? trade->login : 0); }
void APIENTRY MtSrvTradesAddExt(TradeRecord* trade, const UserInfo*, const ConSymbol*, const int) { wake(trade ? trade->login : 0); }
void APIENTRY MtSrvTradesAddRequest(TradeRecord* trade, const RequestInfo*, const UserInfo*, const ConSymbol*, const int) { wake(trade ? trade->login : 0); }
void APIENTRY MtSrvTradesUpdate(TradeRecord* trade, UserInfo*, const int) { wake(trade ? trade->login : 0); }
void APIENTRY MtSrvTradesRestore(const TradeRecord* trade) { wake(trade ? trade->login : 0); }
void APIENTRY MtSrvTradesCloseBy(TradeRecord* trade, TradeRecord*, TradeRecord*, ConSymbol*, UserInfo* user) { wake(user ? user->login : (trade ? trade->login : 0)); }
void APIENTRY MtSrvUserAdd(const UserRecord* user) { wake(user ? user->login : 0); }
void APIENTRY MtSrvUserUpdate(const UserRecord* user) { wake(user ? user->login : 0); }
void APIENTRY MtSrvUserDelete(const UserRecord* user) { wake(user ? user->login : 0); }
void APIENTRY MtSrvUserRestore(const UserRecord* user) { wake(user ? user->login : 0); }

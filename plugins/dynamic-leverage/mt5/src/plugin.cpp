#include <windows.h>
#include <MT5APIServer.h>
#include "adapter.hpp"
#include "discovery.hpp"
#include "events.hpp"
#include "dynamicleverage/execution.hpp"
#include <atomic>
#include <chrono>
#include <iterator>
#include <limits>
#include <map>
#include <new>
#include <thread>

namespace {
using namespace dynamicleverage;
namespace m = dynamicleverage::mt5;
static_assert(MT_RET_OK == 0 && MT_RET_OK_NONE == 1, "Unexpected MT5 success codes");
static_assert(IMTPosition::POSITION_BUY == 0 && IMTPosition::POSITION_SELL == 1,
              "Unexpected MT5 position actions");
static_assert(MTAPI_VOLUME_EXT_DIV == units_per_lot, "Unexpected MT5 VolumeExt precision");
const wchar_t module_anchor = L'\0';
bool pin_module() noexcept {
  HMODULE module = nullptr;
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           &module_anchor, &module) != FALSE;
}

std::string utf8(const wchar_t* value) {
  if (!value || !*value) return {};
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 0) throw std::runtime_error("Invalid UTF-16 parameter");
  std::string result(static_cast<std::size_t>(size), '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr))
    throw std::runtime_error("UTF-8 conversion failed");
  result.pop_back();
  return result;
}
std::wstring wide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) throw std::runtime_error("Invalid UTF-8 text");
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size))
    throw std::runtime_error("UTF-16 conversion failed");
  return result;
}
Values read_values(IMTServerAPI& api) {
  auto plugin = m::own(api.PluginCreate());
  m::check_result(api.PluginCurrent(plugin.get()), "PluginCurrent");
  auto parameter = m::own(api.PluginParamCreate());
  auto values = default_values();
  std::set<std::string> seen;
  for (std::uint32_t i = 0; i < plugin->ParameterTotal(); ++i) {
    m::check_result(plugin->ParameterNext(i, parameter.get()), "ParameterNext");
    const auto name = utf8(parameter->Name());
    const auto found = values.find(name);
    if (found == values.end()) throw std::runtime_error("Unknown parameter: " + name);
    if (!seen.insert(name).second) throw std::runtime_error("Duplicate parameter: " + name);
    found->second = utf8(parameter->Value());
  }
  return values;
}

class Plugin final : public IMTServerPlugin, public IMTPositionSink, public IMTConPluginSink, public IMTUserSink {
public:
  ~Plugin() { Stop(); }
  void Release() noexcept override {
    // The server may still retain a sink pointer after a failed unsubscribe.
    // Keep the stopped object alive so Release/Stop can retry safely.
    if (Stop() != MT_RET_OK) {
      if (!pin_module()) log(MTLogErr, "Dynamic Leverage failed to pin retained DLL module");
      log(MTLogErr, "Dynamic Leverage instance retained; retry Stop/Release; restart server to unload pinned DLL");
      return;
    }
    delete this;
  }

  MTAPIRES Start(IMTServerAPI* api) noexcept override {
    try { return start(api); }
    catch (...) { return MT_RET_ERROR; }
  }
  MTAPIRES Stop() noexcept override {
    try {
      std::lock_guard<std::mutex> lock(lifecycle_);
      return stop_unlocked();
    } catch (...) { return MT_RET_ERROR; }
  }

  void OnPositionAdd(const IMTPosition* position) override { position_event(position); }
  void OnPositionUpdate(const IMTPosition* position) override { position_event(position); }
  void OnPositionDelete(const IMTPosition* position) override { position_event(position); }
  void OnPositionClean(std::uint64_t login) override { account_event(login); }
  void OnPositionSync() override { account_event(0); }
  void OnPluginAdd(const IMTConPlugin*) override { config_event(); }
  void OnPluginUpdate(const IMTConPlugin*) override { config_event(); }
  void OnPluginDelete(const IMTConPlugin*) override { config_event(); }
  void OnPluginSync() override { config_event(); }
  void OnUserAdd(const IMTUser* user) override { user_event(user); }
  void OnUserUpdate(const IMTUser* user) override { user_event(user); }
  void OnUserDelete(const IMTUser* user) override { user_event(user); }
  void OnUserRestore(const IMTUser* user) override { user_event(user); }
  void OnUserClean(std::uint64_t login) override { discovery_event(login); }
  void OnUserSync() override { discovery_event(0); }

private:
  MTAPIRES start(IMTServerAPI* api) {
    std::lock_guard<std::mutex> lock(lifecycle_);
    if (!api || api_) return MT_RET_ERR_PARAMS;
    api_ = api;
    try {
      // This rejects plugin activation, never an incoming customer trade.
      require_native_execution();
      const auto config = parse_config(read_values(*api_));
      events_.reset(config.enabled ? config.accounts : std::vector<Login>{});
      stopping_.store(false);
      revision_.store(0);
      // Subscribe before the startup scan so position/config changes are retained.
      // Mark attempts first: even a failed subscription is rolled back.
      config_subscribed_ = true;
      m::check_result(api_->PluginSubscribe(this), "PluginSubscribe");
      positions_subscribed_ = true;
      m::check_result(api_->PositionSubscribe(this), "PositionSubscribe");
      users_subscribed_ = true;
      m::check_result(api_->UserSubscribe(this), "UserSubscribe");
      worker_ = std::thread([this] { run(); });
      log(MTLogOK, "Dynamic Leverage MT5 started");
      return MT_RET_OK;
    } catch (const std::exception& error) {
      log(MTLogErr, error.what());
    } catch (...) {
      log(MTLogErr, "Dynamic Leverage MT5 startup failed");
    }
    stop_unlocked();
    return MT_RET_ERR_PARAMS;
  }
  void position_event(const IMTPosition* position) noexcept {
    if (stopping_.load()) return;
    try { account_event(position ? position->Login() : 0); }
    catch (...) { account_event(0); }
  }
  void account_event(Login login) noexcept {
    if (stopping_.load()) return;
    try { events_.position(login); }
    catch (...) { /* periodic full scan covers a dropped notification */ }
  }
  void user_event(const IMTUser* user) noexcept {
    if (stopping_.load()) return;
    try { discovery_event(user ? user->Login() : 0); }
    catch (...) { discovery_event(0); }
  }
  void discovery_event(Login login) noexcept {
    if (stopping_.load()) return;
    try { events_.user(login); }
    catch (...) { /* periodic discovery covers dropped/new user notifications */ }
  }
  void config_event() noexcept {
    if (stopping_.load()) return;
    revision_.fetch_add(1); // prevents further writes using the old configuration
    try { events_.configuration(); }
    catch (...) { /* periodic config read covers a dropped notification */ }
  }
  MTAPIRES stop_unlocked() {
    stopping_.store(true);
    events_.stop();
    // Callbacks access only events_/atomics, which remain alive until after
    // unsubscription and the worker's last SDK call have completed.
    if (api_ && positions_subscribed_) {
      try {
        const auto result = api_->PositionUnsubscribe(this);
        if (result == MT_RET_OK || result == MT_RET_ERR_NOTFOUND)
          positions_subscribed_ = false;
        else
          log(MTLogErr, "PositionUnsubscribe returned " + std::to_string(result));
      } catch (...) { log(MTLogErr, "PositionUnsubscribe threw an exception"); }
    }
    if (api_ && users_subscribed_) {
      try {
        const auto result = api_->UserUnsubscribe(this);
        if (result == MT_RET_OK || result == MT_RET_ERR_NOTFOUND)
          users_subscribed_ = false;
        else
          log(MTLogErr, "UserUnsubscribe returned " + std::to_string(result));
      } catch (...) { log(MTLogErr, "UserUnsubscribe threw an exception"); }
    }
    if (api_ && config_subscribed_) {
      try {
        const auto result = api_->PluginUnsubscribe(this);
        if (result == MT_RET_OK || result == MT_RET_ERR_NOTFOUND)
          config_subscribed_ = false;
        else
          log(MTLogErr, "PluginUnsubscribe returned " + std::to_string(result));
      } catch (...) { log(MTLogErr, "PluginUnsubscribe threw an exception"); }
    }
    if (worker_.joinable()) worker_.join();
    if (positions_subscribed_ || config_subscribed_ || users_subscribed_) {
      // A retained sink also needs its vtable/code to remain loaded if the
      // host calls FreeLibrary after Stop fails. PIN lasts until process exit.
      if (!pin_module()) log(MTLogErr, "Dynamic Leverage failed to pin retained DLL module");
      else log(MTLogErr, "Dynamic Leverage DLL pinned after unsubscribe failure; server restart required to unload");
      return MT_RET_ERROR;
    }
    api_ = nullptr;
    return MT_RET_OK;
  }
  void log(std::uint32_t level, const std::string& message) noexcept {
    try { if (api_) api_->LoggerOutString(level, wide(message).c_str()); }
    catch (...) {}
  }
  void run() noexcept {
    try { work(); }
    catch (const std::exception& error) {
      stopping_.store(true);
      log(MTLogErr, std::string("Dynamic Leverage worker stopped: ") + error.what());
    } catch (...) {
      stopping_.store(true);
      log(MTLogErr, "Dynamic Leverage worker stopped unexpectedly");
    }
  }
  std::vector<Account> discover(std::map<Login, std::string>& errors) {
    return m::discover_accounts(*api_, [this] { return stopping_.load(); },
      [this, &errors](Login login, const std::string& error) {
        if (errors[login] != error) {
          errors[login] = error;
          log(MTLogErr, "Dynamic Leverage discovery account=" + std::to_string(login) + ": " + error);
        }
      });
  }
  void work() {
    Config config;
    Values previous_values;
    std::string config_error, file_signature, file_path;
    std::vector<Login> candidates;
    std::map<Login, std::string> account_errors;
    bool valid = false;
    std::uint64_t active_revision = 0;
    auto deadline = m::Events::Clock::now();
    while (!stopping_.load()) {
      auto batch = events_.wait_until(deadline);
      if (batch.stopped || stopping_.load()) break;
      const bool poll = m::Events::Clock::now() >= deadline;
      if (poll || batch.reload) {
        const auto observed_revision = revision_.load();
        try {
          auto values = read_values(*api_);
          const auto sdk = parse_config(values);
          auto next = sdk;
          std::string next_signature;
          if (!sdk.rules_file.empty()) {
            next = load_rules_file(sdk.rules_file, "mt5");
            next_signature = rules_json(next);
            next.enabled = sdk.enabled && next.enabled;
          }
          std::vector<Account> discovered;
          if (next.enabled && has_group_rules(next)) discovered = discover(account_errors);
          auto next_candidates = next.enabled ? candidate_accounts(next, discovered) : std::vector<Login>{};
          if (revision_.load() != observed_revision) {
            events_.configuration();
            continue;
          }
          const bool changed = !valid || values != previous_values || next_signature != file_signature;
          config = std::move(next);
          previous_values = std::move(values);
          file_path = sdk.rules_file;
          file_signature = std::move(next_signature);
          candidates = std::move(next_candidates);
          events_.watch(candidates, config.enabled && has_group_rules(config));
          active_revision = observed_revision;
          valid = true;
          config_error.clear();
          if (changed) {
            account_errors.clear();
            log(MTLogOK, "Dynamic Leverage configuration applied; enabled=" +
                std::to_string(config.enabled) + "; accounts=" + std::to_string(candidates.size()));
          }
          batch.full = batch.full || poll || changed;
        } catch (const std::exception& error) {
          // Invalid or unreadable hot configuration suspends writes until a
          // complete valid read succeeds; never silently keep the previous rules.
          valid = false;
          events_.watch({});
          if (config_error != error.what()) {
            config_error = error.what();
            log(MTLogErr, "Dynamic Leverage suspended: " + config_error);
          }
        } catch (...) {
          valid = false;
          events_.watch({});
          const std::string error = "Unknown configuration read error";
          if (config_error != error) {
            config_error = error;
            log(MTLogErr, "Dynamic Leverage suspended: " + config_error);
          }
        }
        deadline = m::Events::Clock::now() + std::chrono::milliseconds(valid ? config.poll_interval_ms : 1000);
      }
      if (!valid || !config.enabled) continue;
      const auto accounts = batch.targets(candidates);
      m::AccountPort<IMTServerAPI> port(*api_, [this, active_revision, &file_path, &file_signature] {
        if (stopping_.load() || revision_.load() != active_revision) return false;
        try {
          if (!file_path.empty() && rules_json(load_rules_file(file_path, "mt5")) != file_signature)
            return false;
        } catch (...) { return false; }
        return !stopping_.load() && revision_.load() == active_revision;
      });
      for (const auto login : accounts) {
        if (stopping_.load() || revision_.load() != active_revision) break;
        try {
          const auto result = reconcile(config, login, port);
          account_errors.erase(login);
          if (result.status == Status::updated)
            log(MTLogOK, "Dynamic Leverage account=" + std::to_string(login) +
                " rule=" + result.rule_id + " lots=" + format_lots(result.volume) + " leverage=" +
                std::to_string(result.previous) + "->" + std::to_string(result.target));
          // retry (a changing snapshot or concurrent administrator edit) is
          // retried by the next event or periodic scan, without a busy loop.
        } catch (const std::exception& error) {
          if (account_errors[login] != error.what()) {
            account_errors[login] = error.what();
            log(MTLogErr, "Dynamic Leverage account=" + std::to_string(login) + ": " + error.what());
          }
        } catch (...) {
          const std::string error = "Unknown account processing error";
          if (account_errors[login] != error) {
            account_errors[login] = error;
            log(MTLogErr, "Dynamic Leverage account=" + std::to_string(login) + ": " + error);
          }
        }
      }
    }
  }

  IMTServerAPI* api_ = nullptr;
  std::mutex lifecycle_;
  std::thread worker_;
  m::Events events_;
  std::atomic<bool> stopping_{true};
  std::atomic<std::uint64_t> revision_{0};
  bool config_subscribed_ = false, positions_subscribed_ = false, users_subscribed_ = false;
};
} // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info) {
  info = {};
  info.version = 200;
  info.version_api = MTServerAPIVersion;
  wcscpy_s(info.name, L"Dynamic Leverage");
  wcscpy_s(info.copyright, L"Dynamic Leverage");
  wcscpy_s(info.description, L"Account leverage tiers based on gross open-position lots");
  const MTPluginParam defaults[] = {
    {MTPluginParam::TYPE_INT, L"Enabled", L"0"},
    {MTPluginParam::TYPE_STRING, L"Accounts", L""},
    {MTPluginParam::TYPE_STRING, L"RulesFile", L""},
    {MTPluginParam::TYPE_STRING, L"Tiers", L"0:1000;2:500;5:200"},
    {MTPluginParam::TYPE_INT, L"PollIntervalMs", L"1000"}
  };
  std::copy(std::begin(defaults), std::end(defaults), std::begin(info.defaults));
  info.defaults_total = static_cast<std::uint32_t>(std::size(defaults));
  return MT_RET_OK;
}
MTAPIENTRY MTAPIRES MTServerCreate(std::uint32_t version, IMTServerPlugin** plugin) {
  if (!plugin) return MT_RET_ERR_PARAMS;
  *plugin = nullptr;
  if (version != MTServerAPIVersion) return MT_RET_ERR_NOTSUPPORTED;
  try {
    *plugin = new (std::nothrow) Plugin;
    return *plugin ? MT_RET_OK : MT_RET_ERR_MEM;
  } catch (...) { return MT_RET_ERR_MEM; }
}

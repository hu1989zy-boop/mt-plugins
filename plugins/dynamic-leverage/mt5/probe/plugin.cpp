// Diagnostic DLL only. The production executor remains disabled separately.
// This plugin never modifies a request/order/position/account and always lets
// the original trade request continue with MT_RET_OK.
#include <windows.h>
#include <MT5APIServer.h>
#include "settings.hpp"
#include "adapter.hpp"
#include "order_margin.hpp"
#include <atomic>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <new>
#include <sstream>

namespace {
namespace m = dynamicleverage::mt5;
namespace p = dynamicleverage::mt5::probe;
static_assert(MTServerAPIVersion == 6182, "Margin probe requires Server API 6182");
static_assert(IMTOrder::OP_BUY == 0 && IMTOrder::OP_SELL == 1, "Unexpected market order types");
const wchar_t module_anchor = L'\0';

bool pin_module() noexcept {
  HMODULE module = nullptr;
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           &module_anchor, &module) != FALSE;
}

std::wstring wide(const std::string& value) {
  if (value.empty()) return {};
  const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0) return L"Margin probe diagnostic text could not be decoded";
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), count))
    return L"Margin probe diagnostic text could not be decoded";
  return result;
}

p::Settings read_settings(IMTServerAPI& api) {
  auto plugin = m::own(api.PluginCreate());
  m::check_result(api.PluginCurrent(plugin.get()), "PluginCurrent");
  if (plugin->ParameterTotal() > 3) throw std::invalid_argument("Margin probe supports exactly three named parameters");
  auto parameter = m::own(api.PluginParamCreate());
  std::vector<std::pair<std::wstring, std::wstring>> values;
  for (std::uint32_t index = 0; index < plugin->ParameterTotal(); ++index) {
    m::check_result(plugin->ParameterNext(index, parameter.get()), "ParameterNext");
    if (!parameter->Name() || !parameter->Value()) throw std::invalid_argument("Null margin probe parameter");
    values.emplace_back(parameter->Name(), parameter->Value());
  }
  return p::parse_settings(values);
}

const char* status_name(m::OrderMarginProbeStatus value) {
  switch (value) {
    case m::OrderMarginProbeStatus::sdk_error: return "sdk_error";
    case m::OrderMarginProbeStatus::sdk_exception: return "sdk_exception";
    case m::OrderMarginProbeStatus::validated: return "validated_arithmetic_only";
    default: return "unconfirmed_scope";
  }
}

class MarginProbe final : public IMTServerPlugin, public IMTTradeSink {
public:
  ~MarginProbe() { Stop(); }

  void Release() noexcept override {
    if (Stop() != MT_RET_OK) {
      if (!pin_module()) log(MTLogErr, "Unable to pin retained margin probe DLL");
      log(MTLogErr, "Margin probe instance retained after unsubscribe failure; retry Stop/Release or restart server");
      return;
    }
    delete this;
  }

  MTAPIRES Start(IMTServerAPI* api) noexcept override {
    try {
      std::lock_guard<std::mutex> lifecycle(lifecycle_);
      if (!api || api_) return MT_RET_ERR_PARAMS;
      api_ = api;
      try {
        settings_ = read_settings(*api_);
        sampled_ = false;
        if (!settings_.enabled) {
          log(MTLogOK, "Margin probe inactive: Enabled=0; no trade subscription");
          return MT_RET_OK;
        }
        stopping_.store(false);
        subscribed_ = true; // Roll back even if the SDK partially subscribes then fails.
        m::check_result(api_->TradeSubscribe(this), "TradeSubscribe");
        log(MTLogOK, "Read-only margin probe active for one exact account; one eligible opening sample per activation; live trade parameters remain unchanged");
        return MT_RET_OK;
      } catch (const std::exception& error) {
        log(MTLogErr, std::string("Margin probe startup failed: ") + error.what());
      } catch (...) {
        log(MTLogErr, "Margin probe startup failed with an unknown exception");
      }
      stop_unlocked();
      return MT_RET_ERR_PARAMS;
    } catch (...) { return MT_RET_ERROR; }
  }

  MTAPIRES Stop() noexcept override {
    try {
      std::lock_guard<std::mutex> lifecycle(lifecycle_);
      return stop_unlocked();
    } catch (...) { return MT_RET_ERROR; }
  }

  MTAPIRES HookTradeRequestAdd(IMTRequest* request, const IMTConGroup*, const IMTConSymbol*,
                              const IMTPosition* position, const IMTOrder*, IMTOrder* order_new) noexcept override {
    // TradeMarginCheck is diagnostic work inside a trading hook. Restrict it
    // to one sample and skip recursion/contention instead of queuing requests.
    static thread_local bool inside_probe = false;
    if (stopping_.load() || inside_probe) return MT_RET_OK;
    try {
      std::unique_lock<std::mutex> callback(callback_, std::try_to_lock);
      if (!callback.owns_lock() || stopping_.load() || sampled_ || !api_ || !request || !order_new)
        return MT_RET_OK;
      const auto action = request->Action();
      const bool market = action == IMTRequest::TA_INSTANT || action == IMTRequest::TA_MARKET ||
                          action == IMTRequest::TA_EXCHANGE;
      const bool closes = (request->Flags() & IMTRequest::TA_FLAG_CLOSE) != 0 || request->PositionBy() != 0;
      const bool existing = request->Position() != 0 || order_new->PositionID() != 0 ||
                            (position && position->Position() != 0);
      if (!p::eligible_request(settings_, request->Login(), order_new->Login(), market, closes,
                               existing, order_new->Type(), order_new->VolumeCurrentExt())) return MT_RET_OK;
      sampled_ = true;
      struct ReentrancyGuard {
        bool& flag;
        explicit ReentrancyGuard(bool& value) : flag(value) { flag = true; }
        ~ReentrancyGuard() { flag = false; }
      } reentrancy(inside_probe);
      try {
        auto user = m::own(api_->UserCreate());
        m::check_result(api_->UserGet(settings_.account, user.get()), "UserGet probe account");
        if (user->Login() != settings_.account || !user->Leverage())
          throw std::runtime_error("Probe account identity or current leverage is invalid");
        // Both the baseline and adjusted inputs belong to this probe. The
        // actual order_new pointer is never passed into TradeMarginCheck.
        auto baseline = m::own(api_->OrderCreate());
        m::check_result(baseline->Assign(order_new), "Order.Assign baseline copy");
        const auto observed_original_rate = order_new->RateMargin();
        const m::OrderMarginProbeScope unconfirmed_scope{};
        auto result = m::prepare_order_margin_probe(*api_, *baseline, user->Leverage(),
                                                    settings_.target_leverage, unconfirmed_scope);
        const auto code = [](const std::optional<std::uint32_t>& value) {
          return value ? std::to_string(*value) : std::string("not_called");
        };
        std::ostringstream message;
        message << std::setprecision(17) << "MarginProbe sample login=" << settings_.account
                << " request=" << request->ID() << " base_leverage=" << user->Leverage()
                << " target_leverage=" << settings_.target_leverage
                << " original_rate=" << observed_original_rate << " candidate_rate=" << result.candidate_rate
                << " baseline_code=" << code(result.baseline_code)
                << " adjusted_code=" << code(result.adjusted_code)
                << " baseline_current_margin=" << result.baseline_current.margin
                << " baseline_future_margin=" << result.baseline_future.margin
                << " adjusted_current_margin=" << result.adjusted_current.margin
                << " adjusted_future_margin=" << result.adjusted_future.margin
                << " baseline_future_free=" << result.baseline_future.free_margin
                << " adjusted_future_free=" << result.adjusted_future.free_margin
                << " status=" << status_name(result.status) << " reason=" << result.reason
                << "; observation only; live order unchanged; restart probe for another sample";
        log(MTLogOK, message.str());
      } catch (const std::exception& error) {
        log(MTLogErr, std::string("MarginProbe sample failed; live order unchanged; no retry this activation: ") + error.what());
      } catch (...) {
        log(MTLogErr, "MarginProbe sample raised an unknown exception; live order unchanged; no retry this activation");
      }
    } catch (...) { /* Every callback outcome must allow the original trade to continue. */ }
    return MT_RET_OK;
  }

private:
  MTAPIRES stop_unlocked() {
    stopping_.store(true);
    if (api_ && subscribed_) {
      try {
        const auto result = api_->TradeUnsubscribe(this);
        if (result == MT_RET_OK || result == MT_RET_ERR_NOTFOUND) subscribed_ = false;
        else log(MTLogErr, "TradeUnsubscribe returned " + std::to_string(result));
      } catch (...) { log(MTLogErr, "TradeUnsubscribe raised an exception"); }
    }
    // Do not clear api_ until a diagnostic already in progress has finished.
    std::lock_guard<std::mutex> callback(callback_);
    if (subscribed_) {
      if (!pin_module()) log(MTLogErr, "Unable to pin retained margin probe DLL");
      return MT_RET_ERROR;
    }
    api_ = nullptr;
    return MT_RET_OK;
  }

  void log(std::uint32_t level, const std::string& message) noexcept {
    try { if (api_) api_->LoggerOutString(level, wide(message).c_str()); } catch (...) {}
  }

  IMTServerAPI* api_ = nullptr;
  p::Settings settings_;
  std::mutex lifecycle_, callback_;
  std::atomic<bool> stopping_{true};
  bool subscribed_ = false, sampled_ = false;
};
} // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info) {
  info = {};
  info.version = 1;
  info.version_api = MTServerAPIVersion;
  wcscpy_s(info.name, L"Dynamic Leverage Margin Probe");
  wcscpy_s(info.copyright, L"Dynamic Leverage");
  wcscpy_s(info.description, L"Read-only one-account order margin diagnostic; does not apply leverage");
  const MTPluginParam defaults[] = {
    {MTPluginParam::TYPE_INT, L"Enabled", L"0"},
    {MTPluginParam::TYPE_STRING, L"ProbeAccount", L""},
    {MTPluginParam::TYPE_INT, L"TargetLeverage", L"500"}
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
    *plugin = new (std::nothrow) MarginProbe;
    return *plugin ? MT_RET_OK : MT_RET_ERR_MEM;
  } catch (...) { return MT_RET_ERR_MEM; }
}

// Read-only MT5 SDK loading and event diagnostics. Not a trade copier.
// Interface signatures are taken from the supplied Server API 6182 SDK.
#if !defined(_WIN32) || !defined(_MSC_VER) || !defined(_M_X64)
#error TradeCopierSdkProbe requires MSVC targeting Windows x64.
#endif

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

#include "MT5APIServer.h"

static_assert(MTServerAPIVersion == 6182,
              "Review a different SDK before changing the probe API target.");
static_assert(sizeof(void*) == 8, "The probe must be built for x64.");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "The callback lifetime gate must not use an internal lock.");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "The callback log quota must not use an internal lock.");

namespace {

constexpr uint32_t kPluginVersion = 100;
constexpr uint32_t kDefaultMaxEvents = 100;
constexpr uint32_t kMaxEventsLimit = 10000;
constexpr uint64_t kGateClosed = uint64_t{1} << 63;
constexpr uint64_t kGateCountMask = kGateClosed - 1;

template <size_t N, size_t M>
void CopyLiteral(wchar_t (&destination)[N], const wchar_t (&source)[M]) noexcept {
  static_assert(M <= N, "Metadata text exceeds the SDK field size.");
  std::memcpy(destination, source, sizeof(source));
}

template <typename... Args>
void LogNoThrow(IMTServerAPI* api, const uint32_t level,
                const wchar_t* format, Args... args) noexcept {
  if (!api) return;
  try {
    (void)api->LoggerOut(level, format, args...);
  } catch (...) {
    // A logging failure must not escape into the trade server.
  }
}

template <typename T>
struct SdkRelease {
  void operator()(T* object) const noexcept {
    if (!object) return;
    try {
      object->Release();
    } catch (...) {
      // The SDK owns this object. Never try to delete it locally.
    }
  }
};

// Read raw decimal text to reject signs, spaces, truncation and overflow.
bool ParseUnsigned(const wchar_t* text, uint64_t& value) noexcept {
  if (!text || *text == L'\0') return false;
  uint64_t result = 0;
  size_t length = 0;
  for (; *text != L'\0'; ++text) {
    if (++length > 20 || *text < L'0' || *text > L'9') return false;
    const uint64_t digit = static_cast<uint64_t>(*text - L'0');
    if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
    result = result * 10 + digit;
  }
  value = result;
  return true;
}

class TradeCopierSdkProbe final : public IMTServerPlugin, public IMTTradeSink {
 public:
  explicit TradeCopierSdkProbe(const uint32_t runtime_api) noexcept
      : runtime_api_(runtime_api) {}

  void Release() noexcept override {
    try {
      // Normal host lifecycle has already called Stop. This local operation
      // is idempotent and deliberately makes no IMTServerAPI calls.
      std::lock_guard<std::mutex> lock(lifecycle_);
      CloseAndDrain();
    } catch (...) {
      // Do not destroy an object whose local callback drain did not complete.
      return;
    }
    delete this;
  }

  MTAPIRES Start(IMTServerAPI* api) noexcept override {
    try {
      std::lock_guard<std::mutex> lock(lifecycle_);
      if (!api) return MT_RET_ERR_PARAMS;
      if (api_ != nullptr) return MT_RET_ERR_DUPLICATE;

      MTServerInfo info{};
      MTAPIRES result = api->About(info);
      if (result != MT_RET_OK) {
        LogNoThrow(api, MTLogErr,
                   L"TradeCopierSdkProbe: About failed ret=%u; not subscribed", result);
        return result;
      }
      LogNoThrow(api, MTLogOK,
                 L"TradeCopierSdkProbe: READ-ONLY; server-version=%u server-build=%u "
                 L"runtime-server-api=%u compiled-api=%u plugin-version=%u",
                 info.server_version, info.server_build, runtime_api_,
                 static_cast<uint32_t>(MTServerAPIVersion), kPluginVersion);

      uint64_t watch_login = 0;
      uint32_t max_events = kDefaultMaxEvents;
      result = ReadParameters(api, watch_login, max_events);
      if (result != MT_RET_OK) return result;

      // Publish immutable per-Start values before opening the callback gate.
      // Config/parameter SDK objects have already been released.
      api_ = api;
      watch_login_ = watch_login;
      max_events_ = max_events;
      logged_events_.store(0, std::memory_order_relaxed);
      callback_errors_.store(0, std::memory_order_relaxed);

      if (watch_login_ == 0 || max_events_ == 0) {
        LogNoThrow(api, MTLogOK,
                   L"TradeCopierSdkProbe: account observation disabled "
                   L"(WatchLogin=0 or MaxEvents=0); no trade-event subscription");
        return MT_RET_OK;
      }

      LogNoThrow(api, MTLogOK,
                 L"TradeCopierSdkProbe: WatchLogin=%I64u MaxEvents=%u; "
                 L"callback lines can repeat the same deal; parameters reload only after disable/enable",
                 watch_login_, max_events_);

      gate_.store(0, std::memory_order_release);
      try {
        result = api->TradeSubscribe(this);
      } catch (...) {
        // Only Start has a valid API lifetime for a subscription rollback.
        gate_.fetch_or(kGateClosed, std::memory_order_acq_rel);
        try { (void)api->TradeUnsubscribe(this); } catch (...) {}
        CloseAndDrain();
        LogNoThrow(api, MTLogErr,
                   L"TradeCopierSdkProbe: subscription exception; Start failed");
        return MT_RET_ERROR;
      }
      if (result != MT_RET_OK) {
        CloseAndDrain();
        LogNoThrow(api, MTLogErr,
                   L"TradeCopierSdkProbe: TradeSubscribe failed ret=%u", result);
        return result;
      }
      // Documentation says event delivery starts after Start completes.
      // No throwing work is performed after the successful subscription.
      return MT_RET_OK;
    } catch (...) {
      return MT_RET_ERROR;
    }
  }

  MTAPIRES Stop() noexcept override {
    try {
      std::lock_guard<std::mutex> lock(lifecycle_);
      // SDK documentation: the server unsubscribes this instance before Stop;
      // server access is no longer allowed here. No logger or unsubscribe call.
      CloseAndDrain();
      return MT_RET_OK;
    } catch (...) {
      return MT_RET_ERROR;
    }
  }

  void OnTradeRequestProcess(const IMTRequest* request,
                             const IMTConfirm*, const IMTConGroup*,
                             const IMTConSymbol*, const IMTPosition*,
                             const IMTOrder*, const IMTDeal* deal) noexcept override {
    CallbackUse use(*this);
    if (!use) return;
    Observe(L"RequestProcess", request, nullptr, deal);
  }

  void OnTradeExecution(const IMTConGateway*, const IMTExecution* execution,
                        const IMTConGroup*, const IMTConSymbol*,
                        const IMTPosition*, const IMTOrder*,
                        const IMTDeal* deal) noexcept override {
    CallbackUse use(*this);
    if (!use) return;
    Observe(L"GatewayExecution", nullptr, execution, deal);
  }

  void OnTradeRequestProcessCloseBy(const IMTRequest* request,
                                    const IMTConfirm*, const IMTConGroup*,
                                    const IMTConSymbol*, const IMTPosition*,
                                    const IMTOrder*, const IMTDeal* deal,
                                    const IMTDeal* deal_by) noexcept override {
    CallbackUse use(*this);
    if (!use) return;
    Observe(L"RequestCloseBy.primary", request, nullptr, deal);
    Observe(L"RequestCloseBy.opposite", request, nullptr, deal_by);
  }

  void OnTradeExecutionCloseBy(const IMTExecution* execution,
                               const IMTConGroup*, const IMTConSymbol*,
                               const IMTPosition*, const IMTOrder*,
                               const IMTDeal* deal,
                               const IMTDeal* deal_by) noexcept override {
    CallbackUse use(*this);
    if (!use) return;
    // The supplied header exposes this event; its detailed timing and overlap
    // are not described in the supplied CHM. Observe both legs without inference.
    Observe(L"GatewayCloseBy.primary", nullptr, execution, deal);
    Observe(L"GatewayCloseBy.opposite", nullptr, execution, deal_by);
  }

 private:
  ~TradeCopierSdkProbe() noexcept = default;

  class CallbackUse final {
   public:
    explicit CallbackUse(TradeCopierSdkProbe& owner) noexcept : owner_(owner) {
      uint64_t current = owner_.gate_.load(std::memory_order_acquire);
      while ((current & kGateClosed) == 0 && (current & kGateCountMask) != kGateCountMask) {
        if (owner_.gate_.compare_exchange_weak(current, current + 1,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
          active_ = true;
          break;
        }
      }
    }
    ~CallbackUse() noexcept {
      if (active_) owner_.gate_.fetch_sub(1, std::memory_order_release);
    }
    explicit operator bool() const noexcept { return active_; }
    CallbackUse(const CallbackUse&) = delete;
    CallbackUse& operator=(const CallbackUse&) = delete;
   private:
    TradeCopierSdkProbe& owner_;
    bool active_ = false;
  };

  static MTAPIRES ReadParameters(IMTServerAPI* api, uint64_t& watch_login,
                                 uint32_t& max_events) {
    std::unique_ptr<IMTConPlugin, SdkRelease<IMTConPlugin>> config(api->PluginCreate());
    std::unique_ptr<IMTConParam, SdkRelease<IMTConParam>> param(api->PluginParamCreate());
    if (!config || !param) return MT_RET_ERR_MEM;
    MTAPIRES result = api->PluginCurrent(config.get());
    if (result != MT_RET_OK) {
      LogNoThrow(api, MTLogErr,
                 L"TradeCopierSdkProbe: PluginCurrent failed ret=%u", result);
      return result;
    }

    const auto read = [&](const wchar_t* name, const uint32_t expected_type,
                          uint64_t& value) -> MTAPIRES {
      const MTAPIRES read_result = config->ParameterGet(name, param.get());
      if (read_result == MT_RET_ERR_NOTFOUND) return MT_RET_OK; // safe default
      if (read_result != MT_RET_OK) return read_result;
      if (param->Type() != expected_type || !ParseUnsigned(param->Value(), value)) {
        LogNoThrow(api, MTLogErr,
                   L"TradeCopierSdkProbe: invalid type or unsigned-decimal text for %s", name);
        return MT_RET_ERR_PARAMS;
      }
      return MT_RET_OK;
    };

    result = read(L"WatchLogin", IMTConParam::TYPE_STRING, watch_login);
    if (result != MT_RET_OK) return result;
    uint64_t limit = kDefaultMaxEvents;
    result = read(L"MaxEvents", IMTConParam::TYPE_INT, limit);
    if (result != MT_RET_OK) return result;
    if (limit > kMaxEventsLimit) {
      LogNoThrow(api, MTLogErr,
                 L"TradeCopierSdkProbe: MaxEvents must be between 0 and %u", kMaxEventsLimit);
      return MT_RET_ERR_PARAMS;
    }
    max_events = static_cast<uint32_t>(limit);
    return MT_RET_OK;
  }

  uint32_t ReserveLogLine() noexcept {
    uint32_t current = logged_events_.load(std::memory_order_relaxed);
    while (current < max_events_) {
      if (logged_events_.compare_exchange_weak(current, current + 1,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed)) return current + 1;
    }
    return 0;
  }

  void Observe(const wchar_t* origin, const IMTRequest* request,
               const IMTExecution* execution, const IMTDeal* deal) noexcept {
    // The caller holds one gate admission for its entire SDK callback, including
    // both Close By legs. Never release that admission between the two legs.
    try {
      if (!deal || watch_login_ == 0 ||
          logged_events_.load(std::memory_order_relaxed) >= max_events_) return;
      if (deal->Login() != watch_login_) return;
      const uint32_t action = deal->Action();
      if (action != IMTDeal::DEAL_BUY && action != IMTDeal::DEAL_SELL) return;
      const uint32_t ordinal = ReserveLogLine();
      if (ordinal == 0) return;

      // Copy only scalar evidence while borrowed callback objects are valid.
      const uint64_t deal_id = deal->Deal();
      const uint64_t order_id = deal->Order();
      const uint64_t position_id = deal->PositionID();
      const uint64_t volume_ext = deal->VolumeExt();
      const uint32_t entry = deal->Entry();
      const int64_t time_msc = deal->TimeMsc();
      const uint32_t request_id = request ? request->ID() : 0;
      const uint64_t execution_id = execution ? execution->ID() : 0;
      LogNoThrow(api_, MTLogOK,
                 L"TradeCopierSdkProbe: OBSERVE %u/%u source=%s login=%I64u "
                 L"deal=%I64u order=%I64u position=%I64u action=%u entry=%u "
                 L"volume_ext_raw=%I64u time_msc=%I64d request=%u execution=%I64u",
                 ordinal, max_events_, origin, watch_login_, deal_id, order_id,
                 position_id, action, entry, volume_ext, time_msc, request_id, execution_id);
    } catch (...) {
      // No exception is allowed to leave a server callback. No SDK objects or
      // callback string pointers are retained, and no retry is attempted.
      callback_errors_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void CloseAndDrain() noexcept {
    gate_.fetch_or(kGateClosed, std::memory_order_acq_rel);
    // Lifecycle-only wait: callbacks never lock lifecycle_ or wait on this gate.
    // Do not clear api_ or destroy this object while an admitted callback uses it.
    while ((gate_.load(std::memory_order_acquire) & kGateCountMask) != 0) {
      std::this_thread::yield();
    }
    api_ = nullptr;
  }

  const uint32_t runtime_api_;
  std::mutex lifecycle_; // Start/Stop/Release only; never acquired in a callback.
  std::atomic<uint64_t> gate_{kGateClosed};
  std::atomic<uint32_t> logged_events_{0};
  std::atomic<uint64_t> callback_errors_{0};
  IMTServerAPI* api_ = nullptr; // Host-owned, valid only during the running instance.
  uint64_t watch_login_ = 0;    // Immutable for the entire Start/Stop interval.
  uint32_t max_events_ = kDefaultMaxEvents;
};

} // namespace

MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info) {
  try {
    std::memset(&info, 0, sizeof(info));
    info.version = kPluginVersion;
    info.version_api = MTServerAPIVersion;
    CopyLiteral(info.name, L"TradeCopierSdkProbe");
    CopyLiteral(info.copyright, L"Trade Copier project");
    CopyLiteral(info.description,
                L"Read-only SDK loading/event diagnostics. Does not copy or execute trades.");
    info.defaults[0].type = MTPluginParam::TYPE_STRING;
    CopyLiteral(info.defaults[0].name, L"WatchLogin");
    CopyLiteral(info.defaults[0].value, L"0");
    info.defaults[1].type = MTPluginParam::TYPE_INT;
    CopyLiteral(info.defaults[1].name, L"MaxEvents");
    CopyLiteral(info.defaults[1].value, L"100");
    info.defaults_total = 2;
    return MT_RET_OK;
  } catch (...) {
    return MT_RET_ERROR;
  }
}

MTAPIENTRY MTAPIRES MTServerCreate(const uint32_t apiversion, IMTServerPlugin** plugin) {
  if (!plugin) return MT_RET_ERR_PARAMS;
  try {
    *plugin = nullptr;
    // This is a runtime API gate, not a comparison with the server Build label.
    // Accepting a newer API is not proof that its host behavior is compatible.
    if (apiversion < MTServerAPIVersion) return MT_RET_ERR_NOTSUPPORTED;
    auto* instance = new (std::nothrow) TradeCopierSdkProbe(apiversion);
    if (!instance) return MT_RET_ERR_MEM;
    *plugin = instance;
    return MT_RET_OK;
  } catch (...) {
    return MT_RET_ERROR;
  }
}

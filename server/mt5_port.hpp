#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include "MT5APIServer.h"
#include "tradecopier/executor.hpp"
#include "request_refusals.hpp"

namespace tradecopier::execution {
constexpr std::uint64_t kCopierMarker = 0x5443583631383201ULL;
struct Stopping final : std::exception {};

// Close prevents NEW API calls immediately. Calls admitted before Stop may
// finish; Stop joins the worker before releasing this gate or the plugin.
class LifetimeGate {
    static constexpr std::uint64_t closed=std::uint64_t{1}<<63;
    std::atomic<std::uint64_t> value_{closed};
public:
    void Open() { value_.store(0,std::memory_order_release); }
    bool Enter() noexcept {
        auto n=value_.load(std::memory_order_acquire);
        while (!(n&closed)) {
            if (value_.compare_exchange_weak(n,n+1,std::memory_order_acq_rel)) return true;
        }
        return false;
    }
    void Leave() noexcept { value_.fetch_sub(1,std::memory_order_release); }
    void Close() noexcept { value_.fetch_or(closed,std::memory_order_acq_rel); }
    void Drain() noexcept {
        while (value_.load(std::memory_order_acquire)&(closed-1)) std::this_thread::yield();
    }
};
struct GateUse {
    LifetimeGate& gate;
    explicit GateUse(LifetimeGate& g):gate(g) { if (!gate.Enter()) throw Stopping{}; }
    ~GateUse() { gate.Leave(); }
};
template<typename T> struct ReleaseSdk {
    void operator()(T* p) const noexcept { if (p) { try { p->Release(); } catch (...) {} } }
};
template<typename T> using SdkPtr=std::unique_ptr<T,ReleaseSdk<T>>;
inline void Check(MTAPIRES result,const char* operation) {
    if (result!=MT_RET_OK) throw std::runtime_error(std::string(operation)+" ret="+std::to_string(result));
}
template<typename T> SdkPtr<T> Own(T* p) {
    if (!p) throw std::runtime_error("SDK allocation failed");
    return SdkPtr<T>(p);
}
std::string Utf8(const wchar_t*);
std::wstring Wide(const std::string&);

template<typename T> std::uint64_t ApiValue(const T* p,std::uint16_t app,std::uint8_t id) {
    std::uint64_t value=0;
    const auto result=p->ApiDataGet(app,id,value);
    if (result==MT_RET_OK_NONE || result==MT_RET_ERR_NOTFOUND) return 0;
    Check(result,"ApiDataGet");
    return value;
}

class Mt5Port final : public Port {
public:
    Mt5Port(IMTServerAPI* api,LifetimeGate& gate,std::atomic<bool>& callback_fault,
            Id server,std::uint16_t app)
        :api_(api),gate_(gate),callback_fault_(callback_fault),server_(server),app_(app) {}
    template<typename F> auto Call(F f) { GateUse use(gate_); return f(api_); }
    BootSnapshot Baseline(const CopierConfig&);
    std::int64_t NowMs() override;
    SourceView ReadSource(const Deal&) override;
    void BeginCycle() override { symbol_names_.clear(); candidates_.clear(); names_loaded_=false; deal_windows_.clear(); order_windows_.clear(); }
    SymbolResolution ResolveSymbol(const RuleConfig&,const std::string&,Side) override;
    VolumeLimits Limits(const RuleConfig&,const std::string&,const std::string&,Side,bool) override;
    EquitySnapshot Equities(const RuleConfig&) override;
    Units FollowerRemaining(Id,Id,const std::string&,Side) override;
    Submission Submit(const Intent&) override;
    Result Reconcile(const Intent&) override;
    bool RecordRefusal(const RequestRefusal& event) noexcept { return refusals_.Push(event); }
    void ForgetRequest(Id id) noexcept override { refusals_.Forget(id); }
    void Log(const std::string&) override;
private:
    SdkPtr<IMTConGroup> AccountGroup(Id);
    SdkPtr<IMTConSymbol> Symbol(Id,const std::string&);
    IMTDealArray* Deals(Id,std::int64_t);
    IMTOrderArray* Orders(Id,std::int64_t);
    template<typename T> struct HistoryWindow { std::int64_t from=0; SdkPtr<T> rows; };
    std::map<Id,HistoryWindow<IMTDealArray>> deal_windows_;
    std::map<Id,HistoryWindow<IMTOrderArray>> order_windows_;
    Units Position(Id,Id,const std::string&,Side);
    IMTServerAPI* api_;
    LifetimeGate& gate_;
    std::atomic<bool>& callback_fault_;
    Id server_;
    std::uint16_t app_;
    RequestRefusals refusals_;
    std::vector<std::string> symbol_names_;
    bool names_loaded_=false;
    std::map<std::pair<std::wstring,std::string>,std::vector<SymbolSpec>> candidates_;
};
} // namespace tradecopier::execution

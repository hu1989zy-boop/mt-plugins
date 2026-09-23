#pragma once
#include "event_queue.hpp"
#include "tradecopier/executor.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace tradecopier::execution {
// Numeric constants are asserted against Server API 6182 in the adapter.
constexpr std::uint32_t kRequestNoMoney=10019;
struct RequestRefusal {
    Id intent=0,login=0,position=0,result_order=0,result_deal=0;
    Units requested=0,result_volume=0;
    std::uint32_t request=0,action=0,type=0,retcode=0;
    std::uint64_t flags=0;
    std::array<wchar_t,128> symbol{};
    bool operator==(const RequestRefusal& b) const {
        return intent==b.intent && login==b.login && position==b.position && result_order==b.result_order &&
            result_deal==b.result_deal && requested==b.requested && result_volume==b.result_volume &&
            request==b.request && action==b.action && type==b.type && retcode==b.retcode && (flags&3)==(b.flags&3) && symbol==b.symbol;
    }
};

// Callback producers copy borrowed fields only. Registration, validation and
// reclamation happen on the single SDK worker. No unbounded callback history.
class RequestRefusals {
    struct Entry { std::optional<RequestRefusal> evidence; bool conflict=false; };
    std::unique_ptr<EventQueue<RequestRefusal,4096>> queue_=std::make_unique<EventQueue<RequestRefusal,4096>>();
    std::map<Id,Entry> live_;
public:
    void Register(Id intent) {
        if (!intent || live_.size()>=Engine::kMaxInFlight || !live_.emplace(intent,Entry{}).second)
            throw std::runtime_error("invalid/full request-refusal registration");
    }
    bool Push(const RequestRefusal& event) noexcept { return queue_->Push(event); }
    void Forget(Id intent) noexcept { live_.erase(intent); }
    std::size_t size() const { return live_.size(); }
    std::optional<Result> Read(const Intent& intent,std::wstring_view symbol) {
        RequestRefusal event; std::uint64_t sequence=0;
        for (unsigned n=0;n<4096 && queue_->Pop(event,sequence);++n) {
            const auto found=live_.find(event.intent);
            if (found==live_.end()) continue; // Foreign or late callback after durable settlement.
            auto& entry=found->second;
            if (entry.evidence && !(*entry.evidence==event)) entry.conflict=true;
            else entry.evidence=event;
        }
        const auto found=live_.find(intent.id);
        if (found==live_.end() || !found->second.evidence) return {};
        const auto& entry=found->second; const auto& e=*entry.evidence;
        Result result; result.status=Result::Status::Uncertain;
        result.reason="conflicting_or_mismatched_refusal_callback";
        const auto end=std::find(e.symbol.begin(),e.symbol.end(),L'\0');
        const auto length=static_cast<std::size_t>(end-e.symbol.begin());
        if (entry.conflict || !intent.request_id || e.request!=intent.request_id || e.login!=intent.follower_login ||
            e.position!=intent.follower_position || e.requested!=intent.units || e.action!=3 ||
            e.type!=(intent.side==Side::Buy?0U:1U) || (e.flags&3)!=(intent.close?3U:2U) ||
            end==e.symbol.end() || std::wstring_view(e.symbol.data(),length)!=symbol ||
            e.retcode!=kRequestNoMoney || e.result_order || e.result_deal || e.result_volume) return result;
        result.status=Result::Status::Rejected;
        result.retcode=e.retcode;
        result.reason="server_pretrade_no_money_zero_fill";
        return result;
    }
};
} // namespace tradecopier::execution

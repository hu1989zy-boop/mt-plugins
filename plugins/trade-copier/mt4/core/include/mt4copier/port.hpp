#pragma once
#include "mt4copier/broker.hpp"
#include <map>
#include <optional>
#include <set>

namespace mt4copier {
struct Chain {
    Id root=0, current=0;
    Order opening;
    Units initial=0, remaining=0;
    std::vector<Order> records;
};
class Port final : public tradecopier::execution::Port {
public:
    // Stable nonzero plugin-instance identity; journal intent IDs make tokens
    // unique across restarts. The same journal must use the same instance ID.
    Port(Broker&,std::uint32_t instance);
    static Units ToUnits(std::int32_t);
    static std::int32_t ToVolume(Units);
    static Chain ResolveChain(const std::vector<Order>&,Id ticket);
    static bool IsCopierToken(const std::string&);
    std::string Token(Id intent) const;
    // Host sends exact SDK callback evidence to this ONE worker. Never call
    // Port from SDK callback threads. Same request may emit repeated evidence.
    void ObserveRequest(const RequestEvidence&);
    // Convert a live order callback to the original opening. A remainder
    // callback returns nullopt, so partial closes never create new source opens.
    std::optional<tradecopier::execution::Deal> Opening(Id login,Id ticket);
    tradecopier::execution::BootSnapshot Snapshot(const std::vector<Id>& logins,Id sequence);
    std::int64_t NowMs() override;
    void BeginCycle() override;
    tradecopier::execution::SourceView ReadSource(const tradecopier::execution::Deal&) override;
    tradecopier::SymbolResolution ResolveSymbol(const tradecopier::RuleConfig&,const std::string&,Side) override;
    tradecopier::VolumeLimits Limits(const tradecopier::RuleConfig&,const std::string&,const std::string&,Side,bool) override;
    tradecopier::execution::EquitySnapshot Equities(const tradecopier::RuleConfig&) override;
    Units FollowerRemaining(Id,Id,const std::string&,Side) override;
    tradecopier::execution::Submission Submit(const tradecopier::execution::Intent&) override;
    // Open fills need exact request callback + history. A live accepted close
    // may additionally settle its objective from its exact old ticket's
    // terminal history; this never attributes the close to a particular actor.
    tradecopier::execution::Result Reconcile(const tradecopier::execution::Intent&) override;
    void ForgetRequest(Id) noexcept override;
    void Log(const std::string&) override;
private:
    const std::vector<Order>& Orders(Id);
    Symbol FindSymbol(Id,const std::string&);
    Account CheckedAccount(Id);
    Broker& broker_;
    std::uint32_t instance_;
    std::map<Id,std::vector<Order>> orders_;
    std::map<std::uint32_t,std::vector<RequestEvidence>> evidence_;
    std::map<Id,std::uint32_t> requests_;
    // Live-process acceptance and exact native target. Never reconstructed
    // from a journal Intent: after restart a queued close remains uncertain.
    std::map<Id,std::pair<std::uint32_t,Request>> accepted_;
    std::set<std::uint32_t> contradictory_;
};
}

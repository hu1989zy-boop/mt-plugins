#pragma once
#include <windows.h>
#include <ctime>
#include "MT4ServerAPI.h"
#include "mt4copier/port.hpp"
#include "runtime.hpp"
#include <cstring>
#include <array>
#include <string>

namespace mt4copier {
template<std::size_t N> std::string Fixed(const char (&value)[N]) {
    const auto* end = static_cast<const char*>(std::memchr(value, 0, N));
    if (!end) throw std::runtime_error("unterminated SDK string");
    return {value, end};
}
std::string Utf8(const std::string& ansi);
std::string Ansi(const std::string& utf8);
Order CopyOrder(const TradeRecord&);

class SdkBroker final : public Broker {
public:
    SdkBroker(CServerInterface* api, CallGate& gate, std::uint32_t dealer_login)
        : api_(api), gate_(gate), dealer_login_(dealer_login) {}
    template<class F> auto Call(F&& f) { GateUse use(gate_); return f(api_); }
    std::int64_t NowMs() override;
    std::vector<Order> Orders(Id,std::int64_t,std::int64_t) override;
    Account AccountInfo(Id) override;
    std::vector<Symbol> Symbols(Id) override;
    tradecopier::execution::Submission Submit(const Request&) override;
    void Log(const std::string&) override;
private:
    struct DealerContext { UserInfo user{}; ConManager permissions{}; };
    DealerContext Dealer(const UserInfo& follower);
    int ValidateTrade(const Request&, const UserInfo&, const ConSymbol&);
    std::array<double,2> Quote(const Request&, const UserInfo&);
    UserInfo User(Id);
    ConSymbol SymbolInfo(const std::string&);
    CServerInterface* api_;
    CallGate& gate_;
    const std::uint32_t dealer_login_;
};
} // namespace mt4copier

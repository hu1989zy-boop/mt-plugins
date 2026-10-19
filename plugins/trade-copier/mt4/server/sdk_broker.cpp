#include "sdk_broker.hpp"
#include "diagnostic_text.hpp"
#include "dealer_policy.hpp"
#include "dealer_execution.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <tuple>

namespace mt4copier {
using tradecopier::execution::Submission;
static_assert(ServerApiVersion == 5);
static_assert(sizeof(void*) == 4 && sizeof(__time32_t) == 4);
static_assert(sizeof(TradeTransInfo) == 96);
static_assert(RET_TRADE_NO_MONEY == 134);
static_assert(TS_OPEN_NORMAL==0 && TS_OPEN_REMAND==1 && TS_OPEN_RESTORED==2 &&
              TS_CLOSED_NORMAL==3 && TS_CLOSED_PART==4 && TS_CLOSED_BY==5 && TS_DELETED==6);
namespace {
int Login(Id id) {
    if (!id || id > INT_MAX) throw std::runtime_error("MT4 account/ticket exceeds signed 32-bit range");
    return static_cast<int>(id);
}
void Check(int code, const char* label) {
    if (code != RET_OK) throw std::runtime_error(std::string(label) + " ret=" + std::to_string(code));
}
void CheckFound(int found, const char* label) {
    // These Server API lookups use BOOL semantics, as in the supplied
    // TradeCollector and DealerHelper examples. RET_OK checks invert success.
    if (found == FALSE) throw std::runtime_error(std::string(label) + " failed");
}
void CheckTickEconomics(const ConSymbol& symbol) {
    if (!std::isfinite(symbol.tick_size) || !std::isfinite(symbol.tick_value) ||
        symbol.tick_size<0 || symbol.tick_value<0 ||
        (symbol.profit_mode==PROFIT_CALC_FUTURES && (symbol.tick_size<=0 || symbol.tick_value<=0)))
        throw std::runtime_error("invalid MT4 contract tick size/value");
}
template<std::size_t N> void Copy(char (&out)[N], const std::string& text) {
    if (text.size() >= N || text.find('\0') != std::string::npos) throw std::runtime_error("SDK field length exceeded");
    std::memset(out, 0, N); std::memcpy(out, text.data(), text.size());
}
struct FreeOrders { void operator()(TradeRecord* p) const noexcept { if (p) HEAP_FREE(p); } };
void Append(std::vector<Order>& out, TradeRecord* data, int count, int login) {
    const std::unique_ptr<TradeRecord,FreeOrders> owner(data);
    if (count < 0 || count > 100000 || (count && !data)) throw std::runtime_error("invalid or oversized SDK order snapshot");
    for (int i=0; i<count; ++i) {
        if (data[i].login != login) throw std::runtime_error("SDK returned another account's orders");
        if (data[i].cmd == OP_BUY || data[i].cmd == OP_SELL) out.push_back(CopyOrder(data[i]));
    }
}
std::wstring Decode(const std::string& text, UINT page, DWORD flags) {
    if (text.empty()) return {};
    const int n=MultiByteToWideChar(page,flags,text.data(),static_cast<int>(text.size()),nullptr,0);
    if (!n) throw std::runtime_error("invalid encoded SDK text");
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    if (MultiByteToWideChar(page,flags,text.data(),static_cast<int>(text.size()),out.data(),n)!=n)
        throw std::runtime_error("SDK text decoding failed");
    return out;
}
}
std::string Utf8(const std::string& ansi) {
    const auto wide=Decode(ansi,CP_ACP,0);
    if (wide.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
    if (!n) throw std::runtime_error("UTF-8 encoding failed");
    std::string out(static_cast<std::size_t>(n),'\0');
    if (WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),out.data(),n,nullptr,nullptr)!=n)
        throw std::runtime_error("UTF-8 encoding failed");
    return out;
}
std::string Ansi(const std::string& utf8) {
    const auto wide=Decode(utf8,CP_UTF8,MB_ERR_INVALID_CHARS);
    if (wide.empty()) return {};
    BOOL substituted=FALSE;
    const UINT page=GetACP();
    const DWORD flags=page==CP_UTF8?0:WC_NO_BEST_FIT_CHARS;
    BOOL* used=page==CP_UTF8?nullptr:&substituted;
    const int n=WideCharToMultiByte(page,flags,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,used);
    if (!n || substituted) throw std::runtime_error("text cannot be represented in server code page");
    std::string out(static_cast<std::size_t>(n),'\0');
    if (WideCharToMultiByte(page,flags,wide.data(),static_cast<int>(wide.size()),out.data(),n,nullptr,used)!=n || substituted)
        throw std::runtime_error("server code page conversion failed");
    return out;
}
Order CopyOrder(const TradeRecord& raw) {
    if (raw.order<=0 || raw.login<=0 || (raw.cmd!=OP_BUY && raw.cmd!=OP_SELL) || raw.digits<0 || raw.digits>12)
        throw std::runtime_error("invalid SDK trade identity");
    Order out;
    out.ticket=static_cast<Id>(raw.order); out.login=static_cast<Id>(raw.login);
    out.symbol=Utf8(Fixed(raw.symbol)); out.comment=Utf8(Fixed(raw.comment));
    out.side=raw.cmd==OP_BUY?Side::Buy:Side::Sell; out.volume=raw.volume;
    out.open_ms=static_cast<std::int64_t>(raw.open_time)*1000;
    out.close_ms=static_cast<std::int64_t>(raw.close_time)*1000;
    out.open_price=raw.open_price; out.digits=static_cast<unsigned>(raw.digits);
    out.state=raw.state; out.reason=raw.reason;
    return out;
}
std::int64_t SdkBroker::NowMs() {
    const auto now=Call([](CServerInterface* api){return api->TradeTime();});
    if (now<=0) throw std::runtime_error("invalid MT4 server time");
    return static_cast<std::int64_t>(now)*1000;
}
UserInfo SdkBroker::User(Id id) {
    const int login=Login(id);
    UserRecord record{}; CheckFound(Call([&](CServerInterface* api){return api->ClientsUserInfo(login,&record);}),"ClientsUserInfo");
    if (record.login!=login) throw std::runtime_error("account identity mismatch");
    UserInfo user{};
    user.login=login; Copy(user.group,Fixed(record.group)); Copy(user.name,Fixed(record.name));
    user.enable=record.enable; user.enable_change_password=record.enable_change_password;
    user.enable_read_only=record.enable_read_only; user.enable_flags=record.enable_flags;
    user.leverage=record.leverage; user.agent_account=record.agent_account;
    user.balance=record.balance; user.credit=record.credit; user.prevbalance=record.prevbalance;
    CheckFound(Call([&](CServerInterface* api){return api->GroupsGet(user.group,&user.grp);}),"GroupsGet");
    if (Fixed(user.grp.group)!=Fixed(user.group)) throw std::runtime_error("account group identity mismatch");
    return user;
}
Account SdkBroker::AccountInfo(Id login) {
    auto user=User(login);
    // A configured and authorized dealer may attempt to trade for a read-only
    // client. Never change the native permission flags or the stored account.
    if (user.enable_read_only) (void)Dealer(user);
    const auto group=Fixed(user.group);
    double margin=std::numeric_limits<double>::quiet_NaN();
    double free=std::numeric_limits<double>::quiet_NaN(),equity=std::numeric_limits<double>::quiet_NaN();
    // The supplied MarketMakerCollector example uses TradesMarginInfo with a
    // populated UserInfo/ConGroup and does not interpret its return value.
    // Require all outputs to be written and finite; the return convention and
    // equity scale must also be checked on the target Demo server before use.
    const int status=Call([&](CServerInterface* api){return api->TradesMarginInfo(&user,&margin,&free,&equity);});
    if (user.login!=Login(login) || Fixed(user.group)!=group || Fixed(user.grp.group)!=group ||
        !std::isfinite(margin) || margin<0 || margin>1e14 ||
        !std::isfinite(free) || std::abs(free)>1e14 ||
        !std::isfinite(equity) || std::abs(equity)>1e14)
        throw std::runtime_error("invalid TradesMarginInfo account snapshot, ret="+std::to_string(status));
    Account out;
    out.currency=Utf8(Fixed(user.grp.currency));
    // Account permissions and existing closes remain available with a valid
    // negative equity. Zero means "no positive sizing equity"; Port::Equities
    // rejects it for equity-ratio openings, never for a failed SDK read.
    out.equity=equity>0?static_cast<std::uint64_t>(std::llround(equity*100.0)):0;
    out.query_ms=NowMs(); out.currency_digits=2;
    out.enabled=user.enable && user.grp.enable && !user.grp.hedge_prohibited && !user.grp.close_fifo;
    out.close_reopen=user.grp.close_reopen!=0;
    return out;
}
std::vector<Order> SdkBroker::Orders(Id id,std::int64_t from,std::int64_t to) {
    const int login=Login(id);
    if (from<0 || to<from || to/1000>INT_MAX) throw std::runtime_error("invalid MT4 history interval");
    auto user=User(id);
    const auto fields=[](const Order& o) {
        return std::tie(o.ticket,o.login,o.symbol,o.comment,o.side,o.volume,o.open_ms,o.close_ms,
                        o.open_price,o.digits,o.state,o.reason);
    };
    std::vector<Order> previous;
    for (unsigned attempt=0;attempt<3;++attempt) {
        // Read again at the current server boundary so a close which happened
        // during the previous pair of queries can appear in closed history.
        const auto until=std::max(to,NowMs());
        if (until/1000>INT_MAX) throw std::runtime_error("MT4 history time exceeds native range");
        int count=-1;
        auto* closed=Call([&](CServerInterface* api){return api->OrdersGetClosed(static_cast<__time32_t>(from/1000),
            static_cast<__time32_t>(until/1000),&login,1,&count);});
        std::vector<Order> result;
        Append(result,closed,count,login);
        count=-1;
        auto* open=Call([&](CServerInterface* api){return api->OrdersGetOpen(&user,&count);});
        Append(result,open,count,login);
        if (result.size()>100000) throw std::runtime_error("MT4 account history exceeds supported snapshot bound");
        std::sort(result.begin(),result.end(),[](const Order& a,const Order& b){return a.ticket<b.ticket;});
        for (std::size_t n=1;n<result.size();++n) if (result[n-1].ticket==result[n].ticket)
            throw std::runtime_error("duplicate MT4 order ticket in snapshot");
        if (attempt && result.size()==previous.size() &&
            std::equal(result.begin(),result.end(),previous.begin(),[&](const Order& a,const Order& b){return fields(a)==fields(b);}))
            return result;
        previous=std::move(result);
    }
    // Matching consecutive samples reduce mixed-history windows but are not
    // a server transaction barrier. Missing lineage is never a zero position.
    throw IncompleteSnapshot("mt4_snapshot_unstable");
}
ConSymbol SdkBroker::SymbolInfo(const std::string& name) {
    const auto ansi=Ansi(name); ConSymbol raw{};
    CheckFound(Call([&](CServerInterface* api){return api->SymbolsGet(ansi.c_str(),&raw);}),"SymbolsGet");
    if (Fixed(raw.symbol)!=ansi || raw.type<0 || raw.type>=MAX_SEC_GROUPS || raw.digits<0 || raw.digits>12)
        throw std::runtime_error("invalid MT4 symbol identity or group");
    CheckTickEconomics(raw);
    return raw;
}
std::vector<Symbol> SdkBroker::Symbols(Id login) {
    const auto user=User(login);
    if (user.enable_read_only) (void)Dealer(user);
    std::vector<Symbol> result;
    for (int n=0;n<MAX_SYMBOLS;++n) {
        ConSymbol raw{};
        const int found=Call([&](CServerInterface* api){return api->SymbolsNext(n,&raw);});
        if (found==FALSE) break; // Supplied TradeCollector uses BOOL enumeration semantics.
        if (raw.type<0 || raw.type>=MAX_SEC_GROUPS || raw.digits<0 || raw.digits>12)
            throw std::runtime_error("invalid MT4 symbol specification");
        const auto& effective=user.grp.secgroups[raw.type];
        if (!effective.show || effective.lot_min<=0 || effective.lot_max<=0 || effective.lot_step<=0) continue;
        CheckTickEconomics(raw);
        Symbol item;
        item.spec.name=Utf8(Fixed(raw.symbol)); item.spec.basis=Utf8(Fixed(raw.source));
        // MT4 has no distinct base/profit currency pair fields. Preserve the
        // supplied currency metadata; suffix inference needs an explicit suffix
        // if the SDK synonym is not a valid instrument prefix.
        item.spec.currency_base=Utf8(Fixed(raw.currency));
        item.spec.currency_margin=Utf8(Fixed(raw.margin_currency));
        item.spec.contract_size=raw.contract_size;
        item.spec.tick_size=raw.tick_size; item.spec.tick_value=raw.tick_value;
        item.spec.calc_mode=static_cast<unsigned>(raw.profit_mode*16+raw.margin_mode);
        item.spec.digits=static_cast<unsigned>(raw.digits);
        item.spec.start=raw.starting; item.spec.expiration=raw.expiration;
        const bool enabled=user.enable && user.grp.enable && effective.trade;
        item.spec.buy=enabled && raw.trade==TRADE_FULL;
        item.spec.sell=item.spec.buy && !raw.long_only;
        item.close_allowed=enabled && (raw.trade==TRADE_CLOSE || raw.trade==TRADE_FULL);
        item.market_execution=raw.exemode==EXE_MARKET;
        item.limits={Port::ToUnits(effective.lot_min),Port::ToUnits(effective.lot_max),Port::ToUnits(effective.lot_step)};
        result.push_back(std::move(item));
    }
    if (result.empty()) throw std::runtime_error("no usable MT4 symbols for account");
    return result;
}
SdkBroker::DealerContext SdkBroker::Dealer(const UserInfo& follower) {
    if (!dealer_login_ || dealer_login_>INT_MAX)
        throw std::runtime_error("configured DealerLogin required");
    DealerContext result;
    result.user=User(dealer_login_);
    // As with other Server API configuration lookups, require the requested
    // identity in the populated record as well as the lookup result.
    const auto lookup=Call([&](CServerInterface* api){return api->ManagersGet(static_cast<int>(dealer_login_),&result.permissions);});
    if (lookup==FALSE) throw std::runtime_error("DealerLogin ManagersGet unavailable native="+
        std::to_string(lookup)+" returned_login="+std::to_string(result.permissions.login));
    RequireDealerAccount(dealer_login_,static_cast<std::uint32_t>(result.permissions.login),
        result.user.enable!=0 && result.user.grp.enable!=0,result.user.enable_read_only!=0,
        result.permissions.broker!=0,Fixed(result.permissions.groups),Fixed(follower.group));
    Copy(result.user.ip,"mt4-copier");
    return result;
}
int SdkBroker::ValidateTrade(const Request& input,const UserInfo& user,const ConSymbol& symbol) {
    if (!user.enable || !user.grp.enable || user.grp.hedge_prohibited || user.grp.close_fifo ||
        user.grp.close_reopen || Fixed(user.grp.currency)!="USD")
        return RET_NOT_ENOUGH_RIGHTS;
    const auto& limits=user.grp.secgroups[symbol.type];
    if (symbol.exemode!=EXE_MARKET || !limits.show || !limits.trade || symbol.trade==TRADE_NO ||
        (!input.close && (symbol.trade!=TRADE_FULL || (symbol.long_only && input.position_side==Side::Sell))) ||
        input.volume<=0 || limits.lot_min<=0 || limits.lot_max<=0 || limits.lot_step<=0 ||
        input.volume<limits.lot_min || input.volume>limits.lot_max || input.volume%limits.lot_step)
        return RET_TRADE_BAD_VOLUME;
    if (input.close) {
        TradeRecord actual{};
        CheckFound(Call([&](CServerInterface* api){return api->OrdersGet(Login(input.ticket),&actual);}),"exact close order");
        if (actual.login!=Login(input.login) || actual.order!=Login(input.ticket) || actual.close_time ||
            Fixed(actual.symbol)!=Ansi(input.symbol) || actual.cmd!=(input.position_side==Side::Buy?OP_BUY:OP_SELL) ||
            input.expected_remaining<=0 || actual.volume!=input.expected_remaining || actual.volume<input.volume ||
            (actual.state!=TS_OPEN_NORMAL && actual.state!=TS_OPEN_REMAND))
            return RET_INVALID_DATA;
    }
    return RET_OK;
}
std::array<double,2> SdkBroker::Quote(const Request& input,const UserInfo& user) {
    const auto symbol=Ansi(input.symbol);
    double raw[2]{}; __time32_t tick_time=0; int tick_direction=0;
    Check(Call([&](CServerInterface* api){return api->HistoryPrices(symbol.c_str(),raw,&tick_time,&tick_direction);}),"dealer HistoryPrices");
    const auto now=NowMs(), at=static_cast<std::int64_t>(tick_time)*1000;
    if (at<=0 || at>now || now-at>5000)
        throw std::runtime_error("dealer quote missing, future or older than 5000 ms");
    std::array<double,2> prices{};
    Check(Call([&](CServerInterface* api){return api->HistoryPricesGroup(symbol.c_str(),&user.grp,prices.data());}),"dealer HistoryPricesGroup");
    if (!std::isfinite(prices[0]) || !std::isfinite(prices[1]) || prices[0]<=0 || prices[1]<prices[0])
        throw std::runtime_error("invalid dealer group bid/ask");
    return prices;
}
Submission SdkBroker::Submit(const Request& input) {
    auto user=User(input.login);
    const auto symbol=SymbolInfo(input.symbol);
    const auto code_before=ValidateTrade(input,user,symbol);
    if (code_before!=RET_OK) return {Submission::Status::NotSubmitted,0,static_cast<std::uint32_t>(code_before)};
    try {
        const auto dealer=Dealer(user);
        const auto& permission=dealer.permissions.secgroups[symbol.type];
        RequireDealerVolume(permission.enable!=0,permission.minimum_lots,permission.maximum_lots,input.volume);
    } catch (const std::exception& error) {
        try { Log("MT4_DEALER_PREFLIGHT login="+std::to_string(input.login)+" DealerLogin="+
            std::to_string(dealer_login_)+" reason="+DiagnosticText(error.what())); } catch (...) {}
        return {Submission::Status::NotSubmitted,0,RET_NOT_ENOUGH_RIGHTS};
    }
    if (!Port::IsCopierToken(input.token)) throw std::runtime_error("invalid MT4 request correlation token");
    RequestInfo request{};
    request.login=Login(input.login); Copy(request.group,Fixed(user.group));
    request.balance=user.balance; request.credit=user.credit;
    request.trade.type=static_cast<UCHAR>(input.close?TT_ORDER_MK_CLOSE:TT_ORDER_MK_OPEN);
    request.trade.flags=TT_FLAG_API;
    request.trade.cmd=static_cast<short>(input.position_side==Side::Buy?OP_BUY:OP_SELL);
    request.trade.order=input.close?Login(input.ticket):0;
    request.trade.volume=input.volume;
    Copy(request.trade.symbol,Ansi(input.symbol)); Copy(request.trade.comment,input.token);
    try {
        const auto prices=Quote(input,user);
        request.prices[0]=prices[0]; request.prices[1]=prices[1];
    } catch (const std::exception& error) {
        try { Log("MT4_DEALER_PREFLIGHT login="+std::to_string(input.login)+" DealerLogin="+
            std::to_string(dealer_login_)+" reason="+DiagnosticText(error.what())); } catch (...) {}
        return {Submission::Status::NotSubmitted,0,RET_TRADE_BAD_PRICES};
    }
    const bool use_ask=input.close?input.position_side==Side::Sell:input.position_side==Side::Buy;
    request.trade.price=request.prices[use_ask?1:0];
    int id=0;
    const int code=Call([&](CServerInterface* api){return api->RequestsAdd(&request,FALSE,&id);});
    // Diagnostics must not turn an already queued request into a submission
    // exception. Record native values without inferring execution from them.
    try {
        Log("MT4_REQUEST_SUBMIT request="+std::to_string(id)+" native_code="+std::to_string(code)+
            " login="+std::to_string(request.login)+" group="+DiagnosticText(Utf8(Fixed(request.group)))+
            " symbol="+DiagnosticText(input.symbol)+" type="+std::to_string(request.trade.type)+
            " cmd="+std::to_string(request.trade.cmd)+" volume="+std::to_string(request.trade.volume)+
            " order="+std::to_string(request.trade.order)+" flags="+std::to_string(static_cast<unsigned char>(request.trade.flags))+
            " DealerLogin="+std::to_string(dealer_login_)+" readonly="+std::to_string(user.enable_read_only)+
            " isdemo_argument=0 group_execution="+std::to_string(user.grp.secgroups[symbol.type].execution)+
            " symbol_execution="+std::to_string(symbol.exemode)+" token="+input.token);
    } catch (...) {} // A failed log never changes the SDK result or retries it.
    if ((code==RET_OK || code==RET_TRADE_ACCEPTED || code==RET_TRADE_PROCESS) && id>0) {
        // SDK AdvancedDealerHelper demonstrates RequestsLock + RequestsConfirm
        // with the dealer's UserInfo. RequestInfo.login remains the CLIENT;
        // orderby is not a dealer login. Operate only on this returned ID, never
        // enumerate or take over another client's/plugin's queued requests.
        const auto outcome=ExecuteAcceptedDealerRequest(true,id,dealer_login_,
            [this](int request_id,int dealer) { return Call([&](CServerInterface* api){return api->RequestsLock(request_id,dealer);}); },
            [this,&input,&user](int request_id,int) {
                DealerContext dealer;
                std::array<double,2> prices{};
                try {
                    auto fresh=User(input.login);
                    if (Fixed(fresh.group)!=Fixed(user.group))
                        throw std::runtime_error("follower group changed after request admission");
                    const auto current_symbol=SymbolInfo(input.symbol);
                    const auto valid=ValidateTrade(input,fresh,current_symbol);
                    if (valid!=RET_OK) throw std::runtime_error("dealer confirmation preflight ret="+std::to_string(valid));
                    dealer=Dealer(fresh);
                    const auto& permission=dealer.permissions.secgroups[current_symbol.type];
                    RequireDealerVolume(permission.enable!=0,permission.minimum_lots,permission.maximum_lots,input.volume);
                    prices=Quote(input,fresh);
                } catch (const std::exception& error) {
                    try { Log("MT4_DEALER_CONFIRM_PREFLIGHT request="+std::to_string(request_id)+
                        " login="+std::to_string(input.login)+" DealerLogin="+std::to_string(dealer_login_)+
                        " reason="+DiagnosticText(error.what())+"; accepted request still requires reconciliation"); } catch (...) {}
                    throw;
                }
                return Call([&](CServerInterface* api){return api->RequestsConfirm(request_id,&dealer.user,prices.data());});
            },
            [this](int request_id,int dealer) { return Call([&](CServerInterface* api){return api->RequestsFree(request_id,dealer);}); });
        // Even a successful confirm is not fill evidence. Any post-admission
        // error retains the accepted ID for reconciliation; it must not retry.
        try { Log("MT4_DEALER_EXECUTION request="+std::to_string(id)+" login="+std::to_string(input.login)+
            " "+DealerExecutionDiagnostic(outcome)); } catch (...) {}
        return {Submission::Status::Queued,static_cast<std::uint32_t>(id),static_cast<std::uint32_t>(code)};
    }
    // Only explicit synchronous refusal codes mean it was not enqueued.
    // A timeout, unknown status or missing accepted request ID is ambiguous.
    if (code==RET_TRADE_NO_MONEY)
        return {id==0?Submission::Status::NotSubmitted:Submission::Status::Unknown,
            id>0?static_cast<std::uint32_t>(id):0,static_cast<std::uint32_t>(code)};
    if (id==0 && (code==RET_TRADE_BAD_VOLUME || code==RET_TRADE_BAD_STOPS ||
        code==RET_TRADE_DISABLE || code==RET_TRADE_MARKET_CLOSED || code==RET_NOT_ENOUGH_RIGHTS ||
        code==RET_ACCOUNT_DISABLED || code==RET_TRADE_LONG_ONLY))
        return {Submission::Status::NotSubmitted,0,static_cast<std::uint32_t>(code)};
    return {Submission::Status::Unknown,id>0?static_cast<std::uint32_t>(id):0,static_cast<std::uint32_t>(code)};
}
void SdkBroker::Log(const std::string& message) {
    const auto line=Ansi("MT4TradeCopierExecutor: "+message);
    Call([&](CServerInterface* api){api->LogsOut(CmdOK,"mt4-copier",line.c_str());});
}
} // namespace mt4copier

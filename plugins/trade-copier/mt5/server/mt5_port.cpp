#include "mt5_port.hpp"
#include "readonly_policy.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <limits>
#include <set>
#include <tuple>

static_assert(MTServerAPIVersion==6182,"Executor requires the reviewed Server API 6182 headers");
static_assert(MT_RET_REQUEST_NO_MONEY==tradecopier::execution::kRequestNoMoney);
static_assert(IMTRequest::TA_MARKET==3 && IMTRequest::TA_FLAG_CLOSE==1 && IMTRequest::TA_FLAG_MARKET==2);
static_assert(IMTOrder::OP_BUY==0 && IMTOrder::OP_SELL==1);
static_assert(IMTRequest::TA_MARKET==tradecopier::execution::kClientMarketAction &&
              IMTRequest::TA_DEALER_POS_EXECUTE==tradecopier::execution::kDealerExecuteAction);
static_assert(IMTUser::USER_RIGHT_ENABLED==tradecopier::execution::kUserEnabled &&
              (IMTUser::USER_RIGHT_TRADE_DISABLED|IMTUser::USER_RIGHT_INVESTOR|IMTUser::USER_RIGHT_READONLY)==
              tradecopier::execution::kClientTradingBlocked);
static_assert(IMTRequest::TA_FLAG_SKIP_MARGIN_CHECK==tradecopier::execution::kRequestSkipMarginCheck);
namespace tradecopier::execution {
namespace {
// Cache only within one read cycle. Bound aggregate SDK-owned rows as well as
// account count; a wide fan-out must not retain 100,000 rows per account.
template<typename Map> void TrimHistory(Map& windows,Id keep,std::size_t incoming) {
    std::size_t total=incoming;
    for (const auto& e:windows) if (e.first!=keep && e.second.rows) total+=e.second.rows->Total();
    for (auto it=windows.begin();it!=windows.end() && (total>100000 || windows.size()>32);) {
        if (it->first==keep) { ++it; continue; }
        if (it->second.rows) total-=it->second.rows->Total();
        it=windows.erase(it);
    }
}
bool Absent(MTAPIRES r) { return r==MT_RET_ERR_NOTFOUND || r==MT_RET_OK_NONE; }
bool Final(const IMTOrder* o) {
    const auto s=o->State();
    return s==IMTOrder::ORDER_STATE_FILLED || s==IMTOrder::ORDER_STATE_CANCELED ||
           s==IMTOrder::ORDER_STATE_REJECTED || s==IMTOrder::ORDER_STATE_EXPIRED;
}
bool Add(Units& sum,Units value) {
    if (!value || value>std::numeric_limits<Units>::max()-sum) return false;
    sum+=value; return true;
}
std::uint32_t Action(Side side) { return side==Side::Buy ? IMTDeal::DEAL_BUY : IMTDeal::DEAL_SELL; }
bool OrdinaryReason(std::uint32_t reason) {
    switch (reason) {
    case IMTDeal::DEAL_REASON_CLIENT: case IMTDeal::DEAL_REASON_EXPERT:
    case IMTDeal::DEAL_REASON_DEALER: case IMTDeal::DEAL_REASON_SL:
    case IMTDeal::DEAL_REASON_TP: case IMTDeal::DEAL_REASON_SO:
    case IMTDeal::DEAL_REASON_EXTERNAL_CLIENT: case IMTDeal::DEAL_REASON_GATEWAY:
    case IMTDeal::DEAL_REASON_SIGNAL: case IMTDeal::DEAL_REASON_EXTERNAL_SERVICE:
    case IMTDeal::DEAL_REASON_MOBILE: case IMTDeal::DEAL_REASON_WEB: return true;
    default: return false; // No rollover, split, transfer or database synchronization.
    }
}
SourceView Invalid(const char* reason) { SourceView s; s.reason=reason; return s; }
SourceView Waiting() { SourceView s; s.state=ReadState::Waiting; return s; }
Result Uncertain(const char* reason) { Result r; r.status=Result::Status::Uncertain; r.reason=reason; return r; }
std::uint64_t ScaledEquity(double value,unsigned digits) {
    if (!std::isfinite(value) || value<=0 || digits>8) throw std::runtime_error("unsupported equity value/precision");
    long double scale=1;
    for (unsigned i=0;i<digits;++i) scale*=10;
    const long double scaled=std::round(static_cast<long double>(value)*scale);
    if (scaled<1 || scaled>9007199254740991.0L) throw std::runtime_error("equity exceeds exact scaling range");
    return static_cast<std::uint64_t>(scaled);
}
}

std::string Utf8(const wchar_t* text) {
    if (!text) throw std::runtime_error("null SDK string");
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);
    if (!n || n>4096) throw std::runtime_error("invalid SDK string");
    std::string s(static_cast<std::size_t>(n),'\0');
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,s.data(),n,nullptr,nullptr))
        throw std::runtime_error("UTF8 conversion failed");
    s.pop_back(); return s;
}
std::wstring Wide(const std::string& text) {
    if (text.empty() || text.size()>4096 || text.find('\0')!=std::string::npos)
        throw std::runtime_error("invalid UTF8 string");
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.c_str(),-1,nullptr,0);
    if (!n) throw std::runtime_error("invalid UTF8");
    std::wstring s(static_cast<std::size_t>(n),L'\0');
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.c_str(),-1,s.data(),n))
        throw std::runtime_error("wide conversion failed");
    s.pop_back(); return s;
}
std::int64_t Mt5Port::NowMs() {
    const auto now=Call([](IMTServerAPI* api) { return api->TimeCurrentMsc(); });
    if (now<=0) throw std::runtime_error("invalid server time");
    return now;
}
void Mt5Port::Log(const std::string& message) {
    const auto text=Wide("TradeCopierExecutor: "+message);
    Call([&](IMTServerAPI* api) { return api->LoggerOut(MTLogOK,L"%s",text.c_str()); });
}
SdkPtr<IMTConGroup> Mt5Port::AccountGroup(Id login,bool follower,std::uint64_t* snapshot_rights) {
    auto user=Own(Call([](IMTServerAPI* api) { return api->UserCreate(); }));
    Check(Call([&](IMTServerAPI* api) { return api->UserGet(login,user.get()); }),"UserGet");
    if (user->Login()!=login) throw std::runtime_error("account identity mismatch");
    const auto rights=user->Rights();
    CheckAccountAccess(rights,follower,dealer_login_,login);
    if (snapshot_rights) *snapshot_rights=rights;
    auto group=Own(Call([](IMTServerAPI* api) { return api->GroupCreate(); }));
    Check(Call([&](IMTServerAPI* api) { return api->GroupGet(user->Group(),group.get()); }),"GroupGet");
    if (group->Server()!=server_ || group->MarginMode()!=IMTConGroup::MARGIN_MODE_RETAIL_HEDGED ||
        std::wcscmp(group->Currency(),L"USD")!=0 || group->CurrencyDigits()>8)
        throw std::runtime_error("requires local-server Hedging/USD account login="+std::to_string(login));
    return group;
}
SdkPtr<IMTConSymbol> Mt5Port::Symbol(Id login,const std::string& name,bool follower,std::uint64_t* rights) {
    auto group=AccountGroup(login,follower,rights);
    auto symbol=Own(Call([](IMTServerAPI* api) { return api->SymbolCreate(); }));
    const auto wide=Wide(name);
    Check(Call([&](IMTServerAPI* api) { return api->SymbolGet(wide.c_str(),group.get(),symbol.get()); }),"SymbolGet effective");
    if (Utf8(symbol->Symbol())!=name) throw std::runtime_error("symbol identity mismatch");
    return symbol;
}
BootSnapshot Mt5Port::Baseline(const CopierConfig& config) {
    BootSnapshot snapshot;
    std::set<Id> sources;
    for (const auto& rule:config.relationships) if (rule.enabled) {
        (void)AccountGroup(rule.source_login); (void)AccountGroup(rule.follower_login,true);
        sources.insert(rule.source_login);
    }
    if (sources.empty()) throw std::runtime_error("no enabled relationships");
    for (Id source:sources) {
        auto positions=Own(Call([](IMTServerAPI* api) { return api->PositionCreateArray(); }));
        const auto ret=Call([&](IMTServerAPI* api) { return api->PositionGet(source,positions.get()); });
        if (ret!=MT_RET_OK && !Absent(ret)) Check(ret,"PositionGet baseline");
        if (positions->Total()>100000) throw std::runtime_error("baseline too large");
        for (std::uint32_t n=0;n<positions->Total();++n) {
            const auto* p=positions->Next(n);
            if (!p || p->Login()!=source) throw std::runtime_error("invalid baseline position");
            if (snapshot.positions.size()>=100000) throw std::runtime_error("combined baseline too large");
            snapshot.positions.emplace_back(source,p->Position());
        }
    }
    snapshot.time_ms=NowMs();
    return snapshot;
}
const Mt5Port::HistoryWindow<IMTDealArray>& Mt5Port::Deals(Id login,std::int64_t from_ms) {
    const auto now=NowMs();
    if (from_ms<=0 || from_ms>now) throw std::runtime_error("invalid history time window");
    auto& window=deal_windows_[login];
    const auto from=std::max<std::int64_t>(0,std::min(from_ms/1000-1,now/1000-30));
    if (window.rows && window.from<=from) { ++history_metrics_.cached_windows; return window; }
    auto deals=Own(Call([](IMTServerAPI* api) { return api->DealCreateArray(); }));
    ++history_metrics_.deal_queries;
    const auto ret=Call([&](IMTServerAPI* api) {
        return api->DealGet(from,now/1000+1,login,deals.get());
    });
    if (ret!=MT_RET_OK && !Absent(ret)) Check(ret,"DealGet history");
    if (deals->Total()>100000) throw std::runtime_error("history query exceeds Demo-stage limit");
    auto index=HistoryIndex::Build(deals->Total(),[&](std::uint32_t row) -> HistoryIndex::Keys {
        const auto* deal=deals->Next(row);
        if (!deal) return {false,{},{}};
        // Index all entries, including canceled and malformed trades. The
        // existing lifecycle/identity checks must see them, not filter them out.
        return {true,deal->Order(),deal->PositionID()};
    });
    history_metrics_.indexed_deal_rows+=deals->Total();
    TrimHistory(deal_windows_,login,deals->Total());
    window.from=from; window.rows=std::move(deals); window.index=std::move(index);
    return window;
}
const Mt5Port::HistoryWindow<IMTOrderArray>& Mt5Port::Orders(Id login,std::int64_t from_ms) {
    const auto now=NowMs();
    if (from_ms<=0 || from_ms>now) throw std::runtime_error("invalid order history time window");
    auto& window=order_windows_[login];
    const auto from=std::max<std::int64_t>(0,std::min(from_ms/1000-1,now/1000-30));
    if (window.rows && window.from<=from) { ++history_metrics_.cached_windows; return window; }
    auto rows=Own(Call([](IMTServerAPI* api) { return api->OrderCreateArray(); }));
    ++history_metrics_.order_queries;
    const auto ret=Call([&](IMTServerAPI* api) { return api->HistoryGet(from,now/1000+1,login,rows.get()); });
    if (ret!=MT_RET_OK && !Absent(ret)) Check(ret,"HistoryGet target request");
    if (rows->Total()>100000) throw std::runtime_error("target history limit exceeded");
    auto index=HistoryIndex::Build(rows->Total(),[&](std::uint32_t row) -> HistoryIndex::Keys {
        const auto* order=rows->Next(row);
        if (!order) return {false,{},{}};
        if (ApiValue(order,app_,1)!=kCopierMarker) return {};
        const auto intent=ApiValue(order,app_,2);
        // Keep server validation in Reconcile after the intent comparison,
        // preserving its short-circuit behavior for unrelated metadata.
        return {true,intent,{}};
    });
    history_metrics_.indexed_order_rows+=rows->Total();
    TrimHistory(order_windows_,login,rows->Total());
    window.from=from; window.rows=std::move(rows); window.index=std::move(index);
    return window;
}
const HistoryIndex::Rows& Mt5Port::HistoryCandidates(const HistoryIndex& index,Id key,bool secondary) {
    const auto& rows=secondary?index.Secondary(key):index.Primary(key);
    ++history_metrics_.index_lookups;
    if (!rows.empty()) ++history_metrics_.index_hits;
    history_metrics_.candidate_rows+=rows.size();
    return rows;
}
Units Mt5Port::Position(Id login,Id ticket,const std::string& name,Side side) {
    if (!login || !ticket) throw std::runtime_error("invalid exact-position lookup");
    auto position=Own(Call([](IMTServerAPI* api) { return api->PositionCreate(); }));
    const auto ret=Call([&](IMTServerAPI* api) { return api->PositionGetByTicket(ticket,position.get()); });
    if (Absent(ret)) return 0;
    Check(ret,"PositionGetByTicket");
    if (position->Position()!=ticket || position->Login()!=login ||
        Utf8(position->Symbol())!=name || position->Action()!=Action(side) || !position->VolumeExt())
        throw std::runtime_error("exact position identity/side mismatch");
    return position->VolumeExt();
}
Units Mt5Port::FollowerRemaining(Id login,Id ticket,const std::string& symbol,Side side) {
    (void)AccountGroup(login,true);
    const auto actual=Position(login,ticket,symbol,side);
    if (actual) return actual;
    // A missing ticket alone is not evidence of manual closure: a utility
    // operation can re-open a position under another ticket. Require ordinary
    // entry/exit history to prove this mapped lifecycle has fully closed.
    auto order=Own(Call([](IMTServerAPI* api) { return api->OrderCreate(); }));
    Check(Call([&](IMTServerAPI* api) { return api->HistoryGet(ticket,order.get()); }),"missing follower entry order");
    if (order->Login()!=login || order->PositionID()!=ticket || Utf8(order->Symbol())!=symbol)
        throw std::runtime_error("missing follower position identity not established");
    const auto& deals=Deals(login,order->TimeSetupMsc());
    if (deals.index.HasNullRows()) throw std::runtime_error("null follower history deal");
    Units opened=0,closed=0;
    for (const auto n:HistoryCandidates(deals.index,ticket,true)) {
        const auto* d=deals.rows->Next(n);
        if (!d) throw std::runtime_error("null follower history deal");
        if (d->PositionID()!=ticket) continue;
        if (d->Action()!=IMTDeal::DEAL_BUY && d->Action()!=IMTDeal::DEAL_SELL) {
            if (d->Action()==IMTDeal::DEAL_BUY_CANCELED || d->Action()==IMTDeal::DEAL_SELL_CANCELED)
                throw std::runtime_error("canceled follower history deal");
            continue;
        }
        if (d->Login()!=login || Utf8(d->Symbol())!=symbol || !OrdinaryReason(d->Reason()))
            throw std::runtime_error("unsupported follower history/utility operation");
        if (d->Entry()==IMTDeal::ENTRY_IN && d->Order()==ticket && d->Action()==Action(side)) {
            if (!Add(opened,d->VolumeExt())) throw std::runtime_error("invalid follower entry volume");
        } else if (d->Entry()==IMTDeal::ENTRY_OUT && d->Action()!=Action(side)) {
            if (!Add(closed,d->VolumeExt())) throw std::runtime_error("invalid follower exit volume");
        } else throw std::runtime_error("unsupported follower lifecycle change");
    }
    if (!opened || opened!=closed) throw std::runtime_error("missing follower position not reconciled as closed");
    return 0;
}
SourceView Mt5Port::ReadSource(const Deal& event) {
    (void)AccountGroup(event.login);
    auto order=Own(Call([](IMTServerAPI* api) { return api->OrderCreate(); }));
    const auto ret=Call([&](IMTServerAPI* api) { return api->HistoryGet(event.order,order.get()); });
    if (Absent(ret)) return Waiting();
    Check(ret,"HistoryGet source entry order");
    if (!Final(order.get())) return Waiting();
    if (order->Login()!=event.login || order->PositionID()!=event.position ||
        Utf8(order->Symbol())!=event.symbol || order->Type()>IMTOrder::OP_SELL_STOP_LIMIT ||
        order->Type()%2!=Action(event.side) || ApiValue(order.get(),app_,1)==kCopierMarker)
        return Invalid("source order identity/origin mismatch");
    const auto& deals=Deals(event.login,order->TimeSetupMsc());
    if (deals.index.HasNullRows()) return Invalid("null source history deal");
    SourceView view; view.state=ReadState::Ready;
    Units closed=0;
    std::set<Id> seen;
    bool saw_callback=false;
    for (const auto n:HistoryCandidates(deals.index,event.position,true)) {
        const auto* d=deals.rows->Next(n);
        if (!d) return Invalid("null source history deal");
        if (d->PositionID()!=event.position) continue;
        if (d->Action()==IMTDeal::DEAL_BUY_CANCELED || d->Action()==IMTDeal::DEAL_SELL_CANCELED)
            return Invalid("canceled source deal");
        if (d->Action()!=IMTDeal::DEAL_BUY && d->Action()!=IMTDeal::DEAL_SELL) continue;
        if (d->Login()!=event.login || Utf8(d->Symbol())!=event.symbol || !d->Deal() || !OrdinaryReason(d->Reason()) ||
            !seen.insert(d->Deal()).second) return Invalid("invalid/duplicate source history");
        if (d->Entry()==IMTDeal::ENTRY_IN) {
            if (d->Order()!=event.order || d->Action()!=Action(event.side) ||
                !Add(view.opening_units,d->VolumeExt())) return Invalid("multiple source entries/reversal");
            if (!view.first_fill_ms || d->TimeMsc()<view.first_fill_ms) view.first_fill_ms=d->TimeMsc();
            if (d->Deal()==event.id && d->VolumeExt()==event.units) saw_callback=true;
        } else if (d->Entry()==IMTDeal::ENTRY_OUT) {
            if (d->Action()==Action(event.side) || !Add(closed,d->VolumeExt())) return Invalid("invalid source close");
            view.closes.push_back({d->Deal(),d->VolumeExt(),d->TimeMsc()});
        } else return Invalid("Close By/reversal outside this executor stage");
    }
    if (!saw_callback || !view.opening_units || closed>view.opening_units ||
        order->VolumeCurrentExt()>order->VolumeInitialExt() ||
        order->VolumeInitialExt()-order->VolumeCurrentExt()!=view.opening_units) return Waiting();
    view.remaining=view.opening_units-closed;
    if (Position(event.login,event.position,event.symbol,event.side)!=view.remaining) return Waiting();
    std::sort(view.closes.begin(),view.closes.end(),[](const SourceClose& a,const SourceClose& b) {
        return std::tie(a.time_ms,a.deal)<std::tie(b.time_ms,b.deal);
    });
    return view;
}
namespace {
bool CanTrade(const IMTConSymbol* target,Side side,bool close) {
    const auto mode=target->TradeMode();
    return target->ExecMode()==IMTConSymbol::EXECUTION_MARKET &&
        (target->OrderFlags()&IMTConSymbol::ORDER_FLAGS_MARKET) &&
        (target->FillFlags()&(IMTConSymbol::FILL_FLAGS_IOC|IMTConSymbol::FILL_FLAGS_FOK)) &&
        mode!=IMTConSymbol::TRADE_DISABLED &&
        (close || (mode!=IMTConSymbol::TRADE_CLOSEONLY &&
        !(mode==IMTConSymbol::TRADE_LONGONLY && side==Side::Sell) &&
        !(mode==IMTConSymbol::TRADE_SHORTONLY && side==Side::Buy))) &&
        ValidLimits({target->VolumeMinExt(),target->VolumeMaxExt(),target->VolumeStepExt()});
}
SymbolSpec Specification(const IMTConSymbol* symbol) {
    SymbolSpec s;
    s.name=Utf8(symbol->Symbol()); s.basis=Utf8(symbol->Basis());
    s.currency_base=Utf8(symbol->CurrencyBase()); s.currency_profit=Utf8(symbol->CurrencyProfit());
    s.currency_margin=Utf8(symbol->CurrencyMargin()); s.isin=Utf8(symbol->ISIN());
    s.contract_size=symbol->ContractSize(); s.calc_mode=symbol->CalcMode(); s.digits=symbol->Digits();
    s.start=symbol->TimeStart(); s.expiration=symbol->TimeExpiration();
    s.options_mode=symbol->OptionsMode(); s.strike=symbol->PriceStrike(); s.face_value=symbol->FaceValue();
    s.buy=CanTrade(symbol,Side::Buy,false); s.sell=CanTrade(symbol,Side::Sell,false);
    return s;
}
}
SymbolResolution Mt5Port::ResolveSymbol(const RuleConfig& rule,const std::string& name,Side side) {
    const auto source=Symbol(rule.source_login,name);
    const auto spec=Specification(source.get());
    const auto base=rule.auto_symbol?SourceBaseSymbol(spec,rule.source_suffix):name;
    if (base.empty()) return {{},"源品种与配置的源后缀不一致："+name};
    // One symbol inventory per planning round, shared by the entire fan-out.
    // User rights are checked per account; accounts in the same group share
    // this round's effective specifications. No result survives the next round.
    if (!names_loaded_) {
        const auto total=Call([](IMTServerAPI* api) { return api->SymbolTotal(); });
        if (total>100000) throw std::runtime_error("symbol inventory exceeds scan limit");
        auto item=Own(Call([](IMTServerAPI* api) { return api->SymbolCreate(); }));
        for (std::uint32_t n=0;n<total;++n) {
            Check(Call([&](IMTServerAPI* api) { return api->SymbolNext(n,item.get()); }),"SymbolNext inventory");
            symbol_names_.push_back(Utf8(item->Symbol()));
        }
        if (Call([](IMTServerAPI* api) { return api->SymbolTotal(); })!=total)
            throw std::runtime_error("symbol inventory changed during scan");
        names_loaded_=true;
    }
    auto group=AccountGroup(rule.follower_login,true);
    const auto key=std::make_pair(std::wstring(group->Group()),base);
    auto found=candidates_.find(key);
    if (found==candidates_.end()) {
        auto target=Own(Call([](IMTServerAPI* api) { return api->SymbolCreate(); }));
        std::vector<SymbolSpec> candidates;
        for (const auto& candidate:symbol_names_) {
            if (candidate.compare(0,base.size(),base)!=0) continue;
            const auto wide=Wide(candidate);
            const auto ret=Call([&](IMTServerAPI* api) { return api->SymbolGet(wide.c_str(),group.get(),target.get()); });
            if (Absent(ret)) continue;
            Check(ret,"SymbolGet effective candidate");
            if (Utf8(target->Symbol())!=candidate) throw std::runtime_error("candidate symbol identity mismatch");
            candidates.push_back(Specification(target.get()));
        }
        found=candidates_.emplace(key,std::move(candidates)).first;
    }
    auto result=ResolveTargetSymbol(spec,found->second,rule.auto_symbol,rule.source_suffix,side==Side::Buy);
    if (!result.error.empty()) result.error+="；跟单账号="+std::to_string(rule.follower_login);
    return result;
}
VolumeLimits Mt5Port::Limits(const RuleConfig& rule,const std::string& source_name,const std::string& target_name,Side side,bool close) {
    auto target=Symbol(rule.follower_login,target_name,true);
    if (!close) {
        auto source=Symbol(rule.source_login,source_name);
        if (!SameSymbolContract(Specification(source.get()),Specification(target.get())))
            throw std::runtime_error("source/target effective contract differs");
    }
    if (!CanTrade(target.get(),side,close)) throw std::runtime_error("unsupported target execution/order/side policy");
    return {target->VolumeMinExt(),target->VolumeMaxExt(),target->VolumeStepExt()};
}
EquitySnapshot Mt5Port::Equities(const RuleConfig& rule) {
    const auto sg=AccountGroup(rule.source_login), fg=AccountGroup(rule.follower_login,true);
    auto source=Own(Call([](IMTServerAPI* api) { return api->UserCreateAccount(); }));
    auto target=Own(Call([](IMTServerAPI* api) { return api->UserCreateAccount(); }));
    Check(Call([&](IMTServerAPI* api) { return api->UserAccountGet(rule.source_login,source.get()); }),"source equity");
    EquitySnapshot snapshot; snapshot.source_query_ms=NowMs();
    Check(Call([&](IMTServerAPI* api) { return api->UserAccountGet(rule.follower_login,target.get()); }),"follower equity");
    snapshot.follower_query_ms=NowMs();
    if (snapshot.follower_query_ms<snapshot.source_query_ms ||
        snapshot.follower_query_ms-snapshot.source_query_ms>5000)
        throw std::runtime_error("equity queries exceeded 5-second freshness window");
    if (source->Login()!=rule.source_login || target->Login()!=rule.follower_login ||
        source->CurrencyDigits()!=sg->CurrencyDigits() || target->CurrencyDigits()!=fg->CurrencyDigits())
        throw std::runtime_error("equity identity/precision changed");
    snapshot.common_digits=std::max(source->CurrencyDigits(),target->CurrencyDigits());
    snapshot.source=ScaledEquity(source->Equity(),snapshot.common_digits);
    snapshot.follower=ScaledEquity(target->Equity(),snapshot.common_digits);
    return snapshot;
}
void Mt5Port::CheckDealer(Id follower) {
    auto group=AccountGroup(follower,true);
    auto manager=Own(Call([](IMTServerAPI* api) { return api->ManagerCreate(); }));
    Check(Call([&](IMTServerAPI* api) { return api->ManagerGet(dealer_login_,manager.get()); }),"DealerLogin ManagerGet");
    auto user=Own(Call([](IMTServerAPI* api) { return api->UserCreate(); }));
    Check(Call([&](IMTServerAPI* api) { return api->UserGet(dealer_login_,user.get()); }),"DealerLogin UserGet");
    if (user->Login()!=dealer_login_) throw std::runtime_error("dealer account identity mismatch");
    const auto count=manager->GroupTotal();
    if (count>100000) throw std::runtime_error("dealer group list exceeds limit");
    std::vector<std::wstring> groups; groups.reserve(count);
    for (std::uint32_t n=0;n<count;++n) {
        const auto* name=manager->GroupNext(n);
        if (!name) throw std::runtime_error("invalid dealer group entry");
        groups.emplace_back(name);
    }
    try {
        RequireDealerGroup(dealer_login_,manager->Login(),(user->Rights()&IMTUser::USER_RIGHT_ENABLED)!=0,
            manager->Right(IMTConManager::RIGHT_TRADES_DEALER)==IMTConManager::RIGHT_FLAGS_GRANTED,
            groups,group->Group());
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(error.what())+" follower_group="+Utf8(group->Group()));
    }
}
Submission Mt5Port::Submit(const Intent& intent) {
    SdkPtr<IMTRequest> request;
    std::uint32_t action=kClientMarketAction;
    Id dealer=0;
    const auto not_submitted=[&](std::uint32_t retcode,const std::string& reason) {
        Log("NOT_SUBMITTED intent="+std::to_string(intent.id)+" follower="+std::to_string(intent.follower_login)+
            " source_deal="+std::to_string(intent.source_event)+" dealer="+std::to_string(dealer_login_)+
            " retcode="+std::to_string(retcode)+" preflight="+reason);
        return Submission{Submission::Status::NotSubmitted,0,retcode};
    };
    // This block performs reads and constructs an unsubmitted request only.
    // A failure here proves that TradeRequest has not been called.
    try {
        std::uint64_t rights=0;
        auto symbol=Symbol(intent.follower_login,intent.symbol,true,&rights);
        if (!CanTrade(symbol.get(),intent.side,intent.close))
            return not_submitted(MT_RET_ERR_NOTSUPPORTED,"target execution/order/side policy unavailable at submission");
        if (intent.close && Position(intent.follower_login,intent.follower_position,intent.symbol,
            intent.side==Side::Buy?Side::Sell:Side::Buy)!=intent.follower_before)
            return not_submitted(MT_RET_REQUEST_ORDER_CHANGED,"follower position changed before close submission");
        action=CopyRequestAction(rights,dealer_login_,intent.follower_login);
        dealer=action==kDealerExecuteAction?dealer_login_:0;
        MTTickShort tick{};
        if (dealer) {
            CheckDealer(intent.follower_login);
            // Use group-adjusted bid/ask, never the raw-symbol overload.
            Check(Call([&](IMTServerAPI* api) { return api->TickLast(symbol.get(),tick); }),"dealer TickLast effective");
        }
        request=Own(Call([](IMTServerAPI* api) { return api->TradeRequestCreate(); }));
        Check(request->Clear(),"request Clear");
        Check(request->Action(action),"request Action");
        Check(request->Login(intent.follower_login),"request Login");
        if (dealer) Check(request->SourceLogin(dealer),"request dealer SourceLogin");
        const auto name=Wide(intent.symbol);
        Check(request->Symbol(name.c_str()),"request Symbol");
        Check(request->Type(intent.side==Side::Buy?IMTOrder::OP_BUY:IMTOrder::OP_SELL),"request Type");
        Check(request->TypeFill(symbol->FillFlags()&IMTConSymbol::FILL_FLAGS_IOC?IMTOrder::ORDER_FILL_IOC:IMTOrder::ORDER_FILL_FOK),"request Fill");
        Check(request->TypeTime(IMTOrder::ORDER_TIME_GTC),"request Time");
        // In particular, never set TA_FLAG_SKIP_MARGIN_CHECK. TA_FLAG_MARKET is
        // obsolete for dealer requests; price is explicitly the current quote.
        Check(request->Flags(CopyRequestFlags(action,intent.close)),"request Flags");
        Check(request->VolumeExt(intent.units),"request VolumeExt");
        if (intent.close) Check(request->Position(intent.follower_position),"request Position");
        Check(request->PriceSL(0),"request SL"); Check(request->PriceTP(0),"request TP");
        Check(request->DigitsSet(symbol->Digits()),"request Digits");
        const auto comment=L"TCX:"+std::to_wstring(intent.id);
        Check(request->Comment(comment.c_str()),"request Comment");
        Check(request->ApiDataSet(app_,1,kCopierMarker),"request marker");
        Check(request->ApiDataSet(app_,2,intent.id),"request intent marker");
        Check(request->ApiDataSet(app_,3,server_),"request server marker");
        const auto now=NowMs();
        if (intent.equity.source_query_ms && (now<intent.equity.source_query_ms || now-intent.equity.source_query_ms>5000))
            return not_submitted(MT_RET_ERR_TIMEOUT,"equity snapshot outside 5000 ms submission window");
        if (dealer) {
            Check(request->PriceOrder(DealerExecutionPrice(intent.side,tick.bid,tick.ask,tick.datetime_msc,now)),"dealer PriceOrder");
            Log("READONLY_COPY intent="+std::to_string(intent.id)+" follower="+std::to_string(intent.follower_login)+
                " dealer="+std::to_string(dealer)+" operation="+(intent.close?"close":"open")+
                " action=200; customer rights unchanged; margin check enabled");
        }
    } catch (const Stopping&) { throw; }
      catch (const std::exception& e) {
        return not_submitted(MT_RET_ERR_PARAMS,e.what());
    }
    // Correlate the exact action/dealer before entering the asynchronous SDK.
    refusals_.Register(intent.id,action,dealer);
    const auto result=Call([&](IMTServerAPI* api) { return api->TradeRequest(request.get()); });
    return {result==MT_RET_OK?Submission::Status::Queued:Submission::Status::Unknown,request->ID(),result};
}
Result Mt5Port::Reconcile(const Intent& intent) {
    struct Measure {
        HistoryMetrics& metrics;
        std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
        explicit Measure(HistoryMetrics& value):metrics(value) { ++metrics.reconcile_calls; }
        ~Measure() {
            metrics.reconcile_us+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now()-started).count());
        }
    } measure(history_metrics_);
    if (const auto refusal=refusals_.Read(intent,Wide(intent.symbol))) return *refusal;
    const auto& orders=Orders(intent.follower_login,intent.created_ms);
    if (orders.index.HasNullRows()) return Uncertain("null_order_history_row");
    const IMTOrder* matched=nullptr;
    for (const auto n:HistoryCandidates(orders.index,intent.id)) {
        const auto* order=orders.rows->Next(n);
        if (!order) return Uncertain("null_order_history_row");
        if (ApiValue(order,app_,1)!=kCopierMarker || ApiValue(order,app_,2)!=intent.id ||
            ApiValue(order,app_,3)!=server_) continue;
        if (matched || order->Login()!=intent.follower_login || Utf8(order->Symbol())!=intent.symbol ||
            order->Type()!=Action(intent.side) || order->VolumeInitialExt()!=intent.units ||
            (intent.close && order->PositionID()!=intent.follower_position)) return Uncertain("duplicate_or_mismatched_order_identity");
        matched=order;
    }
    if (!matched || !Final(matched)) {
        Result waiting;
        waiting.reason=matched?"order_not_final":"no_matching_order_history";
        return waiting;
    }
    const auto& deals=Deals(intent.follower_login,intent.created_ms);
    if (deals.index.HasNullRows()) return Uncertain("null_deal_history_row");
    Result result;
    std::int64_t first_deal_ms=0,last_deal_ms=0;
    bool complete_deal_times=true;
    std::set<Id> unique;
    for (const auto n:HistoryCandidates(deals.index,matched->Order())) {
        const auto* d=deals.rows->Next(n);
        if (!d) return Uncertain("null_deal_history_row");
        if (d->Order()!=matched->Order()) continue;
        if (d->Action()!=IMTDeal::DEAL_BUY && d->Action()!=IMTDeal::DEAL_SELL) continue;
        if (d->Login()!=intent.follower_login || Utf8(d->Symbol())!=intent.symbol ||
            d->Action()!=Action(intent.side) || d->Entry()!=(intent.close?IMTDeal::ENTRY_OUT:IMTDeal::ENTRY_IN) ||
            ApiValue(d,app_,1)!=kCopierMarker || ApiValue(d,app_,2)!=intent.id ||
            ApiValue(d,app_,3)!=server_ || !d->Deal() || !unique.insert(d->Deal()).second ||
            !d->PositionID() || (result.position && result.position!=d->PositionID()) ||
            !Add(result.units,d->VolumeExt())) return Uncertain("mismatched_deal_identity_or_volume");
        result.position=d->PositionID(); result.deals.push_back(d->Deal());
        const auto time=d->TimeMsc();
        if (time<=0) complete_deal_times=false;
        else {
            if (!first_deal_ms || time<first_deal_ms) first_deal_ms=time;
            if (time>last_deal_ms) last_deal_ms=time;
        }
    }
    if (matched->VolumeCurrentExt()>matched->VolumeInitialExt()) return Uncertain("order_remaining_exceeds_initial_volume");
    const auto expected=matched->VolumeInitialExt()-matched->VolumeCurrentExt();
    if (result.units!=expected) {
        result.reason="deal_volume_not_yet_equal_to_final_order";
        return result; // Waiting: preserve partial evidence, never declare rejection.
    }
    if (!result.units) {
        if (matched->State()==IMTOrder::ORDER_STATE_FILLED) return Uncertain("filled_order_without_deal_volume");
        result.status=Result::Status::Rejected;
        result.retcode=MT_RET_REQUEST_REJECT;
        result.reason="final_order_without_fill";
    } else {
        result.status=Result::Status::Filled;
        // Diagnostic only: unknown timestamps remain zero and never change the
        // existing order/deal identity, fill, or rejection decision.
        if (complete_deal_times) {
            result.first_deal_ms=first_deal_ms; result.last_deal_ms=last_deal_ms;
        }
        result.retcode=result.units==intent.units?MT_RET_REQUEST_DONE:MT_RET_REQUEST_DONE_PARTIAL;
        result.reason=result.units==intent.units?"complete_order_and_deal_evidence":"final_order_partially_filled";
    }
    return result;
}
} // namespace tradecopier::execution

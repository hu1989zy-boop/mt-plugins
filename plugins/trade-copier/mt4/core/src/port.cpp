#include "mt4copier/port.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mt4copier {
namespace e=tradecopier::execution;
namespace {
[[noreturn]] void Fail(const std::string& why) { throw std::runtime_error("mt4_"+why); }
[[noreturn]] void Incomplete(const std::string& why) { throw IncompleteSnapshot("mt4_"+why); }
struct References { Id from=0,to=0; };
References Refs(const Order& o) {
    References r;
    for (const auto& entry:{std::pair<const char*,Id*>{"from #",&r.from},{"to #",&r.to}}) {
        const std::string prefix=entry.first;
        if(o.comment.compare(0,prefix.size(),prefix)!=0) continue;
        const auto number=o.comment.substr(prefix.size());
        if(number.empty() || number.size()>10 || number.front()=='0' ||
           number.find_first_not_of("0123456789")!=std::string::npos) Fail("malformed_lineage_comment");
        const auto id=std::stoull(number);
        if(id>static_cast<Id>(std::numeric_limits<std::int32_t>::max()) || id==o.ticket) Fail("invalid_lineage_ticket");
        *entry.second=id;
    }
    return r;
}
Side Opposite(Side side) { return side==Side::Buy?Side::Sell:Side::Buy; }
bool Same(const Order& a,const Order& b) {
    return a.login==b.login && a.symbol==b.symbol && a.side==b.side &&
        a.open_ms==b.open_ms && a.open_price==b.open_price && a.digits==b.digits;
}
void Valid(const Order& o) {
    if(!o.ticket || !o.login || o.ticket>static_cast<Id>(std::numeric_limits<std::int32_t>::max()) ||
       o.login>static_cast<Id>(std::numeric_limits<std::int32_t>::max()) || o.symbol.empty() || o.open_ms<=0 || o.volume<=0 ||
       !std::isfinite(o.open_price) || o.open_price<=0 || o.digits>12 || o.close_ms<0 ||
       (o.close_ms && o.close_ms<o.open_ms)) Fail("invalid_order_record");
    if(o.state==2 || o.state==5 || o.state==6 || o.state<0 || o.state>6) Fail("unsupported_order_state");
    if((o.close_ms==0)!=(o.state==0 || o.state==1)) Fail("order_state_time_mismatch");
}
bool AfterSubmission(const Order& o,const e::Intent& i) {
    // MT4 order timestamps have one-second precision; never compare them to
    // millisecond creation times without accounting for this representation.
    return (i.close?o.close_ms:o.open_ms)>=i.created_ms-i.created_ms%1000;
}
e::Result Uncertain(const std::string& why) { e::Result r; r.status=e::Result::Status::Uncertain; r.reason=why; return r; }
}

Port::Port(Broker& broker,std::uint32_t instance):broker_(broker),instance_(instance) {
    if(!instance) Fail("zero_instance_identity");
}
Units Port::ToUnits(std::int32_t volume) {
    if(volume<=0) Fail("invalid_volume");
    return static_cast<Units>(volume)*kUnitsPerMt4Volume;
}
std::int32_t Port::ToVolume(Units units) {
    if(!units || units%kUnitsPerMt4Volume || units/kUnitsPerMt4Volume>static_cast<Units>(std::numeric_limits<std::int32_t>::max()))
        Fail("unrepresentable_volume");
    return static_cast<std::int32_t>(units/kUnitsPerMt4Volume);
}
std::string Port::Token(Id id) const {
    if(!id) Fail("zero_intent");
    std::ostringstream s; s<<"TC4-"<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(8)<<instance_<<'-'<<std::setw(16)<<id;
    return s.str();
}
bool Port::IsCopierToken(const std::string& s) {
    if(s.size()!=29 || s.compare(0,4,"TC4-") || s[12]!='-') return false;
    for(std::size_t n=4;n<s.size();++n) if(n!=12 && (s[n]<'0'||s[n]>'9') && (s[n]<'A'||s[n]>'F')) return false;
    return true;
}

Chain Port::ResolveChain(const std::vector<Order>& orders,Id ticket) {
    std::map<Id,const Order*> records;
    for(const auto& o:orders) if(!records.emplace(o.ticket,&o).second) Fail("duplicate_order_ticket");
    if(!records.count(ticket)) Incomplete("order_history_missing");
    // Follow explicit ticket references only. Metadata never creates an edge.
    const auto predecessor=[&](Id id) {
        const auto found=records.find(id); if(found==records.end()) Incomplete("lineage_record_missing");
        const auto self=Refs(*found->second);
        std::set<Id> parents;
        if(self.from) parents.insert(self.from);
        for(const auto& [other,o]:records) {
            if(o->comment=="to #"+std::to_string(id)) parents.insert(other);
        }
        if(parents.size()>1) Fail("lineage_multiple_parents");
        return parents.empty()?Id{0}:*parents.begin();
    };
    Id root=ticket; std::set<Id> visited;
    while(true) {
        if(!visited.insert(root).second) Fail("lineage_cycle");
        const Id parent=predecessor(root); if(!parent) break;
        root=parent;
    }
    Chain result; result.root=root; result.opening=*records.at(root); visited.clear();
    Id current=root;
    for(;;) {
        if(!visited.insert(current).second) Fail("lineage_cycle");
        const auto it=records.find(current); if(it==records.end()) Incomplete("lineage_record_missing");
        const Order& o=*it->second; Valid(o);
        if(!Same(result.opening,o)) Fail("lineage_identity_mismatch");
        const auto ref=Refs(o);
        if(current==root && (ref.from || o.state==1)) Fail("lineage_root_missing");
        std::set<Id> children;
        if(ref.to) children.insert(ref.to);
        for(const auto& [other,candidate]:records) if(candidate->comment=="from #"+std::to_string(current)) children.insert(other);
        if(children.size()>1) Fail("lineage_branch");
        const Units units=ToUnits(o.volume);
        if(result.initial>std::numeric_limits<Units>::max()-units) Fail("lineage_volume_overflow");
        result.initial+=units; result.records.push_back(o);
        if(children.empty()) {
            if(o.state==4 || ref.to) Incomplete("partial_close_remainder_missing");
            result.current=o.close_ms?0:o.ticket; result.remaining=o.close_ms?0:units;
            break;
        }
        const Id child=*children.begin();
        if(o.state!=4 || !o.close_ms || ref.to!=child) Fail("lineage_forward_evidence_missing");
        const auto next=records.find(child); if(next==records.end()) Incomplete("lineage_record_missing");
        const auto next_ref=Refs(*next->second);
        if(next_ref.from!=current) {
            // A subsequent close replaces the remainder's "from" comment.
            // Its original ticket is still explicitly named by the parent's
            // "to"; only a later partial-close link or our full-close token
            // may replace the reciprocal reference.
            if(next_ref.from || !next->second->close_ms ||
               (!(next->second->state==4 && next_ref.to) &&
                !(next->second->state==3 && IsCopierToken(next->second->comment)))) Fail("lineage_backward_evidence_missing");
        }
        if(predecessor(child)!=current || (next->second->close_ms && next->second->close_ms<o.close_ms))
            Fail("lineage_order_mismatch");
        current=child;
    }
    if(!visited.count(ticket)) Fail("lineage_target_unreachable");
    return result;
}

std::int64_t Port::NowMs() { return broker_.NowMs(); }
void Port::BeginCycle() { orders_.clear(); }
const std::vector<Order>& Port::Orders(Id login) {
    auto it=orders_.find(login);
    if(it==orders_.end()) {
        auto records=broker_.Orders(login,0,NowMs());
        for(const auto& record:records) if(record.login!=login) Fail("orders_account_mismatch");
        it=orders_.emplace(login,std::move(records)).first;
    }
    return it->second;
}
Account Port::CheckedAccount(Id login) {
    auto account=broker_.AccountInfo(login);
    if(account.currency!="USD" || account.currency_digits!=2 || !account.enabled) Fail("unsupported_account");
    if(account.close_reopen) Fail("close_reopen_unsupported");
    return account;
}
Symbol Port::FindSymbol(Id login,const std::string& symbol) {
    const auto list=broker_.Symbols(login); std::optional<Symbol> found;
    for(const auto& candidate:list) if(candidate.spec.name==symbol) {
        if(found) Fail("duplicate_symbol"); found=candidate;
    }
    if(!found) Fail("symbol_missing");
    if(!found->market_execution) Fail("non_market_execution");
    if(!tradecopier::ValidLimits(found->limits) || found->limits.minimum%kUnitsPerMt4Volume ||
       found->limits.step%kUnitsPerMt4Volume || found->limits.maximum%kUnitsPerMt4Volume) Fail("invalid_symbol_volume_limits");
    return *found;
}
std::optional<e::Deal> Port::Opening(Id login,Id ticket) {
    CheckedAccount(login);
    const auto chain=ResolveChain(Orders(login),ticket);
    if(chain.root!=ticket || chain.records.size()!=1 || chain.opening.close_ms || chain.opening.state!=0) return {};
    e::Deal d; d.id=d.order=d.position=chain.root; d.login=login; d.symbol=chain.opening.symbol;
    d.side=chain.opening.side; d.units=chain.initial; d.time_ms=chain.opening.open_ms;
    d.opening=true; d.copier_owned=IsCopierToken(chain.opening.comment);
    return d;
}
e::BootSnapshot Port::Snapshot(const std::vector<Id>& logins,Id sequence) {
    BeginCycle(); e::BootSnapshot snapshot; snapshot.time_ms=NowMs(); snapshot.sequence=sequence;
    std::set<std::pair<Id,Id>> unique;
    for(Id login:logins) {
        CheckedAccount(login);
        for(const auto& o:Orders(login)) if(!o.close_ms) unique.emplace(login,ResolveChain(Orders(login),o.ticket).root);
    }
    snapshot.positions.assign(unique.begin(),unique.end()); return snapshot;
}
e::SourceView Port::ReadSource(const e::Deal& opening) {
    e::SourceView view;
    try {
        CheckedAccount(opening.login);
        const auto chain=ResolveChain(Orders(opening.login),opening.position);
        if(chain.root!=opening.position || chain.opening.symbol!=opening.symbol || chain.opening.side!=opening.side ||
           chain.opening.open_ms!=opening.time_ms || opening.order!=chain.root) Fail("source_identity_changed");
        view.opening_units=chain.initial; view.remaining=chain.remaining; view.first_fill_ms=chain.opening.open_ms;
        for(const auto& o:chain.records) if(o.close_ms) view.closes.push_back({o.ticket,ToUnits(o.volume),o.close_ms});
        std::sort(view.closes.begin(),view.closes.end(),[](const auto& a,const auto& b) {return std::tie(a.time_ms,a.deal)<std::tie(b.time_ms,b.deal);});
        view.state=e::ReadState::Ready;
    } catch(const IncompleteSnapshot& error) { view.state=e::ReadState::Waiting; view.reason=error.what(); }
      catch(const std::exception& error) { view.state=e::ReadState::Invalid; view.reason=error.what(); }
    return view;
}
tradecopier::SymbolResolution Port::ResolveSymbol(const tradecopier::RuleConfig& rule,const std::string& source,Side side) {
    CheckedAccount(rule.source_login); CheckedAccount(rule.follower_login);
    const auto src=FindSymbol(rule.source_login,source); std::vector<tradecopier::SymbolSpec> targets;
    for(const auto& target:broker_.Symbols(rule.follower_login)) if(target.market_execution) targets.push_back(target.spec);
    return tradecopier::ResolveTargetSymbol(src.spec,targets,rule.auto_symbol,rule.source_suffix,side==Side::Buy);
}
tradecopier::VolumeLimits Port::Limits(const tradecopier::RuleConfig& rule,const std::string& source,const std::string& target,Side side,bool close) {
    CheckedAccount(rule.source_login); CheckedAccount(rule.follower_login);
    const auto src=FindSymbol(rule.source_login,source); const auto dst=FindSymbol(rule.follower_login,target);
    if(!tradecopier::SameSymbolContract(src.spec,dst.spec)) Fail("symbol_contract_mismatch");
    if(close ? !dst.close_allowed : !(side==Side::Buy?dst.spec.buy:dst.spec.sell)) Fail("symbol_direction_disabled");
    return dst.limits;
}
e::EquitySnapshot Port::Equities(const tradecopier::RuleConfig& rule) {
    const auto source=CheckedAccount(rule.source_login), target=CheckedAccount(rule.follower_login);
    if(!source.equity || !target.equity || source.query_ms<=0 || target.query_ms<=0) Fail("invalid_equity");
    if(target.query_ms<source.query_ms || target.query_ms-source.query_ms>5000) Fail("equity_query_freshness");
    return {source.equity,target.equity,2,source.query_ms,target.query_ms};
}
Units Port::FollowerRemaining(Id login,Id position,const std::string& symbol,Side side) {
    CheckedAccount(login); const auto chain=ResolveChain(Orders(login),position);
    if(chain.root!=position || chain.opening.symbol!=symbol || chain.opening.side!=side) Fail("follower_identity_changed");
    return chain.remaining;
}
e::Submission Port::Submit(const e::Intent& intent) {
    if(requests_.count(intent.id)) Fail("intent_already_submitted");
    CheckedAccount(intent.follower_login); const auto symbol=FindSymbol(intent.follower_login,intent.symbol);
    Request request; request.intent=intent.id; request.login=intent.follower_login; request.close=intent.close;
    request.position_side=intent.close?Opposite(intent.side):intent.side; request.symbol=intent.symbol;
    request.volume=ToVolume(intent.units); request.token=Token(intent.id);
    if(intent.close ? !symbol.close_allowed : !(request.position_side==Side::Buy?symbol.spec.buy:symbol.spec.sell)) Fail("symbol_direction_disabled");
    if(intent.units<symbol.limits.minimum || intent.units>symbol.limits.maximum || intent.units%symbol.limits.step) Fail("request_volume_limits");
    if(intent.close) {
        const auto chain=ResolveChain(Orders(intent.follower_login),intent.follower_position);
        if(chain.root!=intent.follower_position || !chain.current || chain.remaining!=intent.follower_before ||
           intent.units>chain.remaining || chain.opening.symbol!=intent.symbol || chain.opening.side!=request.position_side)
            Fail("close_snapshot_changed");
        request.ticket=chain.current;
        request.expected_remaining=ToVolume(intent.follower_before);
    }
    if(intent.equity.source_query_ms) {
        const auto now=NowMs();
        if(now<intent.equity.source_query_ms || now-intent.equity.source_query_ms>5000)
            return {e::Submission::Status::NotSubmitted,0,128}; // RET_TRADE_TIMEOUT; no SDK call happened
    }
    const auto result=broker_.Submit(request);
    if(result.request_id) requests_[intent.id]=result.request_id;
    if(result.status==e::Submission::Status::Queued && result.request_id)
        accepted_[intent.id]={result.request_id,request};
    orders_.erase(intent.follower_login);
    return result;
}
void Port::ObserveRequest(const RequestEvidence& evidence) {
    if(!evidence.request_id || !IsCopierToken(evidence.token) || evidence.token.substr(0,13)!=Token(1).substr(0,13)) return;
    const auto tracked=std::find_if(requests_.begin(),requests_.end(),[&](const auto& item) {
        return item.second==evidence.request_id && Token(item.first)==evidence.token;
    });
    if(tracked==requests_.end()) return;
    auto& list=evidence_[evidence.request_id];
    // Repeated callbacks are expected; keep a bounded list. Overflow remains
    // contradictory evidence and cannot cause a false successful resolution.
    const auto duplicate=std::find_if(list.begin(),list.end(),[&](const auto& x) {
        return x.kind==evidence.kind && x.retcode==evidence.retcode && x.login==evidence.login && x.ticket==evidence.ticket &&
            x.close==evidence.close && x.position_side==evidence.position_side && x.symbol==evidence.symbol &&
            x.token==evidence.token && x.volume==evidence.volume && x.time_ms==evidence.time_ms;
    });
    if(duplicate==list.end()) {
        if(list.size()<16) list.push_back(evidence);
        else contradictory_.insert(evidence.request_id);
    }
    orders_.erase(evidence.login);
}
e::Result Port::Reconcile(const e::Intent& intent) {
    try {
        if(contradictory_.count(intent.request_id)) return Uncertain("mt4_callback_evidence_overflow");
        CheckedAccount(intent.follower_login);
        const auto token=Token(intent.id); const auto side=intent.close?Opposite(intent.side):intent.side;
        const auto& orders=Orders(intent.follower_login);
        const Request* accepted_close=nullptr;
        const auto accepted=accepted_.find(intent.id);
        if(intent.close && accepted!=accepted_.end() && accepted->second.first==intent.request_id) {
            const auto& request=accepted->second.second;
            if(!request.close || !request.ticket || request.login!=intent.follower_login || request.symbol!=intent.symbol ||
               request.position_side!=side || request.token!=token || request.volume!=ToVolume(intent.units) ||
               request.expected_remaining!=ToVolume(intent.follower_before)) return Uncertain("mt4_accepted_close_identity_mismatch");
            accepted_close=&request;
        }
        std::vector<const RequestEvidence*> proof;
        const auto entry=evidence_.find(intent.request_id);
        if(entry!=evidence_.end()) for(const auto& x:entry->second) {
            if(x.token!=token) continue;
            if(x.login!=intent.follower_login || x.close!=intent.close || x.position_side!=side || x.symbol!=intent.symbol ||
               x.volume!=ToVolume(intent.units) || x.time_ms<intent.created_ms-intent.created_ms%1000)
                return Uncertain("mt4_request_evidence_mismatch");
            proof.push_back(&x);
        }
        const auto rejected=std::find_if(proof.begin(),proof.end(),[](const auto* x){return x->kind==RequestEvidence::Kind::Rejected;});
        const bool filled=std::any_of(proof.begin(),proof.end(),[](const auto* x){return x->kind==RequestEvidence::Kind::Filled;});
        std::set<Id> matches;
        for(const auto& o:orders) if(o.comment==token) matches.insert(o.ticket);
        if(rejected!=proof.end()) {
            if(filled || !matches.empty() || (*rejected)->ticket) return Uncertain("mt4_conflicting_rejection");
            for(const auto* x:proof) if(x->retcode!=(*rejected)->retcode || x->ticket) return Uncertain("mt4_conflicting_rejection");
            e::Result r; r.status=e::Result::Status::Rejected; r.retcode=(*rejected)->retcode;
            r.reason=r.retcode==134?"server_pretrade_no_money_zero_fill":"mt4_exact_request_rejected"; return r;
        }
        for(const auto* x:proof) if(x->kind==RequestEvidence::Kind::Filled) {
            if(!x->ticket) return Uncertain("mt4_result_ticket_missing");
            matches.insert(x->ticket);
        }
        bool close_target_settled=false;
        if(accepted_close) {
            const auto target=std::find_if(orders.begin(),orders.end(),[&](const auto& row){return row.ticket==accepted_close->ticket;});
            if(target!=orders.end() && target->close_ms && (target->state==3 || target->state==4)) {
                // This establishes completion of the requested reduction, not
                // which actor caused it. Ordinary MK_CLOSE keeps its original
                // order ticket: an outstanding request cannot close a later
                // remainder ticket after this exact target is terminal.
                matches.insert(target->ticket); close_target_settled=true;
            }
        }
        if(matches.empty()) {
            e::Result waiting; waiting.reason="mt4_no_matching_order_or_execution_callback"; return waiting;
        }
        if(matches.size()!=1) return Uncertain("mt4_multiple_request_results");
        const Id ticket=*matches.begin();
        const auto chain=ResolveChain(orders,ticket);
        const auto row=std::find_if(chain.records.begin(),chain.records.end(),[&](const auto& o){return o.ticket==ticket;});
        if(row==chain.records.end() || row->login!=intent.follower_login || row->symbol!=intent.symbol || row->side!=side || !AfterSubmission(*row,intent))
            return Uncertain("mt4_result_order_identity_mismatch");
        e::Result r; r.status=e::Result::Status::Filled; r.position=chain.root; r.deals={ticket};
        if(intent.close) {
            if(chain.root!=intent.follower_position || !row->close_ms || intent.follower_before<intent.units ||
               chain.remaining>intent.follower_before-intent.units) return Uncertain("mt4_close_history_mismatch");
            if(accepted_close && ticket!=accepted_close->ticket) return Uncertain("mt4_close_actual_target_mismatch");
            r.units=ToUnits(row->volume);
        } else {
            if(chain.root!=ticket) return Uncertain("mt4_open_result_is_remainder");
            r.units=chain.initial;
        }
        if(r.units!=intent.units) return Uncertain("mt4_result_volume_mismatch");
        if(!filled && !close_target_settled) {
            e::Result waiting; waiting.reason="mt4_awaiting_exact_request_callback"; return waiting;
        }
        if(close_target_settled && !filled) r.reason="mt4_exact_close_target_settled";
        return r;
    } catch(const IncompleteSnapshot& error) { e::Result waiting; waiting.reason=error.what(); return waiting; }
      catch(const std::exception& error) { return Uncertain(error.what()); }
}
void Port::ForgetRequest(Id id) noexcept {
    accepted_.erase(id);
    const auto it=requests_.find(id); if(it!=requests_.end()) { evidence_.erase(it->second); contradictory_.erase(it->second); requests_.erase(it); }
}
void Port::Log(const std::string& text) { broker_.Log(text); }
}

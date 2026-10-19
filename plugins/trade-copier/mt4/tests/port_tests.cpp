#include "mt4copier/port.hpp"
#include "tradecopier/journal.hpp"
#include "../server/dealer_execution.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace m=mt4copier;
namespace e=tradecopier::execution;
using tradecopier::Id;
using tradecopier::Units;
namespace {
unsigned checks=0;
void Check(bool ok,const char* why) { ++checks; if(!ok) throw std::runtime_error(why); }
template<class F> void Throws(F&& f,const char* why) { bool thrown=false;try{f();}catch(const std::exception&){thrown=true;}Check(thrown,why); }
Units V(int volume) {return m::Port::ToUnits(volume);}
m::Order Order(Id ticket,Id login,int volume,std::int64_t at=2000) {
    m::Order o; o.ticket=ticket;o.login=login;o.volume=volume;o.symbol="EURUSD";
    o.open_ms=at;o.open_price=1.125;o.digits=5;return o;
}
m::Symbol Symbol(const std::string& name="EURUSD") {
    m::Symbol s;s.spec.name=name;s.spec.basis="EURUSD";s.spec.contract_size=100000;
    s.spec.currency_base="EUR";s.spec.currency_profit=s.spec.currency_margin="USD";s.spec.digits=5;
    s.spec.buy=s.spec.sell=true;s.market_execution=s.close_allowed=true;s.limits={V(1),V(100000),V(1)};return s;
}
struct Fake final:m::Broker {
    std::int64_t now=1000; Id next_ticket=10000; std::uint32_t next_request=1;
    std::map<Id,std::vector<m::Order>> orders;
    std::map<Id,m::Account> accounts;
    std::map<Id,std::vector<m::Symbol>> symbols;
    std::vector<std::pair<std::uint32_t,m::Request>> sent;
    std::vector<std::string> logs;
    std::set<std::uint32_t> settled;
    std::map<Id,e::Submission::Status> acknowledgements;
    bool execute_dealer = false, dealer_confirm_throws = false;
    int dealer_lock_result = 1, dealer_confirm_result = 0;
    unsigned dealer_lock_calls = 0, dealer_confirm_calls = 0, dealer_release_calls = 0;
    std::vector<m::DealerExecutionOutcome> dealer_outcomes;
    std::int64_t NowMs() override{return now;}
    std::vector<m::Order> Orders(Id login,std::int64_t from,std::int64_t) override {
        Check(from==0,"lineage query includes complete history");return orders[login];
    }
    m::Account AccountInfo(Id login) override {
        auto it=accounts.find(login);if(it!=accounts.end())return it->second;
        return {"USD",login==100?100000u:50000u,2,now,true,false};
    }
    std::vector<m::Symbol> Symbols(Id login) override {
        auto it=symbols.find(login);return it==symbols.end()?std::vector<m::Symbol>{Symbol()}:it->second;
    }
    e::Submission Submit(const m::Request& request) override {
        const auto id=next_request++;sent.emplace_back(id,request);
        auto it=acknowledgements.find(request.login);
        const auto status=it==acknowledgements.end()?e::Submission::Status::Queued:it->second;
        if(execute_dealer) dealer_outcomes.push_back(m::ExecuteAcceptedDealerRequest(
            status==e::Submission::Status::Queued,id,2016,
            [&](int,int) {
                ++dealer_lock_calls;
                return dealer_lock_result;
            },
            [&](int,int) {
                ++dealer_confirm_calls;
                if(dealer_confirm_throws) throw std::runtime_error("native confirmation outcome unknown");
                return dealer_confirm_result;
            },
            [&](int,int) {
                ++dealer_release_calls;
                return 1;
            }));
        return {status,id,0};
    }
    void Log(const std::string& line) override {logs.push_back(line);}
    Id Close(Id login,Id ticket,int volume,const std::string& final_comment={}) {
        auto& list=orders[login];auto it=std::find_if(list.begin(),list.end(),[&](const auto& o){return o.ticket==ticket;});
        if(it==list.end() || it->close_ms || volume>it->volume || volume<=0) throw std::runtime_error("invalid fake close");
        const auto before=*it;it->volume=volume;it->close_ms=now;
        if(volume==before.volume) {it->state=3;if(!final_comment.empty())it->comment=final_comment;return 0;}
        const Id child=next_ticket++;it->state=4;it->comment="to #"+std::to_string(child);
        auto remainder=before;remainder.ticket=child;remainder.volume-=volume;remainder.state=1;
        remainder.comment="from #"+std::to_string(ticket);list.push_back(remainder);return child;
    }
    void Fill(std::size_t index,m::Port& port,bool evidence=true) {
        const auto [id,request]=sent.at(index);if(!settled.insert(id).second)throw std::runtime_error("duplicate fake fill");
        Id result=request.ticket;
        if(request.close) Close(request.login,request.ticket,request.volume,request.token);
        else {
            result=next_ticket++;auto o=Order(result,request.login,request.volume,now);
            o.side=request.position_side;o.symbol=request.symbol;o.comment=request.token;orders[request.login].push_back(o);
        }
        if(evidence) {
            m::RequestEvidence x;x.request_id=id;x.login=request.login;x.ticket=result;x.close=request.close;
            x.position_side=request.position_side;x.symbol=request.symbol;x.token=request.token;x.volume=request.volume;x.time_ms=now;
            port.ObserveRequest(x);
        }
    }
};
tradecopier::CopierConfig Config(tradecopier::SizingMode mode=tradecopier::SizingMode::LotRatio,const std::string& parameter="0.5",bool second=false) {
    tradecopier::CopierConfig c;c.enabled=true;
    tradecopier::RuleConfig r;r.id=1;r.source_login=100;r.follower_login=200;r.enabled=true;r.mode=mode;r.parameter=parameter;
    c.relationships.push_back(r);if(second){r.id=2;r.follower_login=201;c.relationships.push_back(r);}return c;
}
struct Harness {
    Fake broker;tradecopier::CopierConfig config;
    std::filesystem::path directory;
    std::unique_ptr<m::Port> port;
    std::unique_ptr<e::FileJournal> journal;
    std::unique_ptr<e::Engine> engine;
    explicit Harness(tradecopier::CopierConfig c=Config()):config(std::move(c)) {
        static unsigned serial=0;
        directory=std::filesystem::temp_directory_path()/("mt4-port-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        std::filesystem::create_directory(directory);Start();
    }
    ~Harness(){engine.reset();journal.reset();port.reset();std::error_code error;std::filesystem::remove_all(directory,error);}
    void Start() {
        port=std::make_unique<m::Port>(broker,42);journal=std::make_unique<e::FileJournal>(directory/"state.journal");
        engine=std::make_unique<e::Engine>(config,"mt4-test-server-instance42",*port,*journal);
        engine->Start(port->Snapshot({100},0));
    }
    void Restart(){engine.reset();journal.reset();port.reset();Start();}
    void Admit(Id ticket=1,int volume=100) {
        broker.now+=1000;broker.orders[100].push_back(Order(ticket,100,volume,broker.now));
        port->BeginCycle();const auto opening=port->Opening(100,ticket);Check(opening.has_value(),"source opening produced");
        engine->Admit(*opening,ticket);engine->Tick();
    }
    void Fill(std::size_t index){broker.Fill(index,*port);engine->ConfirmPending();engine->Tick();}
};

void VolumeAndLineage() {
    Check(V(100)==tradecopier::kUnitsPerLot,"MT4 integer lot scale");
    Check(m::Port::ToVolume(V(37))==37,"exact reverse lot conversion");
    Throws([]{m::Port::ToVolume(1);},"fractional native unit rejected");
    Throws([]{m::Port::ToUnits(-1);},"negative native volume rejected");
    Fake f;f.orders[100]={Order(1,100,100)};f.now=3000;
    const Id second=f.Close(100,1,20);f.now=4000;const Id third=f.Close(100,second,30);
    auto chain=m::Port::ResolveChain(f.orders[100],third);
    Check(chain.root==1 && chain.current==third && chain.initial==V(100) && chain.remaining==V(50),"multiple partial closes retain root and conserve volume");
    auto broken=f.orders[100];broken.back().comment="from #999";
    Throws([&]{m::Port::ResolveChain(broken,1);},"missing reciprocal ancestry rejected");
    broken=f.orders[100];auto branch=broken.back();branch.ticket=50000;broken.push_back(branch);
    Throws([&]{m::Port::ResolveChain(broken,1);},"branch rejected");
    broken=f.orders[100];broken.back().open_price+=0.001;
    Throws([&]{m::Port::ResolveChain(broken,1);},"same symbol alone cannot establish lineage");
    broken=f.orders[100];broken.back().login=101;
    Throws([&]{m::Port::ResolveChain(broken,1);},"cross-account lineage rejected");
    broken=f.orders[100];broken.back().state=5;
    Throws([&]{m::Port::ResolveChain(broken,1);},"CloseBy rejected");
    broken=f.orders[100];broken.pop_back();
    Throws([&]{m::Port::ResolveChain(broken,1);},"missing history never treated as zero");
    m::Port p(f,42);Check(!p.Opening(100,third),"remainder callback cannot create opening");
    auto boot=p.Snapshot({100},50);Check(boot.positions==std::vector<std::pair<Id,Id>>{{100,1}},"baseline stores root ticket");
    f.now=5000;f.Close(100,third,50);p.BeginCycle();
    Check(!p.Opening(100,1),"historically closed root is not a new opening");
    e::Deal opening;opening.id=opening.order=opening.position=1;opening.login=100;opening.symbol="EURUSD";
    opening.units=V(100);opening.time_ms=2000;opening.opening=true;
    const auto view=p.ReadSource(opening);
    Check(view.state==e::ReadState::Ready && view.remaining==0 && view.closes.size()==3,"source history contains each actual closed amount");
    f.orders[100].pop_back();p.BeginCycle();
    Check(p.ReadSource(opening).state==e::ReadState::Waiting,"incomplete source chain waits instead of permanently reviewing immediately");
}
void Sizing() {
    for(const auto& item:std::vector<std::tuple<tradecopier::SizingMode,std::string,int>>{
        {tradecopier::SizingMode::LotRatio,"0.5",50},{tradecopier::SizingMode::EquityRatio,"1",50},{tradecopier::SizingMode::FixedLots,"0.03",3}}) {
        Harness h(Config(std::get<0>(item),std::get<1>(item)));h.Admit();
        Check(h.broker.sent.size()==1 && h.broker.sent[0].second.volume==std::get<2>(item),"shared engine sizing matches MT5 rules");
        Check(h.broker.sent[0].second.token.size()==29,"request token fits MT4 comment");
        h.Fill(0);Check(h.engine->state().links.at(0).phase==e::Phase::Active,"history and correlated fill activate mapping");
    }
}
void PartialManualAndRestart() {
    Harness h;h.Admit();h.Fill(0);const Id root=h.engine->state().links.at(0).follower_position;
    h.broker.now=3000;const Id after_manual=h.broker.Close(200,root,10);h.engine->Tick();
    Check(h.engine->state().links.at(0).follower_remaining==V(40),"manual partial close updates actual target remainder");
    h.broker.now=4000;const Id source_remainder=h.broker.Close(100,1,25);h.engine->Tick();
    Check(h.broker.sent.size()==2 && h.broker.sent[1].second.volume==10,"25 percent source close uses actual 40 remainder");
    Check(h.broker.sent[1].second.ticket==after_manual && h.broker.sent[1].second.position_side==m::Side::Buy,"close targets actual ticket using original MT4 direction");
    Check(h.broker.sent[1].second.expected_remaining==40,"SDK submission contract carries exact expected remaining volume");
    h.Fill(1);Check(h.engine->state().links.at(0).follower_position==root,"journal position remains original root");
    h.broker.now=5000;h.Restart();
    Check(h.engine->state().links.at(0).phase==e::Phase::Active && h.broker.sent.size()==2,"settled chain restarts without duplicate submission");
    h.broker.now=6000;h.broker.Close(100,source_remainder,75);h.engine->Tick();
    Check(h.broker.sent.size()==3 && h.broker.sent[2].second.volume==30,"full source close closes entire real remainder");
    h.Fill(2);Check(h.port->FollowerRemaining(200,root,"EURUSD",m::Side::Buy)==0,"full close keeps root lineage and zero remainder");

    Harness manual;manual.Admit();manual.Fill(0);const Id ticket=manual.engine->state().links.at(0).follower_position;
    manual.broker.now=3000;manual.broker.Close(200,ticket,50);manual.engine->Tick();
    manual.broker.now=4000;manual.broker.Close(100,1,100);manual.engine->Tick();
    Check(manual.broker.sent.size()==1,"manual full close never opens replacement or closes unrelated trade");
}
void PendingAndIsolation() {
    Harness h(Config(tradecopier::SizingMode::LotRatio,"1",true));h.Admit();
    Check(h.broker.sent.size()==2,"one source fans out");
    const auto waiting=h.port->Reconcile(h.engine->state().intents.at(0));
    Check(waiting.status==e::Result::Status::Waiting && waiting.reason=="mt4_no_matching_order_or_execution_callback",
          "accepted request without target order or execution callback remains waiting with an explicit diagnostic");
    h.Fill(1);h.broker.now=33000;h.engine->ConfirmPending();h.engine->Tick();
    Check(h.engine->state().links.at(0).phase==e::Phase::Review && h.engine->state().links.at(1).phase==e::Phase::Active,"missing evidence isolates only affected mapping");
    Check(h.engine->state().links.at(0).reason=="execution_confirmation_timeout" &&
          h.engine->state().intents.at(0).state==e::IntentState::Review,
          "missing evidence remains an execution timeout rather than a claimed rejection");
    Check(std::any_of(h.broker.logs.begin(),h.broker.logs.end(),[](const auto& line) {
        return line.find("ISOLATED_MAPPING intent=1 request=1 source_position=1 target=200 rule=1 reason=execution_confirmation_timeout ") == 0 &&
            line.find("evidence_reason=mt4_no_matching_order_or_execution_callback;")!=std::string::npos;
    }),"timeout log correlates original intent, server request, source position and missing evidence");
    h.broker.now=33500;h.engine->ConfirmPending();h.engine->Tick();
    Check(h.broker.sent.size()==2,"repeated confirmation and worker cycles never resend a timed-out opening");
    h.broker.now=34000;h.broker.Close(100,1,100);h.engine->Tick();
    Check(h.broker.sent.size()==3 && h.broker.sent[2].second.login==201,"healthy target continues source close despite sibling review");
    Harness pending;pending.Admit();pending.broker.now=3000;pending.Restart();pending.engine->Tick();
    Check(pending.broker.sent.size()==1 && pending.engine->state().links.at(0).phase==e::Phase::Review,"journal restart never retries unresolved request");
}
void EvidenceAndAccounts() {
    Harness h;h.Admit();auto intent=h.engine->state().intents.at(0);
    Check(h.port->Reconcile(intent).status==e::Result::Status::Waiting,"queue acceptance is not a fill");
    h.broker.orders[200]={Order(900,200,50,h.broker.now)};h.port->BeginCycle();
    Check(h.port->Reconcile(intent).status==e::Result::Status::Waiting,"similar manual order is not attributed");
    h.broker.orders[200][0].comment=h.port->Token(intent.id);h.broker.orders[200][0].volume=49;h.port->BeginCycle();
    Check(h.port->Reconcile(intent).status==e::Result::Status::Uncertain,"matching token wrong actual volume cannot confirm");
    h.broker.orders[200][0].volume=50;h.port->BeginCycle();
    const auto callback_wait=h.port->Reconcile(intent);
    Check(callback_wait.status==e::Result::Status::Waiting && callback_wait.reason=="mt4_awaiting_exact_request_callback",
          "matching order without exact callback stays distinguishable from missing order and callback");
    h.broker.orders[200].clear();h.port->BeginCycle();
    const auto [request_id,request]=h.broker.sent[0];m::RequestEvidence refusal;
    refusal.kind=m::RequestEvidence::Kind::Rejected;refusal.request_id=request_id;refusal.retcode=134;
    refusal.login=200;refusal.symbol=request.symbol;refusal.token=request.token;refusal.volume=request.volume;refusal.time_ms=h.broker.now;
    h.port->ObserveRequest(refusal);auto result=h.port->Reconcile(intent);
    Check(result.status==e::Result::Status::Rejected && result.retcode==134 && result.reason=="server_pretrade_no_money_zero_fill","authoritative exact zero-fill no-money refusal preserved");
    Fake f;m::Port p(f,42);f.accounts[100]={"EUR",1000,2,1000,true,false};
    Throws([&]{p.Snapshot({100},0);},"non-USD source refused");
    f.accounts[100]={"USD",1000,2,1000,true,true};
    Throws([&]{p.Snapshot({100},0);},"full-close-and-reopen accounting mode refused");
    f.accounts.clear();f.symbols[200]={Symbol()};f.symbols[200][0].market_execution=false;
    auto rule=Config().relationships[0];Check(p.ResolveSymbol(rule,"EURUSD",m::Side::Buy).symbol.empty(),"nonmarket target excluded");
}
void DealerAdmissionEvidenceAndRecovery() {
    // Native confirmation is an action on the accepted request, not execution
    // proof. Exercise the real Engine/Port/FileJournal boundary for success,
    // native failure and exceptions, including a lock taken by another dealer.
    for(unsigned scenario=0;scenario<4;++scenario) {
        const auto configure=[&](Fake& broker) {
            broker.execute_dealer=true;
            broker.dealer_confirm_result=scenario==1?134:0;
            broker.dealer_confirm_throws=scenario==2;
            broker.dealer_lock_result=scenario==3?0:1;
        };
        {
            Harness h;configure(h.broker);h.Admit();
            const auto original_id=h.broker.sent.at(0).first;
            const auto& outcome=h.broker.dealer_outcomes.at(0);
            Check(outcome.accepted && outcome.request_id==original_id,"dealer outcome retains original queued identity");
            Check(outcome.failure_phase==(scenario==3?m::DealerFailurePhase::Lock:
                  scenario==0?m::DealerFailurePhase::None:m::DealerFailurePhase::Confirm),
                  "dealer diagnostic records success or failure independently of engine admission");
            Check(h.engine->state().intents.at(0).state==e::IntentState::Submitted &&
                  h.engine->state().intents.at(0).request_id==original_id &&
                  h.engine->state().links.at(0).phase==e::Phase::Opening,
                  "dealer result never erases queued request or invents an opening fill");
            Check(h.broker.orders[200].empty(),"native confirm return alone creates no fake position");
            for(unsigned n=0;n<3;++n) {
                h.broker.now+=1000;h.engine->ConfirmPending();h.engine->Tick();
                h.port->BeginCycle();h.engine->Admit(*h.port->Opening(100,1),1);
            }
            Check(h.broker.sent.size()==1 && h.broker.dealer_outcomes.size()==1 &&
                  h.engine->state().intents.at(0).state==e::IntentState::Submitted,
                  "polling and duplicate source callbacks cannot repeat dealer processing");
            h.broker.now=33000;h.engine->ConfirmPending();h.engine->Tick();
            Check(h.engine->state().intents.at(0).state==e::IntentState::Review &&
                  h.engine->state().intents.at(0).request_id==original_id &&
                  h.engine->state().links.at(0).reason=="execution_confirmation_timeout",
                  "dealer uncertainty times out with original queued ID rather than retry eligibility");
            h.broker.now=34000;h.Restart();h.engine->ConfirmPending();h.engine->Tick();
            Check(h.broker.sent.size()==1 && h.broker.dealer_outcomes.size()==1 &&
                  h.engine->state().intents.at(0).state==e::IntentState::Review &&
                  h.engine->state().intents.at(0).request_id==original_id,
                  "FileJournal restart preserves ambiguous dealer request without resubmitting");
            Check(h.broker.dealer_lock_calls==1 && h.broker.dealer_confirm_calls==(scenario==3?0u:1u) &&
                  h.broker.dealer_release_calls==((scenario==1 || scenario==2)?1u:0u),
                  "whole engine lifecycle keeps one lock/confirm/release attempt at most");
        }
        {
            Harness h;configure(h.broker);h.Admit();
            const auto request_id=h.broker.sent.at(0).first;
            h.broker.now=3000;h.Restart();h.engine->ConfirmPending();h.engine->Tick();
            Check(h.engine->state().intents.at(0).request_id==request_id &&
                  h.engine->state().intents.at(0).state==e::IntentState::Review &&
                  h.engine->state().links.at(0).reason=="restart_unresolved_request",
                  "restart before timeout recovers submitted dealer ID and isolates uncertainty");
            Check(h.broker.sent.size()==1 && h.broker.dealer_outcomes.size()==1,
                  "restart before timeout cannot replay a dealer confirmation or order request");
        }
        {
            Harness h;configure(h.broker);h.Admit();
            const auto [request_id,request]=h.broker.sent.at(0);
            h.broker.now=3000;h.broker.Fill(0,*h.port,false);
            h.engine->ConfirmPending();h.engine->Tick();
            Check(h.engine->state().intents.at(0).state==e::IntentState::Submitted &&
                  h.engine->state().links.at(0).phase==e::Phase::Opening,
                  "history without exact callback cannot settle dealer confirmation");
            m::RequestEvidence proof;
            proof.request_id=request_id+100;proof.login=request.login;proof.ticket=h.broker.orders[200].at(0).ticket;
            proof.close=request.close;proof.position_side=request.position_side;proof.symbol=request.symbol;
            proof.token=request.token;proof.volume=request.volume;proof.time_ms=h.broker.now;
            h.port->ObserveRequest(proof);h.engine->ConfirmPending();h.engine->Tick();
            Check(h.engine->state().intents.at(0).state==e::IntentState::Submitted,
                  "different request callback cannot settle this dealer admission");
            proof.request_id=request_id;h.port->ObserveRequest(proof);h.engine->ConfirmPending();h.engine->Tick();
            Check(h.engine->state().intents.at(0).state==e::IntentState::Confirmed &&
                  h.engine->state().intents.at(0).request_id==request_id &&
                  h.engine->state().links.at(0).phase==e::Phase::Active && h.broker.sent.size()==1,
                  "later exact callback plus matching history settles the original queued request");
            const Id position=h.engine->state().links.at(0).follower_position;
            h.broker.now=4000;h.broker.Close(100,1,100);h.engine->Tick();
            Check(h.broker.sent.size()==2 && h.broker.sent.at(1).second.close &&
                  h.broker.sent.at(1).second.ticket==position,
                  "subsequent dealer close retains exact accepted target ticket");
            Check(h.engine->state().intents.back().state==e::IntentState::Submitted &&
                  h.engine->state().intents.back().request_id==h.broker.sent.at(1).first &&
                  h.port->FollowerRemaining(200,position,"EURUSD",m::Side::Buy)==V(50),
                  "dealer confirmation alone cannot claim a reduction of follower volume");
            h.Fill(1);
            Check(h.port->FollowerRemaining(200,position,"EURUSD",m::Side::Buy)==0 && h.broker.sent.size()==2,
                  "exact close execution completes original request without another dealer submission");
            h.broker.now=5000;h.Restart();h.engine->ConfirmPending();h.engine->Tick();
            Check(h.broker.sent.size()==2 && h.broker.dealer_outcomes.size()==2,
                  "settled dealer opening and closing never repeat after journal recovery");
        }
    }
}
void SymbolAndActivation() {
    Fake f;m::Port p(f,42);auto rule=Config().relationships[0];rule.auto_symbol=true;rule.source_suffix=".a";
    f.symbols[100]={Symbol("EURUSD.a")};f.symbols[200]={Symbol("EURUSD.b")};
    Check(p.ResolveSymbol(rule,"EURUSD.a",m::Side::Buy).symbol=="EURUSD.b","shared suffix resolution validates contracts");
    f.symbols[200][0].spec.contract_size=1000;
    Check(p.ResolveSymbol(rule,"EURUSD.a",m::Side::Buy).symbol.empty(),"same base name incompatible contract refused");
    f.symbols[200]={Symbol("EURUSD.b")};f.symbols[100][0].spec.tick_size=0.00001;f.symbols[200][0].spec.tick_size=0.0001;
    Check(p.ResolveSymbol(rule,"EURUSD.a",m::Side::Buy).symbol.empty(),"different tick size refuses apparently identical contract");
    f.symbols[200][0].spec.tick_size=0.00001;f.symbols[100][0].spec.tick_value=1;f.symbols[200][0].spec.tick_value=2;
    Check(p.ResolveSymbol(rule,"EURUSD.a",m::Side::Buy).symbol.empty(),"different tick value refuses apparently identical contract");
    f.symbols[100][0]=Symbol("EURUSD.a");
    f.symbols[200]={Symbol("EURUSD.b"),Symbol("EURUSD.c")};
    Check(p.ResolveSymbol(rule,"EURUSD.a",m::Side::Buy).symbol.empty(),"ambiguous suffix resolution refused");
    Harness h;h.broker.orders[100]={Order(1,100,100,1000)};h.port->BeginCycle();
    h.engine->Admit(*h.port->Opening(100,1),1);h.engine->Tick();
    Check(h.broker.sent.empty(),"same-second activation conservatively skips source opening");
    h.broker.now=2000;const auto child=h.broker.Close(100,1,20);h.port->BeginCycle();
    Check(!h.port->Opening(100,child),"preactivation source continuation never catches up");
    Check(!h.port->Opening(100,1),"replayed root of preactivation chain cannot become a fresh opening");
    h.engine->Tick();Check(h.broker.sent.empty(),"preactivation chain remains uncopied");
}
void FrozenSymbolsAndCloseOnly() {
    Harness h;h.Admit();h.Fill(0);
    auto next=h.config;next.relationships[0].parameter="2";next.relationships[0].auto_symbol=true;
    h.broker.now=3000;h.engine->UpdateSubscriptions(h.engine->state().config_json,next,h.port->Snapshot({100},1));
    auto old=Symbol();old.spec.buy=old.spec.sell=false;old.close_allowed=true;
    h.broker.symbols[200]={old,Symbol("EURUSD.b")};
    h.Admit(2,20);
    Check(h.broker.sent.size()==2 && h.broker.sent[1].second.volume==40 && h.broker.sent[1].second.symbol=="EURUSD.b","changed rules affect subsequent new opening only");
    h.Fill(1);h.broker.now=5000;h.broker.Close(100,1,50);h.engine->Tick();
    Check(h.broker.sent.size()==3 && h.broker.sent[2].second.volume==25 && h.broker.sent[2].second.symbol=="EURUSD","old mapping retains original symbol and closes when opening is disabled");
    h.Fill(2);

    Fake f;m::Port p(f,42);f.orders[200]={Order(90,200,50)};f.orders[200][0].side=m::Side::Sell;
    auto long_only=Symbol();long_only.spec.sell=false;f.symbols[200]={long_only};
    const auto rule=Config().relationships[0];
    Check(p.Limits(rule,"EURUSD","EURUSD",m::Side::Buy,true).minimum==V(1),"long-only opening rule does not prohibit closing existing sell");
    e::Intent close;close.id=1;close.close=true;close.follower_login=200;close.follower_position=90;
    close.side=m::Side::Buy;close.symbol="EURUSD";close.units=close.follower_before=V(50);close.created_ms=3000;
    p.Submit(close);Check(f.sent.at(0).second.position_side==m::Side::Sell,"MT4 closing cmd remains original sell side");
    long_only.close_allowed=false;f.symbols[200]={long_only};
    Throws([&]{p.Submit(close);},"explicit close prohibition remains enforced");
}
void AddedSubscriptionAndExit() {
    Harness h;h.Admit();h.Fill(0);auto next=Config(tradecopier::SizingMode::LotRatio,"0.5",true);
    h.broker.now=3000;h.engine->UpdateSubscriptions(h.engine->state().config_json,next,h.port->Snapshot({100},1));h.engine->Tick();
    Check(h.broker.sent.size()==1,"new subscription does not backfill existing source position");
    h.Admit(2,20);Check(h.broker.sent.size()==3,"subsequent source open follows both subscriptions");h.Fill(1);h.Fill(2);
    auto remaining=next;remaining.relationships.erase(remaining.relationships.begin());h.broker.now=5000;
    h.engine->UpdateSubscriptions(h.engine->state().config_json,remaining,h.port->Snapshot({100},2));h.engine->Tick();
    Check(h.broker.sent.size()==5 && h.broker.sent[3].second.close && h.broker.sent[4].second.close &&
          h.broker.sent[3].second.login==200 && h.broker.sent[4].second.login==200,"subscription removal closes only its owned mappings");
    h.Fill(3);h.Fill(4);
    Check(m::Port::ResolveChain(h.broker.orders[201],h.broker.orders[201].front().ticket).remaining==V(10),"other subscription position remains intact");
}
void RestartThenPartialClose() {
    Harness h;h.Admit();h.Fill(0);const Id position=h.engine->state().links.at(0).follower_position;
    h.broker.now=3000;h.Restart();Check(h.engine->state().links.at(0).phase==e::Phase::Active,"unchanged open source survives journal restart");
    h.broker.now=4000;h.broker.Close(100,1,25);h.engine->Tick();
    Check(h.broker.sent.size()==2 && h.broker.sent[1].second.volume==12,"first source partial close after restart follows floor-to-step policy");
    h.Fill(1);Check(h.port->FollowerRemaining(200,position,"EURUSD",m::Side::Buy)==V(38),"post-restart partial confirmation retains root and actual remainder");
}
void TransientSourceSnapshot() {
    Harness h;h.Admit();h.Fill(0);auto saved=h.broker.orders[100];h.broker.orders[100].clear();
    h.broker.now=3000;h.engine->Tick();
    Check(h.engine->state().links.at(0).phase==e::Phase::Active && h.broker.sent.size()==1,"temporary missing source snapshot waits without extra execution");
    h.broker.orders[100]=saved;h.broker.now=4000;h.broker.Close(100,1,20);h.engine->Tick();
    Check(h.broker.sent.size()==2 && h.broker.sent[1].second.volume==10,"source snapshot recovery continues ordinary partial close");
    h.Fill(1);h.broker.orders[100].clear();h.broker.now=5000;h.engine->Tick();
    h.broker.now=36000;h.engine->Tick();
    Check(h.engine->state().links.at(0).phase==e::Phase::Review,"persistent missing source evidence eventually isolates mapping");
}
e::Intent CloseIntent(Id position,Units before,Units amount) {
    e::Intent i;i.id=7;i.close=true;i.follower_login=200;i.follower_position=position;i.side=m::Side::Sell;
    i.symbol="EURUSD";i.follower_before=before;i.units=amount;i.created_ms=3000;return i;
}
void CloseWithoutRequestCallback() {
    for(bool partial:{false,true}) {
        Harness h;h.Admit();h.Fill(0);const Id position=h.engine->state().links.at(0).follower_position;
        h.broker.now=3000;h.broker.Close(100,1,partial?20:100);h.engine->Tick();
        Check(h.broker.sent.size()==2,"source close submitted before update-only settlement");
        const auto intent=h.engine->state().intents.back();
        Check(h.port->Reconcile(intent).status==e::Result::Status::Waiting,"accepted close queue alone never confirms");
        h.broker.Fill(1,*h.port,false);h.port->BeginCycle();const auto result=h.port->Reconcile(intent);
        Check(result.status==e::Result::Status::Filled && result.position==position &&
              result.reason=="mt4_exact_close_target_settled","update-only full and partial close settle via exact terminal ticket");
        h.engine->ConfirmPending();h.engine->Tick();
        Check(h.port->FollowerRemaining(200,position,"EURUSD",m::Side::Buy)==(partial?V(40):0),"update-only settlement preserves actual remaining volume");
        Check(h.broker.sent.size()==2,"update-only settlement never resubmits close");
    }
    for(int amount:{10,50}) {
        Fake f;m::Port p(f,42);f.orders[200]={Order(90,200,50)};f.now=3000;
        auto intent=CloseIntent(90,V(50),V(amount));const auto submitted=p.Submit(intent);intent.request_id=submitted.request_id;
        f.now=4000;f.Close(200,90,amount);p.BeginCycle();const auto result=p.Reconcile(intent);
        Check(result.status==e::Result::Status::Filled && result.units==V(amount) && result.reason=="mt4_exact_close_target_settled",
              "manual same-volume close completes accepted exact old-ticket objective without attributing actor");
        m::Port restarted(f,42);
        Check(restarted.Reconcile(intent).status!=e::Result::Status::Filled,"restart missing volatile actual target cannot use close fallback");
        p.ForgetRequest(intent.id);
        Check(p.Reconcile(intent).status!=e::Result::Status::Filled,"missing accepted target binding cannot use close fallback");
    }
    {
        Fake f;m::Port p(f,42);f.orders[200]={Order(90,200,50)};f.now=3000;
        auto intent=CloseIntent(90,V(50),V(10));intent.request_id=p.Submit(intent).request_id;
        f.orders[200][0].volume=40;p.BeginCycle();
        Check(p.Reconcile(intent).status==e::Result::Status::Waiting,"same live ticket reduced volume alone does not prove settlement");
        f.orders[200][0].volume=50;f.now=4000;f.Close(200,90,20);p.BeginCycle();
        Check(p.Reconcile(intent).status==e::Result::Status::Uncertain,"terminal old ticket closed by wrong volume is rejected");
    }
    {
        Fake f;m::Port p(f,42);f.orders[200]={Order(90,200,50)};f.now=3000;
        auto intent=CloseIntent(90,V(50),V(50));intent.request_id=p.Submit(intent).request_id;
        auto unrelated=Order(91,200,50);unrelated.close_ms=4000;unrelated.state=3;unrelated.comment=p.Token(intent.id);
        f.orders[200].push_back(unrelated);p.BeginCycle();
        Check(p.Reconcile(intent).status==e::Result::Status::Uncertain,"same token and volume on a different ticket cannot settle accepted close");
        f.orders[200].pop_back();f.now=4000;f.Close(200,90,50);f.orders[200][0].symbol="GBPUSD";p.BeginCycle();
        Check(p.Reconcile(intent).status==e::Result::Status::Uncertain,"exact ticket with changed symbol cannot settle close");
    }
    {
        Fake f;m::Port p(f,42);f.orders[200]={Order(90,200,50)};f.now=3000;
        f.acknowledgements[200]=e::Submission::Status::Unknown;
        auto intent=CloseIntent(90,V(50),V(50));intent.request_id=p.Submit(intent).request_id;
        f.now=4000;f.Close(200,90,50);p.BeginCycle();
        Check(p.Reconcile(intent).status!=e::Result::Status::Filled,"unknown submission is not upgraded to accepted close fallback");
    }
    {
        Harness h;h.Admit();h.Fill(0);h.broker.now=3000;h.broker.Close(100,1,100);h.engine->Tick();
        h.broker.Fill(1,*h.port,false);h.broker.now=4000;h.Restart();h.engine->Tick();
        Check(h.broker.sent.size()==2 && h.engine->state().links.at(0).phase==e::Phase::Review,"FileJournal restart does not invent native target binding or resend settled pending close");
    }
}
void EquityFreshness() {
    Fake f;m::Port p(f,42);const auto rule=Config(tradecopier::SizingMode::EquityRatio,"1").relationships[0];
    f.accounts[100]={"USD",100000,2,2000,true,false};f.accounts[200]={"USD",50000,2,8000,true,false};
    Throws([&]{p.Equities(rule);},"equity queries more than five seconds apart rejected");
    f.accounts[200].query_ms=1000;
    Throws([&]{p.Equities(rule);},"backward equity query time rejected");
    f.accounts[200].query_ms=4000;const auto snapshot=p.Equities(rule);
    e::Intent intent;intent.id=8;intent.follower_login=200;intent.symbol="EURUSD";intent.units=V(50);intent.created_ms=4000;intent.equity=snapshot;
    f.now=8000;const auto stale=p.Submit(intent);
    Check(stale.status==e::Submission::Status::NotSubmitted && !stale.request_id && stale.retcode==128 && f.sent.empty(),"stale equity is rejected before any SDK submission");
    f.now=1000;Check(p.Submit(intent).status==e::Submission::Status::NotSubmitted && f.sent.empty(),"clock reversal also refuses before SDK submission");
    intent.equity={};f.now=8000;
    Check(p.Submit(intent).status==e::Submission::Status::Queued && f.sent.size()==1,"lot and fixed intents have no irrelevant equity freshness restriction");
}
}
int main() {
    try {VolumeAndLineage();Sizing();PartialManualAndRestart();PendingAndIsolation();EvidenceAndAccounts();DealerAdmissionEvidenceAndRecovery();SymbolAndActivation();FrozenSymbolsAndCloseOnly();AddedSubscriptionAndExit();RestartThenPartialClose();TransientSourceSnapshot();CloseWithoutRequestCallback();EquityFreshness();
        std::cout<<"MT4 port and FileJournal integration: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"MT4 port test failed: "<<error.what()<<" after "<<checks<<" checks\n";return 1;}
}

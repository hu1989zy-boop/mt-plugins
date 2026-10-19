#include "tradecopier/executor.hpp"
#include "tradecopier/journal.hpp"
#include "event_queue.hpp"
#include "worker_wake.hpp"
#include "request_refusals.hpp"
#include "tradecopier/management.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace tradecopier;
using namespace tradecopier::execution;
namespace {
unsigned checks=0;
void Require(bool value,const char* message) {
    ++checks; if (!value) throw std::runtime_error(message);
}
template<typename F> void Throws(F f,const char* message) {
    bool threw=false; try { f(); } catch (const std::exception&) { threw=true; }
    Require(threw,message);
}
constexpr Units lot=kUnitsPerLot;
// Construct exact legacy payloads without the schema 5 symbol extension.
std::string Schema4Payload(const State& state) {
    auto result=EncodeState(state);
    if (static_cast<unsigned char>(result[0])==7) {
        std::size_t direction_extension=8;
        for (const auto& link:state.links) if (link.opening_rule) direction_extension+=8;
        result.resize(result.size()-direction_extension);
    }
    result.resize(result.size()-16-state.links.size()*8-state.subscriptions.size()*24); std::size_t extension=8;
    for (const auto& link:state.links) {
        extension+=8+link.follower_symbol.size();
        if (link.opening_rule) extension+=24+link.opening_rule->symbol_mask.size()+link.opening_rule->source_suffix.size();
    }
    result.resize(result.size()-extension); result[0]=4; return result;
}
// Schema 4 keeps the old fields and appends compact-state metadata.
// Strip only that extension to exercise the actual legacy decoding path.
std::string LegacyPayload(const State& state, unsigned version=2) {
    auto result=Schema4Payload(state);
    result.resize(result.size()-32-state.closed_sources.size()*32-state.capacity_pause.size());
    std::size_t extension=8;
    for (const auto& link:state.links) extension+=8+(link.opening_rule?48+link.opening_rule->parameter.size():0);
    result.resize(result.size()-extension-(version==1?8:0));
    result[0]=static_cast<char>(version);
    return result;
}
struct MemoryStore:Store {
    std::string saved, pruned_from, capacity_reason;
    unsigned saves=0,fail_at=0;
    bool Load(State& state) override { if (saved.empty()) return false; state=DecodeState(saved); return true; }
    void Save(const State& state) override {
        if (++saves==fail_at) throw std::runtime_error("injected disk failure");
        saved=EncodeState(state);
    }
    void Compact(const State& state) override { pruned_from=saved; Save(state); }
    std::string OpeningBlockReason(const State&,std::size_t) override { return capacity_reason; }
};
struct FakePort:Port {
    MemoryStore& store;
    std::int64_t now=2000;
    std::map<Id,SourceView> sources;
    std::map<std::pair<Id,Id>,Units> positions;
    std::map<std::pair<Id,Id>,Side> position_sides;
    std::vector<Side> resolution_sides;
    std::vector<std::pair<Side,bool>> limit_sides;
    std::map<Id,Result> results;
    std::vector<Intent> submissions;
    std::vector<std::string> logs;
    VolumeLimits limits{lot/100,50*lot,lot/100};
    EquitySnapshot equity{100000,50000,3,1900,1950};
    unsigned equity_reads=0, source_reads=0, reconcile_reads=0, read_cycles=0;
    bool throw_begin_cycle=false;
    Id throw_submit_at=0, throw_reconcile_at=0, throw_limits_login=0, slow_position=0;
    std::map<Id,Submission::Status> submission_status, submission_status_by_login;
    Submission::Status status=Submission::Status::Queued;
    bool throw_submit=false,throw_read=false,allow_degraded_close=false;
    explicit FakePort(MemoryStore& s):store(s) {}
    std::int64_t NowMs() override { return now; }
    void BeginCycle() override {
        ++read_cycles;
        if (throw_begin_cycle) throw std::runtime_error("injected cycle failure");
    }
    SourceView ReadSource(const Deal& d) override {
        ++source_reads;
        if (throw_read) throw std::runtime_error("injected SDK error");
        return sources.at(d.position);
    }
    SymbolResolution ResolveSymbol(const RuleConfig&,const std::string& symbol,Side side) override {
        resolution_sides.push_back(side); return {symbol,{}};
    }
    VolumeLimits Limits(const RuleConfig& rule,const std::string& symbol,const std::string&,Side side,bool close) override {
        limit_sides.emplace_back(side,close);
        if (rule.follower_login==throw_limits_login) throw std::runtime_error("injected limits failure");
        Require(symbol=="XAUUSDp","must preserve exact symbol"); return limits;
    }
    EquitySnapshot Equities(const RuleConfig&) override { ++equity_reads; return equity; }
    Units FollowerRemaining(Id login,Id position,const std::string&,Side side) override {
        const auto known=position_sides.find({login,position});
        if (known!=position_sides.end()) Require(known->second==side,"follower read uses actual target side");
        if (position==slow_position) std::this_thread::sleep_for(std::chrono::milliseconds(8));
        return positions[{login,position}];
    }
    Submission Submit(const Intent& intent) override {
        const auto durable=DecodeState(store.saved);
        const auto found=std::find_if(durable.intents.begin(),durable.intents.end(),[&](const Intent& i) { return i.id==intent.id; });
        Require((allow_degraded_close && intent.close) || (found!=durable.intents.end() && found->state==IntentState::Prepared && found->units==intent.units),
                "request must have a durable prepared intent BEFORE Submit");
        submissions.push_back(intent);
        if (throw_submit || intent.id==throw_submit_at) throw std::runtime_error("crash during Submit");
        const auto fallback=submission_status_by_login.count(intent.follower_login)?submission_status_by_login[intent.follower_login]:status;
        return {submission_status.count(intent.id)?submission_status[intent.id]:fallback,static_cast<std::uint32_t>(1000+intent.id),0};
    }
    Result Reconcile(const Intent& intent) override {
        ++reconcile_reads;
        if (intent.id==throw_reconcile_at) throw std::runtime_error("injected reconciliation failure");
        return results[intent.id];
    }
    void Log(const std::string& text) override { logs.push_back(text); }
    void Fill(Id id,Units units,Id position=0) {
        const auto& intent=*std::find_if(submissions.begin(),submissions.end(),[&](const Intent& i) { return i.id==id; });
        if (!position) position=intent.close?intent.follower_position:9000+id;
        if (!intent.close) position_sides[{intent.follower_login,position}]=intent.side;
        else {
            const auto known=position_sides.find({intent.follower_login,position});
            if (known!=position_sides.end()) Require(known->second!=intent.side,"close opposes actual target side");
        }
        results[id]={Result::Status::Filled,position,units,10009,{70000+id}};
        positions[{intent.follower_login,position}]=intent.close?intent.follower_before-units:units;
    }
};
CopierConfig Config(SizingMode mode=SizingMode::LotRatio,const std::string& parameter="1") {
    return {true,{{1,10,20,true,mode,parameter}}};
}
Deal Opening(Id position=100,Id login=10,Id deal=1000,Units volume=lot) {
    return {deal,login,position,position,volume,2000,"XAUUSDp",Side::Buy,true,false};
}
SourceView Source(Units units=lot) { return {ReadState::Ready,units,units,2000,{},""}; }
struct Fixture {
    MemoryStore store;
    FakePort port{store};
    Engine engine;
    explicit Fixture(CopierConfig config=Config()):engine(config,"test-server-6182",port,store) {
        engine.Start({1000,10,{}}); port.sources[100]=Source();
    }
    void Open() { engine.Admit(Opening(),11); engine.Tick(); }
    void Confirm() {
        const auto found=std::find_if(engine.state().intents.begin(),engine.state().intents.end(),
            [](const Intent& i) { return i.state==IntentState::Submitted; });
        Require(found!=engine.state().intents.end(),"test confirmation must target a pending request");
        port.Fill(found->id,found->units); engine.Tick();
    }
    void CloseSource(Id deal,Units units) {
        auto& source=port.sources[100]; source.remaining-=units;
        source.closes.push_back({deal,units,++port.now});
    }
};

void Lifecycle() {
    Fixture f;
    f.Open();
    Require(f.port.submissions.size()==1,"opening request emitted");
    Require(f.engine.state().links[0].follower_position==0,"queued request is not a position");
    f.engine.Tick(); f.engine.Admit(Opening(),12); f.engine.Tick();
    Require(f.port.submissions.size()==1,"duplicate opening callback cannot resubmit");
    f.Confirm();
    Require(f.engine.state().links[0].follower_position==9001,"confirmed opening maps exact position");
    f.CloseSource(2001,40*lot/100); f.engine.Tick();
    Require(f.port.submissions.back().close && f.port.submissions.back().follower_position==9001 &&
            f.port.submissions.back().units==40*lot/100 && f.port.submissions.back().side==Side::Sell,
            "partial close addresses mapped buy with a sell request");
    f.Confirm(); f.engine.Tick();
    Require(f.port.submissions.size()==2,"history reread does not repeat source close");
    f.CloseSource(2002,60*lot/100); f.engine.Tick(); f.Confirm();
    Require(f.engine.state().links.empty() && f.engine.state().intents.empty(),"full close removes settled lifecycle records");
    Require(f.port.positions[{20,9001}]==0,"full close leaves no mapped volume");
}
void SourceClosesDuringOpening() {
    Fixture f; f.Open(); f.CloseSource(2001,lot);
    f.engine.Tick(); Require(f.port.submissions.size()==1,"close waits for opening confirmation");
    f.Confirm();
    Require(f.port.submissions.size()==2 && f.port.submissions[1].close,"queued source close handled after confirmation");
    f.Confirm(); Require(f.engine.state().links.empty() && !f.port.positions[{20,9001}],"fast lifecycle closed and pruned");
}
void SellAndPartialExecution() {
    Fixture f;
    auto sell=Opening(); sell.side=Side::Sell;
    f.engine.Admit(sell,11); f.engine.Tick();
    Require(f.port.submissions[0].side==Side::Sell,"sell source opens target sell");
    f.Confirm(); f.CloseSource(2001,40*lot/100); f.engine.Tick();
    Require(f.port.submissions[1].side==Side::Buy && f.port.submissions[1].follower_position==9001,
            "closing sell sends a buy tied to exact sell position");
    f.port.Fill(2,20*lot/100); f.engine.Tick(); f.engine.Tick();
    Require(f.engine.state().halt_reason.empty() && !f.engine.state().openings_paused && f.port.submissions.size()==2 &&
            f.engine.state().intents[1].filled==20*lot/100,"partial close is isolated without pausing other mappings or retrying");
}
void ManualActions() {
    Fixture f; f.Open(); f.Confirm();
    f.port.positions[{20,9001}]=80*lot/100;
    f.CloseSource(2001,40*lot/100); f.engine.Tick();
    Require(f.port.submissions.back().units==32*lot/100,"partial close uses actual manually reduced remainder");
    f.Confirm();
    f.CloseSource(2002,60*lot/100); f.engine.Tick();
    Require(f.port.submissions.back().units==48*lot/100,"full source close closes exact actual remainder");
    Fixture closed; closed.Open(); closed.Confirm(); closed.port.positions[{20,9001}]=0;
    closed.engine.Tick(); closed.CloseSource(2001,lot); closed.engine.Tick();
    closed.engine.Admit(Opening(),20); closed.engine.Tick();
    Require(closed.engine.state().links.empty() && closed.engine.state().closed_sources.size()==1 && closed.port.submissions.size()==1,
            "manual full closure is terminal and cannot reopen");
    Fixture increased; increased.Open(); increased.Confirm(); increased.port.positions[{20,9001}]=2*lot;
    increased.engine.Tick();
    Require(increased.engine.state().links[0].phase==Phase::Review,"unexpected volume increase needs review");
}
void ActivationAndAggregation() {
    Fixture f;
    auto old=Opening(); old.time_ms=1000; f.engine.Admit(old,11);
    f.engine.Admit(Opening(),10);
    auto owned=Opening(); owned.copier_owned=true; f.engine.Admit(owned,12);
    Require(f.engine.state().links.empty(),"activation cutoff, sequence and copier origin enforced");
    MemoryStore store; FakePort port(store); Engine e(Config(),"test",port,store);
    e.Start({1000,10,{{10,100}}}); e.Admit(Opening(),11);
    Require(e.state().links.empty(),"existing baseline position excluded");
    Fixture split(Config(SizingMode::FixedLots,"0.25"));
    split.engine.Admit(Opening(100,10,1000,40*lot/100),11);
    split.port.sources[100].state=ReadState::Waiting;
    split.engine.Tick(); Require(split.port.submissions.empty(),"wait for source entry order completion");
    split.engine.Admit(Opening(100,10,1001,60*lot/100),12);
    split.port.sources[100]=Source(); split.engine.Tick();
    Require(split.port.submissions.size()==1 && split.port.submissions[0].units==25*lot/100,
            "split source opening gets fixed amount exactly once");
    Fixture prior; prior.port.sources[100].first_fill_ms=900; prior.Open();
    Require(prior.port.submissions.empty(),"late partial fill of preactivation lifecycle excluded");
    Fixture gone; gone.CloseSource(2001,lot); gone.Open();
    Require(gone.port.submissions.empty(),"already completed source lifecycle not chased");
}
void EquityAndLimits() {
    Fixture f(Config(SizingMode::EquityRatio,"2")); f.Open();
    Require(f.port.submissions[0].units==lot && f.port.equity_reads==1,"equity ratio and coefficient applied");
    f.port.equity.follower=900000; f.Confirm();
    Require(f.engine.state().intents[0].equity.follower==50000 && f.port.equity_reads==1 &&
            f.engine.state().intents[0].equity.common_digits==3 &&
            f.engine.state().intents[0].equity.follower_query_ms==1950,"frozen equity and independent times persisted");
    Fixture too_large(Config(SizingMode::FixedLots,"51")); too_large.Open();
    Require(too_large.port.submissions.empty(),"never cap overmaximum request");
    Fixture tiny(Config(SizingMode::FixedLots,"0.001")); tiny.Open();
    Require(tiny.port.submissions.empty(),"opening below minimum not rounded up");
    tiny.engine.Tick();
    Require(tiny.engine.state().links.empty() && tiny.engine.state().intents.empty(),
        "pre-submit minimum skip retires without a review or request");
    Fixture step; step.Open(); step.Confirm(); step.port.positions[{20,9001}]=lot-1;
    step.CloseSource(2001,lot/2); step.engine.Tick();
    Require(step.port.submissions.size()==1 && step.engine.state().links[0].phase==Phase::Review,
            "misaligned manual remainder cannot produce a close");
}
void MinimumOpeningRetirement(unsigned rounds=40) {
    auto config=Config(SizingMode::LotRatio,"0.5");
    config.relationships.push_back({2,10,30,true,SizingMode::FixedLots,"0.02"});
    Fixture f(config);
    for (unsigned n=0;n<rounds;++n) {
        f.port.now=2000+std::int64_t(n)*1000;
        auto deal=Opening(100+n,10,1000+n,lot/100); deal.time_ms=f.port.now;
        auto source=Source(lot/100); source.first_fill_ms=f.port.now; f.port.sources[deal.position]=source;
        f.engine.Admit(deal,11+n); f.engine.Tick();
        f.port.Fill(f.port.submissions.back().id,lot/50); f.engine.Tick();
        f.port.sources[deal.position].remaining=0;
        f.port.sources[deal.position].closes={{30000+n,lot/100,f.port.now+1}};
        f.engine.Tick(); f.port.Fill(f.port.submissions.back().id,lot/50); f.engine.Tick(); f.engine.Tick();
        Require(f.engine.state().links.empty() && f.engine.state().intents.empty() &&
            f.engine.state().closed_sources.size()<=62 && f.engine.state().halt_reason.empty() &&
            !f.engine.state().openings_paused,"minimum skips stay bounded and healthy subscriber drains");
    }
    Require(f.port.submissions.size()==rounds*2 && std::all_of(f.port.submissions.begin(),f.port.submissions.end(),
        [](const Intent& i){return i.follower_login==30;}),"minimum skips never send requests; other account is exact once");
    Fixture tiny(Config(SizingMode::FixedLots,"0.001")); tiny.engine.Admit(Opening(),11);
    State original=tiny.engine.state(); original.links[0].phase=Phase::Review;
    original.links[0].reason="skip_below_minimum";
    original.links[0].source_initial=original.links[0].source_remaining=lot;
    original.links[0].follower_symbol="XAUUSDp";
    original.openings_paused=true; original.next_intent_id=2089;
    const auto dir=std::filesystem::temp_directory_path()/("tcx-minimum-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    for (unsigned variant=0;variant<12;++variant) {
        State seed=original; seed.revision=1; auto& link=seed.links[0];
        if (variant==1) link.reason="review_close_below_minimum";
        if (variant==2) link.follower_position=9001;
        if (variant==3) link.follower_remaining=lot;
        if (variant==4) link.processed_closes={2001};
        if (variant==5) link.source_remaining=0;
        if (variant==6) link.source_initial=link.source_remaining=0;
        if (variant>=7) {
            Intent i; i.id=2088; i.link=0; i.follower_login=20; i.symbol=link.follower_symbol;
            i.units=lot; i.created_ms=2000; i.state=static_cast<IntentState>(variant-7); seed.intents.push_back(i);
        }
        const auto path=dir/(std::to_string(variant)+".journal");
        State final;
        { FileJournal store(path); State empty; Require(!store.Load(empty),"new migration test journal"); store.Save(seed); }
        { FileJournal store(path); MemoryStore unused; FakePort port(unused); port.now=4000;
          Engine engine(Config(SizingMode::FixedLots,"0.001"),seed.identity,port,store,true);
          engine.Start({4000,40,{}}); engine.Tick(); final=engine.state();
          Require(final.links.size()==(variant?1U:0U) && port.submissions.empty() &&
              final.openings_paused && final.next_intent_id==2089,
              "legacy cleanup requires exact minimum pre-submit evidence and preserves operator state"); }
        { FileJournal store(path); State replay;
          Require(store.Load(replay) && EncodeState(replay)==EncodeState(final),"minimum cleanup replays exactly from real journal"); }
    }
    for (unsigned failure:{1U,2U}) {
        MemoryStore store; store.saved=EncodeState(original); store.fail_at=failure; FakePort port(store);
        Engine engine(Config(SizingMode::FixedLots,"0.001"),original.identity,port,store,true);
        engine.Start({4000,40,{}});
        Require(engine.storage_degraded() && port.submissions.empty(),"interrupted minimum migration never submits a trade");
        store.fail_at=0;
        Engine retry(Config(SizingMode::FixedLots,"0.001"),original.identity,port,store,true); retry.Start({5000,50,{}});
        Require(retry.state().links.empty() && retry.state().next_intent_id==2089,"minimum migration recovers both durable boundaries");
    }
    std::filesystem::remove_all(dir);
    std::cout<<"minimum opening retirement: rounds="<<rounds<<" healthy_requests="<<f.port.submissions.size()<<" reviews=0\n";
}
void ManyToMany() {
    auto config=Config();
    config.relationships.push_back({2,10,30,true,SizingMode::LotRatio,"2"});
    config.relationships.push_back({3,11,20,true,SizingMode::FixedLots,"0.3"});
    config.relationships.push_back({4,20,40,true,SizingMode::LotRatio,"1"});
    Fixture f(config); f.Open(); f.Confirm();
    Require(f.port.submissions.size()==2 && f.port.submissions[1].follower_login==30,"independent second follower is already submitted before first confirmation");
    f.Confirm();
    f.port.sources[101]=Source(); f.engine.Admit(Opening(101,11,1001),20); f.engine.Tick(); f.Confirm();
    Require(f.port.submissions.size()==3 && f.port.submissions[2].follower_login==20,"second source has separate target mapping");
    auto recursive=Opening(9001,20,70001); recursive.copier_owned=true; f.engine.Admit(recursive,21); f.engine.Tick();
    Require(f.port.submissions.size()==3,"generated follower position cannot recursively copy");
    f.CloseSource(2001,lot); f.engine.Tick();
    const auto close=f.port.submissions.back();
    Require(close.close && close.follower_position!=9003,"source close cannot touch other source's target position");
    f.Confirm(); f.Confirm();
    Require(f.port.positions[{20,9003}]==30*lot/100,"unrelated mapped position remains unchanged");
}
void FaultsAndRestart() {
    Fixture partial; partial.Open(); partial.port.Fill(1,lot/2); partial.engine.Tick();
    partial.engine.Tick();
    Require(partial.engine.state().halt_reason.empty() && !partial.engine.state().openings_paused && partial.port.submissions.size()==1 &&
            partial.engine.state().intents[0].filled==lot/2,"partial fill is recorded and never retried");
    Fixture timeout; timeout.Open(); timeout.port.now+=30001; timeout.engine.Tick();
    Require(timeout.engine.state().halt_reason.empty() && !timeout.engine.state().openings_paused &&
        timeout.engine.state().links[0].reason=="execution_confirmation_timeout","timeout isolates only the unresolved mapping");
    Fixture reject; reject.Open(); reject.port.results[1].status=Result::Status::Rejected;
    reject.engine.Tick(); Require(reject.engine.state().links[0].phase==Phase::Review,"known rejection does not pretend filled");
    Fixture mismatch; mismatch.Open(); mismatch.port.Fill(1,lot); mismatch.port.positions[{20,9001}]=lot/2;
    mismatch.engine.Tick(); Require(mismatch.engine.state().halt_reason.empty() && mismatch.engine.state().links[0].follower_remaining==lot/2,"verified reduction during confirmation is retained without halting");
    Fixture unknown; unknown.port.status=Submission::Status::Unknown; unknown.Open();
    Require(unknown.engine.state().halt_reason.empty() && unknown.engine.state().links[0].phase==Phase::Review,"uncertain submission is never retried");
    Fixture disk;
    disk.engine.Admit(Opening(),11); disk.store.fail_at=disk.store.saves+1;
    disk.engine.Tick();
    disk.engine.Tick(); Require(disk.port.submissions.empty(),"disk failure prevents any submission");
    Fixture after;
    after.engine.Admit(Opening(),11); after.store.fail_at=after.store.saves+2;
    after.engine.Tick();
    Require(after.port.submissions.size()==1,"broker side effect occurred before injected second write failure");
    after.store.fail_at=0;
    Engine restarted(Config(),"test-server-6182",after.port,after.store);
    restarted.Start({3000,30,{{10,100}}}); restarted.Tick();
    Require(restarted.state().halt_reason.empty() && restarted.state().links[0].phase==Phase::Review && after.port.submissions.size()==1,
            "crash between Submit and persisted request id cannot duplicate opening");
    Fixture during; during.port.throw_submit=true;
    during.Open();
    Engine resumed(Config(),"test-server-6182",during.port,during.store);
    resumed.Start({3000,30,{}}); resumed.Tick();
    Require(resumed.state().halt_reason.empty() && resumed.state().links[0].phase==Phase::Review && during.port.submissions.size()==1,"restart of prepared request requires review");
    Fixture stable; stable.Open(); stable.Confirm(); stable.port.positions[{20,9001}]=80*lot/100;
    Engine clean(Config(),"test-server-6182",stable.port,stable.store);
    clean.Start({3000,30,{{10,100}}}); clean.Tick();
    Require(clean.state().links[0].phase==Phase::Active && clean.state().links[0].follower_remaining==80*lot/100,
            "known mapping can resume after reconciling manual reduction during stop");
    stable.CloseSource(2001,lot/2);
    Engine offline(Config(),"test-server-6182",stable.port,stable.store);
    offline.Start({4000,40,{{10,100}}}); offline.Tick();
    Require(offline.state().links[0].phase==Phase::Review && stable.port.submissions.size()==1,
            "offline source trades cannot be replayed blindly");
    Throws([&] { Engine wrong(Config(),"other-server",stable.port,stable.store); },"server identity bound to journal");
    Throws([&] { Engine wrong(Config(SizingMode::FixedLots,"1"),"test-server-6182",stable.port,stable.store); },
           "config change requires explicit migration");
}
void HistoryFailures() {
    Fixture f; f.Open(); f.Confirm();
    f.CloseSource(2001,lot/2); f.engine.Tick(); f.Confirm();
    f.port.sources[100].closes[0].units=lot/4; f.port.sources[100].remaining=3*lot/4;
    f.engine.Tick();
    Require(f.engine.state().links[0].phase==Phase::Review,"edited historical deal detected");
    Fixture duplicate; duplicate.Open(); duplicate.Confirm(); duplicate.CloseSource(2001,lot/4);
    duplicate.CloseSource(2001,lot/4); duplicate.engine.Tick();
    Require(duplicate.engine.state().links[0].phase==Phase::Review,"duplicate history IDs rejected");
    Fixture api; api.Open(); api.Confirm(); api.port.throw_read=true;
    api.engine.Tick();
    Require(api.port.submissions.size()==1,"read error produces no new request");
    Fixture transient; transient.Open(); transient.Confirm(); transient.port.now=100000;
    transient.port.sources[100].state=ReadState::Waiting;
    transient.engine.Tick();
    Require(transient.engine.state().links[0].phase==Phase::Active,"transient history lag on old position starts a new wait window");
    transient.port.now+=29999; transient.engine.Tick();
    Require(transient.engine.state().links[0].phase==Phase::Active,"history lag does not prematurely stop mapping");
    ++transient.port.now; transient.engine.Tick();
    Require(transient.engine.state().links[0].phase==Phase::Review,"persistent source history lag stops mapping");
    Fixture stale; stale.engine.Admit(Opening(),11); stale.port.now=32000; stale.engine.Tick();
    Require(stale.port.submissions.empty(),"stale opening cannot execute after queue delay");
    Fixture alias; alias.Open(); alias.Confirm(); alias.port.sources[101]=Source();
    alias.engine.Admit(Opening(101,10,1001),20); alias.engine.Tick();
    alias.port.Fill(2,lot,9001); alias.engine.Tick();
    Require(alias.engine.state().halt_reason.empty() && alias.engine.state().links[0].phase==Phase::Review && alias.engine.state().links[1].phase==Phase::Review,"two source mappings cannot claim same follower ticket");
}
void JournalFiles() {
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir=std::filesystem::temp_directory_path()/("tcx-tests-"+std::to_string(stamp));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path dir; ~Cleanup() { std::error_code e; std::filesystem::remove_all(dir,e); } } cleanup{dir};
    const auto path=dir/"journal.bin";
    Fixture f; f.Open(); f.Confirm();
    State state=f.engine.state(); state.revision=1;
    {
        FileJournal file(path); State loaded;
        Require(!file.Load(loaded),"empty journal is new"); file.Save(state);
        Throws([&] { FileJournal other(path); },"journal has one writer");
        state.revision=2; file.Save(state);
    }
    {
        FileJournal file(path); State loaded;
        Require(file.Load(loaded) && loaded.revision==2 && EncodeState(loaded)==EncodeState(state),
                "durable journal reconstructs latest complete state");
    }
    std::ifstream read(path,std::ios::binary); const std::string bytes((std::istreambuf_iterator<char>(read)),{}); read.close();
    const auto broken=dir/"broken.bin";
    const auto write=[&](std::string data) { std::ofstream out(broken,std::ios::binary|std::ios::trunc); out.write(data.data(),static_cast<std::streamsize>(data.size())); };
    write(bytes.substr(0,bytes.size()-1));
    Throws([&] { FileJournal file(broken); State loaded; file.Load(loaded); },"torn final intent cannot be silently discarded");
    auto corrupt=bytes; corrupt[40]^=1; write(corrupt);
    Throws([&] { FileJournal file(broken); State loaded; file.Load(loaded); },"payload checksum verified");
    auto trailing=bytes+"x"; write(trailing);
    Throws([&] { FileJournal file(broken); State loaded; file.Load(loaded); },"trailing partial header rejected");
    const auto encoded=EncodeState(state);
    for (std::size_t n=0;n<encoded.size();++n)
        Throws([&] { (void)DecodeState(encoded.substr(0,n)); },"every truncated snapshot is rejected");
}

void ManagementControls() {
    Fixture f; f.Open(); f.Confirm();
    const auto stale=f.engine.state().revision;
    f.engine.PauseOpenings(stale);
    Require(f.engine.state().openings_paused && DecodeState(f.store.saved).openings_paused,"pause persisted before acknowledgement");
    Throws([&] { f.engine.ResumeOpenings(stale,{3000,30,{}}); },"stale commands rejected");
    f.engine.Admit(Opening(101,10,1001),20); f.engine.Tick();
    Require(f.engine.state().links.size()==1,"paused engine ignores new openings");
    f.engine.ApplyConfig(f.engine.state().revision,Config(SizingMode::FixedLots,"0.02"),{3000,30,{}});
    Require(f.engine.state().links[0].opening_rule->mode==SizingMode::LotRatio,
        "changing live sizing leaves active mapping rule frozen");
    f.CloseSource(2001,lot); f.engine.Tick(); f.Confirm();
    Require(f.engine.state().links.empty() && f.port.submissions.size()==2,"pause continues existing closes");
    auto next=Config(SizingMode::FixedLots,"0.02"); next.relationships[0].id=2;
    f.engine.ApplyConfig(f.engine.state().revision,next,{3000,30,{{10,101}}});
    Require(f.engine.state().openings_paused && f.engine.state().intents.empty() && f.engine.state().next_intent_id==3 &&
        f.engine.state().config_json==WriteConfigJson(next),"apply preserves request counter and remains paused after cleanup");
    Throws([&] { Engine strict(Config(),"test-server-6182",f.port,f.store); },"unmanaged bootstrap mismatch still rejected");
    Engine managed([]()->CopierConfig { throw std::runtime_error("bootstrap must not be read on managed restart"); },
        "test-server-6182",f.port,f.store,true);
    managed.Start({4000,40,{{10,101}}});
    Require(managed.state().openings_paused,"managed restart retains pause and ignores stale bootstrap");
    managed.ResumeOpenings(managed.state().revision,{4000,40,{{10,101}}});
    auto paused_source=Opening(101,10,1001); paused_source.time_ms=5000;
    managed.Admit(paused_source,41);
    Require(managed.state().links.empty(),"resume skips sources that existed during pause");
    f.port.sources[102]=Source(); f.port.sources[102].first_fill_ms=5000; f.port.now=5000;
    auto fresh=Opening(102,10,1002); fresh.time_ms=5000;
    managed.Admit(fresh,42); managed.Tick();
    Require(f.port.submissions.size()==3 && f.port.submissions.back().id==3 &&
        f.port.submissions.back().units==2*lot/100,"new config used with monotonic intent IDs after migration");

    Fixture candidate;
    candidate.engine.Admit(Opening(),11);
    candidate.engine.PauseOpenings(candidate.engine.state().revision);
    candidate.engine.Tick();
    Require(candidate.engine.state().links.empty() && candidate.port.submissions.empty(),"pause cancels and clears unsubmitted candidates");
    Fixture submitted; submitted.Open(); submitted.engine.PauseOpenings(submitted.engine.state().revision); submitted.Confirm();
    Require(submitted.engine.state().links[0].phase==Phase::Active,"pause still confirms requests already submitted");
    const auto before=EncodeState(submitted.engine.state());
    const auto evidence=submitted.engine.InspectIntent(1);
    Require(evidence.find("完整成交")!=std::string::npos && EncodeState(submitted.engine.state())==before &&
        submitted.port.submissions.size()==1,"inspection never modifies state or submits");
    submitted.engine.ReportFault("uncertain");
    submitted.engine.ResumeOpenings(submitted.engine.state().revision,{5000,50,{}});
    Require(!submitted.engine.state().openings_paused,"runtime warning does not block operator resume");
    Fixture failed; failed.store.fail_at=failed.store.saves+1;
    Throws([&] { failed.engine.PauseOpenings(failed.engine.state().revision); },"pause not acknowledged after disk failure");
    failed.engine.Admit(Opening(),11); failed.engine.Tick();
    Require(failed.port.submissions.empty() && failed.engine.storage_degraded() && failed.engine.state().halt_reason.empty(),"disk failure prevents more execution");
    Fixture invalid; invalid.engine.PauseOpenings(invalid.engine.state().revision);
    auto disabled=Config(); disabled.enabled=false;
    Throws([&] { invalid.engine.ApplyConfig(invalid.engine.state().revision,disabled,{3000,30,{}}); },"disabled runtime configuration rejected");

    Fixture apply_failed; apply_failed.engine.PauseOpenings(apply_failed.engine.state().revision);
    apply_failed.store.fail_at=apply_failed.store.saves+1;
    Throws([&] { apply_failed.engine.ApplyConfig(apply_failed.engine.state().revision,next,{3000,30,{}}); },
        "failed config commit is not acknowledged");
    Require(apply_failed.engine.state().config_json==WriteConfigJson(Config()) &&
        apply_failed.engine.storage_degraded() && apply_failed.engine.state().halt_reason.empty(),"failed apply keeps last known config and reports degraded storage");
    Fixture reviewed; reviewed.Open(); reviewed.Confirm(); reviewed.port.positions[{20,9001}]=2*lot; reviewed.engine.Tick();
    reviewed.engine.PauseOpenings(reviewed.engine.state().revision);
    reviewed.engine.ResumeOpenings(reviewed.engine.state().revision,{3000,30,{}});
    Require(!reviewed.engine.state().openings_paused,"individual review does not block healthy new signals");
    Require(!reviewed.engine.ApplyBlockReason().empty(),"review mappings block apply");

    // Schema 1 payloads do not have the final persisted pause field.
    auto legacy=LegacyPayload(f.engine.state(),1);
    Require(!DecodeState(legacy).openings_paused,"old journal schema remains readable");
}

// Upgrade the exact pre-submission Review branch written by 0.4.7. Do not
// infer that a position is harmless merely from target_position == 0.
void LegacyUnsubmittedReviewCleanup() {
    auto config=Config(); config.relationships.push_back({2,10,30,true,SizingMode::FixedLots,"0.05"});
    State original; original.identity="legacy-reviews"; original.config_json=WriteConfigJson(config);
    original.revision=2431; original.activation_ms=1000; original.activation_sequence=10;
    original.openings_paused=true; original.next_intent_id=2089;
    for (std::size_t n=0;n<1360;++n) {
        Link link; link.opening_rule=config.relationships[n%2]; link.rule=link.opening_rule->id;
        link.source=Opening(100+n/2,10,1000+n/2); link.follower_symbol=link.source.symbol;
        link.phase=Phase::Review; link.reason="source_opening_predates_activation_or_already_closed";
        original.links.push_back(std::move(link));
    }
    const auto root=std::filesystem::temp_directory_path()/("tcx-legacy-reviews-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p,e); } } cleanup{root};
    const auto path=root/"legacy.journal";
    // A real schema 4 checkpoint, rather than a new-schema-only fixture.
    const auto payload=Schema4Payload(original);
    std::uint32_t crc=0xFFFFFFFFU;
    for (unsigned char c:payload) {
        crc^=c;
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xEDB88320U:0);
    }
    std::string frame="TCXJ0002";
    for (Id value:{static_cast<Id>(payload.size()),original.revision,static_cast<Id>(crc^0xFFFFFFFFU)})
        for (unsigned byte=0;byte<8;++byte) frame.push_back(static_cast<char>((value>>(byte*8))&255));
    { std::ofstream out(path,std::ios::binary); out<<frame<<payload; }
    State cleaned;
    {
        FileJournal file(path); MemoryStore mirror; FakePort port(mirror); port.now=4000;
        Engine engine([]()->CopierConfig { throw std::runtime_error("must retain journal config"); },
            original.identity,port,file,true);
        engine.Start({4000,40,{}});
        cleaned=engine.state();
        const auto snapshot=management::Snapshot(engine);
        Require(cleaned.links.empty() && cleaned.intents.empty() && snapshot.review==0 && snapshot.apply_block.empty(),
            "all 1360 legacy unsubmitted reviews are durably cleaned and management unblocked");
        Require(cleaned.openings_paused && cleaned.halt_reason.empty() && cleaned.next_intent_id==2089 &&
            cleaned.config_json==original.config_json,"cleanup preserves pause, next request ID and authoritative configuration");
        Require(port.submissions.empty() && port.source_reads==0 && port.reconcile_reads==0,
            "cleanup of proven unsubmitted rows neither queries unknown trade results nor sends trades");
        Require(std::count_if(port.logs.begin(),port.logs.end(),[](const std::string& line) {
            return line.find("MIGRATION cleared_legacy_unsubmitted_reviews=1360;")!=std::string::npos;
        })==1,"successful migration logs the exact cleanup count once");
    }
    {
        FileJournal file(path); State replay; Require(file.Load(replay) && EncodeState(replay)==EncodeState(cleaned),
            "mixed legacy/delta/prune journal replays exact cleaned state");
    }
    {
        FileJournal file(path);
        MemoryStore mirror; FakePort port(mirror); port.now=5000;
        Engine engine(config,original.identity,port,file,true); engine.Start({5000,50,{}});
        Require(engine.state().links.empty() && engine.state().openings_paused && engine.ApplyBlockReason().empty(),
            "second restart is idempotent and remains paused");
        Require(std::none_of(port.logs.begin(),port.logs.end(),[](const std::string& line) {
            return line.find("MIGRATION")!=std::string::npos;
        }),"already cleaned rows are not migrated again");
        engine.ResumeOpenings(engine.state().revision,{5000,50,{}});
        engine.Admit(Opening(),51); engine.Tick();
        Require(port.submissions.empty() && engine.state().links.empty(),"resume cannot catch up any retired old opening");
    }
    {
        State mixed=original; mixed.links.resize(1);
        auto retained=mixed.links[0]; retained.source=Opening(900,10,1900);
        retained.reason="source_evidence_invalid: incomplete_history"; mixed.links.push_back(retained);
        auto active=retained; active.source=Opening(901,10,1901); active.phase=Phase::Active; active.reason.clear();
        active.source_initial=active.source_remaining=active.follower_remaining=lot; active.follower_position=9001;
        mixed.links.push_back(active);
        Intent i; i.id=2088; i.link=2; i.follower_login=20; i.symbol=active.follower_symbol; i.units=lot;
        i.state=IntentState::Confirmed; i.result_position=9001; i.filled=lot; i.deals={70001}; mixed.intents.push_back(i);
        MemoryStore store; store.saved=EncodeState(mixed); FakePort port(store);
        port.sources[901]=Source(); port.positions[{20,9001}]=lot;
        Engine engine(config,mixed.identity,port,store,true); engine.Start({4000,40,{{10,901}}});
        Require(engine.state().links.size()==2 && engine.state().links[0].source.position==900 &&
            engine.state().links[0].reason==retained.reason && engine.state().links[1].phase==Phase::Active,
            "mixed state cleans only legacy skip while preserving other review and active mapping");
        Require(engine.state().intents.size()==1 && engine.state().intents[0].link==1 &&
            engine.state().intents[0].id==2088 && engine.state().links[1].follower_position==9001,
            "cleanup remaps surviving requests without changing target positions or request IDs");
        Require(!engine.ApplyBlockReason().empty() && port.submissions.empty(),"other review still blocks management after partial cleanup");
    }
    // Every form of trading evidence must defeat cleanup, even if the old
    // reason and target ticket misleadingly look like a harmless skip.
    for (unsigned variant=0;variant<12;++variant) {
        State seed=original; seed.links.resize(1);
        auto& link=seed.links[0];
        switch (variant) {
        case 0: link.reason="source_history_invalid"; break;
        case 1: link.reason+=" extra"; break;
        case 2: link.source_initial=lot; break;
        case 3: link.source_initial=link.source_remaining=lot; break;
        case 4: link.follower_position=9001; break;
        case 5: link.follower_remaining=lot; break;
        case 6: link.processed_closes.push_back(2001); break;
        default: {
            Intent i; i.id=2088; i.link=0; i.follower_login=20; i.symbol=link.follower_symbol;
            i.units=lot; i.created_ms=2000; i.state=static_cast<IntentState>(variant-7); seed.intents.push_back(i);
        } }
        MemoryStore store; store.saved=EncodeState(seed); FakePort port(store); port.now=4000;
        Engine engine(config,seed.identity,port,store,true); engine.Start({4000,40,{}});
        Require(engine.state().links.size()==1 && engine.state().links[0].phase==Phase::Review &&
            engine.state().intents.size()==seed.intents.size(),"never clean unrelated reviews or any request state");
        Require(!engine.ApplyBlockReason().empty() && port.submissions.empty(),"genuine review stays blocked without replay");
        engine.ResumeOpenings(engine.state().revision,{5000,50,{}});
        Require(engine.state().links.size()==1,"resume retains reviewed trading evidence");
    }
    {
        State seed=original; seed.links.resize(1); seed.halt_reason="manual_recovery_required";
        MemoryStore store; store.saved=EncodeState(seed); FakePort port(store);
        Engine engine(config,seed.identity,port,store,true); engine.Start({4000,40,{}});
        Require(engine.state().links.empty() && engine.state().halt_reason.empty() && !engine.warning().empty(),
            "old HALT becomes a diagnostic while provably unsubmitted rows retire");
    }
    // Both durable boundaries: skipped snapshot, then logical pruning. A failed
    // write must leave enough state to recover safely without a single trade.
    for (unsigned fail_at:{1U,2U}) {
        State seed=original; seed.links.resize(1); seed.links[0].reason="source_opening_expired";
        MemoryStore store; store.saved=EncodeState(seed); store.fail_at=fail_at; FakePort port(store);
        Engine engine(config,seed.identity,port,store,true);
        engine.Start({4000,40,{}});
        Require(engine.storage_degraded() && engine.state().halt_reason.empty() && DecodeState(store.saved).links.size()==1 && port.submissions.empty(),
            "failed cleanup preserves old durable evidence and reports degraded storage");
        store.fail_at=0;
        Engine retry(config,seed.identity,port,store,true); retry.Start({5000,50,{}});
        Require(retry.state().links.empty() && retry.state().openings_paused && retry.state().next_intent_id==2089,
            "restart completes either interrupted cleanup boundary without resetting identity counters");
    }
}

void AddSubscriptionsWithOpenPositions() {
    Fixture f(Config(SizingMode::LotRatio,"2")); f.Open(); f.Confirm();
    f.engine.PauseOpenings(f.engine.state().revision);
    f.CloseSource(2001,40*lot/100); f.engine.Tick(); f.Confirm();
    Require(f.engine.state().links[0].source_remaining==60*lot/100 &&
        f.engine.state().links[0].follower_remaining==120*lot/100,"existing position is partially closed before addition");
    auto next=Config(SizingMode::LotRatio,"2");
    next.relationships.push_back({2,11,20,true,SizingMode::FixedLots,"0.02"});
    next.relationships.push_back({3,10,30,true,SizingMode::LotRatio,"0.5"});
    Require(f.engine.ApplyBlockReason().empty() && management::Snapshot(f.engine).apply_block.empty(),
        "healthy active positions enable apply in both engine and management response");
    auto expected=f.engine.state();
    expected.config_json=WriteConfigJson(next); ++expected.revision;
    expected.activation_ms=3000; expected.activation_sequence=30;
    expected.baseline={{10,100},{10,101},{11,200}};
    f.engine.ApplyConfig(f.engine.state().revision,next,{3000,30,expected.baseline});
    Require(EncodeState(f.engine.state())==EncodeState(expected) && f.store.saved==EncodeState(expected),
        "one atomic config commit retains every active link, volume, processed close, intent and ID");
    Require(f.port.submissions.size()==2,"adding subscriptions neither closes old targets nor backfills new ones");
    f.CloseSource(2002,20*lot/100); f.engine.Tick();
    Require(f.port.submissions.back().close && f.port.submissions.back().follower_position==9001 &&
        f.port.submissions.back().follower_login==20 && f.port.submissions.back().units==40*lot/100,
        "old partial close uses original target and actual remainders while paused after apply");
    f.Confirm();

    Engine restarted(Config(),"test-server-6182",f.port,f.store,true);
    restarted.Start({4000,40,{{10,100},{10,101},{11,200}}});
    Require(restarted.state().openings_paused && restarted.state().config_json==WriteConfigJson(next) &&
        restarted.state().links[0].phase==Phase::Active && restarted.state().links[0].follower_remaining==80*lot/100,
        "restart restores new subscriptions and original active position without bootstrap replacement");
    restarted.ResumeOpenings(restarted.state().revision,{5000,50,{{10,100},{10,101},{11,200}}});
    for (auto source:{Opening(100),Opening(101,10,1001),Opening(200,11,1002)}) {
        source.time_ms=6000; restarted.Admit(source,51);
    }
    auto queued=Opening(202,11,1003); queued.time_ms=6000; restarted.Admit(queued,50);
    queued.time_ms=5000; restarted.Admit(queued,51);
    Require(restarted.state().links.size()==1 && f.port.submissions.size()==3,
        "baseline, callback watermark and activation time all prevent old-source catch-up for added rules");

    const auto confirm=[&] {
        const auto found=std::find_if(restarted.state().intents.begin(),restarted.state().intents.end(),
            [](const Intent& i) { return i.state==IntentState::Submitted; });
        Require(found!=restarted.state().intents.end(),"confirmation targets an unresolved request");
        f.port.Fill(found->id,found->units); restarted.Tick();
    };
    f.port.now=6000; f.port.sources[201]=Source(); f.port.sources[201].first_fill_ms=6000;
    auto fresh=Opening(201,11,1004); fresh.time_ms=6000;
    restarted.Admit(fresh,51); restarted.Tick();
    Require(f.port.submissions.back().id==4 && !f.port.submissions.back().close &&
        f.port.submissions.back().follower_login==20 && f.port.submissions.back().units==2*lot/100,
        "new source subscription opens fixed volume independently on existing follower account");
    confirm();
    f.port.now=7000; f.port.sources[102]=Source(); f.port.sources[102].first_fill_ms=7000;
    fresh=Opening(102,10,1005); fresh.time_ms=7000;
    restarted.Admit(fresh,52); restarted.Tick(); confirm(); confirm();
    Require(f.port.submissions.size()==6 && !f.port.submissions[4].close && !f.port.submissions[5].close,
        "new source position creates exactly one opening per old and added relationship");
    const auto& a=f.port.submissions[4]; const auto& b=f.port.submissions[5];
    Require((a.follower_login==20 && a.units==2*lot && b.follower_login==30 && b.units==lot/2) ||
        (b.follower_login==20 && b.units==2*lot && a.follower_login==30 && a.units==lot/2),
        "old and newly added follower rules retain independent sizing");
    f.CloseSource(2003,40*lot/100); restarted.Tick();
    Require(f.port.submissions.back().id==7 && f.port.submissions.back().close &&
        f.port.submissions.back().follower_position==9001 && f.port.submissions.back().units==80*lot/100,
        "original position fully closes after config apply, resume and restart");
    confirm();
    Require(restarted.state().links.size()==3 && f.port.positions[{20,9001}]==0 &&
        f.port.positions[{20,9004}]==2*lot/100,"old full close leaves the new subscription position intact");
}

void FrozenRulesAcrossEdits() {
    for (auto mode:{SizingMode::LotRatio,SizingMode::EquityRatio,SizingMode::FixedLots}) {
        Fixture f(Config(mode,mode==SizingMode::FixedLots?"0.80":"2"));
        f.Open(); f.Confirm();
        const auto old_total=f.engine.state().links[0].follower_remaining;
        f.engine.PauseOpenings(f.engine.state().revision);
        auto next=Config(SizingMode::FixedLots,"0.20");
        f.engine.ApplyConfig(f.engine.state().revision,next,{3000,30,{{10,100}}});
        f.CloseSource(2001,lot/4); f.engine.Tick();
        Require(f.port.submissions.back().close && f.port.submissions.back().units==old_total/4,
            "old mapping closes proportionally to its actual opening volume after sizing edit");
        f.Confirm();
        f.engine.ResumeOpenings(f.engine.state().revision,{4000,40,{{10,100}}});
        auto fresh=Opening(101,10,1001); fresh.time_ms=5000;
        f.port.sources[101]=Source(); f.port.sources[101].first_fill_ms=5000; f.port.now=5000;
        f.engine.Admit(fresh,41);
        for (unsigned n=0;n<5 && f.port.submissions.size()<3;++n) f.engine.Tick();
        Require(f.port.submissions.size()==3 && f.port.submissions.back().units==lot/5,
            "same relationship ID opens new source positions with the new sizing mode");
        f.Confirm();
        auto view=tradecopier::management::Snapshot(f.engine);
        Require(view.links[0][10].find("0.20")!=std::string::npos &&
            f.engine.state().links[0].opening_rule->mode==mode,
            "management displays the mapping's frozen rule instead of the live configuration");
        f.engine.PauseOpenings(f.engine.state().revision);
        next.relationships[0]={1,11,30,true,SizingMode::LotRatio,"0.5"};
        f.engine.ApplyConfig(f.engine.state().revision,next,{6000,60,{}});
        Engine restarted(Config(),"test-server-6182",f.port,f.store,true);
        restarted.Start({6100,61,{}});
        Require(restarted.state().links[0].phase==Phase::Active && restarted.state().links[1].phase==Phase::Active,
            "restart keeps two generations of one rule on the original source and target accounts");
        restarted.ResumeOpenings(restarted.state().revision,{6500,65,{}});
        fresh=Opening(102,11,1002); fresh.time_ms=7000;
        f.port.sources[102]=Source(); f.port.sources[102].first_fill_ms=7000; f.port.now=7000;
        restarted.Admit(fresh,66);
        for (unsigned n=0;n<5 && f.port.submissions.size()<4;++n) restarted.Tick();
        Require(f.port.submissions.size()==4 && f.port.submissions.back().follower_login==30 &&
            f.port.submissions.back().units==lot/2,"edited accounts apply only to new source positions");
        f.port.Fill(4,lot/2); restarted.Tick();
        restarted.PauseOpenings(restarted.state().revision);
        next.relationships.push_back({2,12,40,true,SizingMode::LotRatio,"1"});
        next.relationships[0].enabled=false;
        restarted.ApplyConfig(restarted.state().revision,next,{7500,75,{}});
        next.relationships.erase(next.relationships.begin());
        restarted.ApplyConfig(restarted.state().revision,next,{8000,80,{}});
        Engine retired(Config(),"test-server-6182",f.port,f.store,true);
        retired.Start({8100,81,{}});
        Require(retired.state().links.size()==3 && retired.state().halt_reason.empty(),
            "disabled then deleted subscription remains recoverable for all three live mapping generations");
        for (Id position:{100ULL,101ULL,102ULL}) {
            auto& source=f.port.sources[position];
            source.closes.push_back({2000+position,source.remaining,9000}); source.remaining=0;
        }
        f.port.now=9000;
        for (unsigned n=0;n<15;++n) {
            retired.Tick();
            for (const auto& intent:retired.state().intents)
                if (intent.state==IntentState::Submitted) f.port.Fill(intent.id,intent.units);
        }
        Require(f.port.submissions.size()==7,"exactly one remaining close per old mapping, no extra openings or retries");
        Require(retired.state().links.empty() && retired.state().intents.empty(),"retired relationships fully close and leave no full trade history");
        for (const auto& position:f.port.positions) Require(!position.second,"all retired target positions are zero");
        for (std::size_t n=4;n<7;++n) {
            const auto& intent=f.port.submissions[n];
            Require(intent.close && intent.follower_login==(intent.follower_position==9004?30:20) &&
                intent.units==(intent.follower_position==9001?old_total*3/4:intent.follower_position==9003?lot/5:lot/2),
                "each generation uses its own account and remaining volume after deletion and restart");
        }
    }
}

void LegacyRuleMigration() {
    Fixture f; f.Open(); f.Confirm(); f.engine.PauseOpenings(f.engine.state().revision);
    for (unsigned schema:{1U,2U}) {
        auto imported=DecodeState(LegacyPayload(f.engine.state(),schema));
        Require(imported.links[0].opening_rule && imported.links[0].opening_rule->parameter=="1",
            "legacy live mappings acquire their original rule before editing");
        MemoryStore legacy; legacy.saved=LegacyPayload(f.engine.state(),schema);
        FakePort port(legacy); port.sources=f.port.sources; port.positions=f.port.positions;
        Engine engine(Config(),"test-server-6182",port,legacy,true); engine.Start({3000,30,{{10,100}}});
        if (!engine.state().openings_paused) engine.PauseOpenings(engine.state().revision);
        engine.ApplyConfig(engine.state().revision,Config(SizingMode::LotRatio,"0.5"),{4000,40,{{10,100}}});
        const auto durable=DecodeState(legacy.saved);
        Require(legacy.saved[0]==7 && durable.links[0].opening_rule->parameter=="1" &&
            durable.config_json==WriteConfigJson(Config(SizingMode::LotRatio,"0.5")),
            "legacy upgrade persists old snapshot separately from changed live configuration");
    }
    State closed=f.engine.state(); closed.links[0].phase=Phase::Closed;
    closed.links[0].source_remaining=closed.links[0].follower_remaining=0;
    auto other=Config(); other.relationships[0]={2,11,30,true,SizingMode::FixedLots,"0.02"};
    closed.config_json=WriteConfigJson(other);
    Require(!DecodeState(LegacyPayload(closed)).links[0].opening_rule,
        "unknown legacy terminal policy is left unknown instead of borrowing a new rule");
    auto broken=f.engine.state(); broken.links[0].opening_rule.reset();
    Throws([&] { DecodeState(EncodeState(broken)); },"schema 3 active mappings cannot fall back to mutable current rules");
    broken=f.engine.state(); broken.links[0].opening_rule->follower_login=99;
    Throws([&] { DecodeState(EncodeState(broken)); },"snapshot must agree with historical intent account");
}

void LegacyJournalUpgrade() {
    Fixture f; f.Open(); f.Confirm(); f.engine.PauseOpenings(f.engine.state().revision);
    auto original=f.engine.state(); original.revision=1;
    const auto path=std::filesystem::temp_directory_path()/("tcx-legacy-rules-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".journal");
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove(p,e); } } cleanup{path};
    const auto frame=[](const State& state) {
        const auto payload=LegacyPayload(state);
        std::uint32_t crc=0xFFFFFFFFU;
        for (unsigned char c:payload) {
            crc^=c;
            for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xEDB88320U:0);
        }
        std::string record="TCXJ0001";
        for (Id value:{static_cast<Id>(payload.size()),state.revision,static_cast<Id>(crc^0xFFFFFFFFU)})
            for (unsigned byte=0;byte<8;++byte) record.push_back(static_cast<char>((value>>(byte*8))&255));
        return record+payload;
    };
    const auto write=[&](const std::string& data) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.write(data.data(),static_cast<std::streamsize>(data.size()));
    };
    write(frame(original));
    State changed=original; changed.revision=2; changed.activation_ms=3000;
    changed.config_json=WriteConfigJson(Config(SizingMode::FixedLots,"0.02"));
    {
        FileJournal journal(path); State loaded;
        Require(journal.Load(loaded) && loaded.links[0].opening_rule->parameter=="1","real legacy journal restores live rule");
        journal.Save(changed);
    }
    {
        FileJournal journal(path); State loaded;
        Require(journal.Load(loaded) && loaded.config_json==changed.config_json &&
            loaded.links[0].opening_rule->parameter=="1","mixed schema journal preserves old rule across a new config commit");
    }
    // The same edit in schema 2 is unsafe: that format has no immutable rule.
    write(frame(original)+frame(changed));
    Throws([&] { FileJournal journal(path); State loaded; journal.Load(loaded); },
        "schema 2 history cannot retroactively change an active mapping policy");
    auto equivalent=original; equivalent.revision=2;
    equivalent.config_json=WriteConfigJson(Config(SizingMode::LotRatio,"1.000"));
    write(frame(original)+frame(equivalent));
    {
        FileJournal journal(path); State loaded;
        Require(journal.Load(loaded) && loaded.links[0].opening_rule->parameter=="1.000",
            "legacy equivalent numeric configuration changes still load");
        loaded.revision=3; journal.Save(loaded);
    }
    { // Even with valid checksums and increasing revisions, no old-format fallback.
        std::ofstream file(path,std::ios::binary|std::ios::app); equivalent.revision=4;
        const auto data=frame(equivalent); file.write(data.data(),static_cast<std::streamsize>(data.size()));
    }
    Throws([&] { FileJournal journal(path); State loaded; journal.Load(loaded); },"journal cannot downgrade after binding snapshots are persisted");
}

void ActiveConfigGuards() {
    Fixture f; f.Open(); f.Confirm(); f.engine.PauseOpenings(f.engine.state().revision);
    auto next=Config(); next.relationships.push_back({2,11,30,true,SizingMode::FixedLots,"0.02"});
    const auto before=EncodeState(f.engine.state()); const auto durable=f.store.saved;
    const auto reject=[&](const CopierConfig& candidate) {
        Require(!f.engine.ConfigChangeBlockReason(candidate).empty(),"invalid configuration rejected before server validation");
        Throws([&] { f.engine.ApplyConfig(f.engine.state().revision,candidate,{3000,30,{}}); },
               "direct engine calls cannot bypass config validation");
        Require(EncodeState(f.engine.state())==before && f.store.saved==durable && f.port.submissions.size()==1,
            "rejected config leaves active execution and durable state unchanged");
    };
    auto changed=next; changed.enabled=false; reject(changed);
    changed=next; changed.relationships[1]={2,20,10,true,SizingMode::LotRatio,"1"}; reject(changed);
    changed=next; changed.relationships[1].parameter="0"; reject(changed);
    changed=next; changed.relationships[1].id=1; reject(changed);
    Throws([&] { f.engine.ApplyConfig(f.engine.state().revision-1,next,{3000,30,{}}); },"stale active-position apply refused");
    Throws([&] { f.engine.ApplyConfig(f.engine.state().revision,next,{0,30,{}}); },"invalid activation snapshot refused");
    Require(EncodeState(f.engine.state())==before,"invalid version or baseline does not change active state");
    // Parameter spelling and ordering are not changes to the trading rule.
    next.relationships[0].parameter="1.0000"; std::swap(next.relationships[0],next.relationships[1]);
    f.engine.ApplyConfig(f.engine.state().revision,next,{3000,30,{}});
    next.relationships[0].parameter="0.03"; next.relationships[0].enabled=false;
    f.engine.ApplyConfig(f.engine.state().revision,next,{3100,31,{}});
    next.relationships.erase(next.relationships.begin());
    f.engine.ApplyConfig(f.engine.state().revision,next,{3200,32,{}});
    Require(f.engine.state().links[0].phase==Phase::Active && f.port.submissions.size()==1,
        "unrelated idle relationship can be changed, disabled and removed without flattening another rule");

    for (auto mode:{SizingMode::LotRatio,SizingMode::EquityRatio,SizingMode::FixedLots}) {
        auto config=Config(mode,"0.20"); config.relationships[0].id=UINT64_MAX;
        Fixture large(config); large.Open(); large.Confirm(); large.engine.PauseOpenings(large.engine.state().revision);
        config.relationships[0].parameter="0.2";
        config.relationships.push_back({2,11,30,true,SizingMode::LotRatio,"1"});
        large.engine.ApplyConfig(large.engine.state().revision,config,{3000,30,{}});
        Require(large.engine.state().links[0].rule==UINT64_MAX && large.engine.state().links[0].phase==Phase::Active,
            "full 64-bit active rule IDs and all sizing modes survive compatible apply");
    }
}

void ActiveConfigPendingAndFailure() {
    auto next=Config(SizingMode::FixedLots,"0.02"); next.relationships.push_back({2,11,30,true,SizingMode::LotRatio,"1"});
    Fixture f; f.Open();
    Require(!f.engine.ApplyBlockReason().empty(),"running engine still requires pausing new openings");
    f.engine.PauseOpenings(f.engine.state().revision);
    Throws([&] { f.engine.ApplyConfig(f.engine.state().revision,next,{3000,30,{}}); },"unconfirmed opening blocks apply");
    f.Confirm();
    Require(f.engine.ApplyBlockReason().empty(),"opening confirmation unlocks apply without closing its position");
    f.CloseSource(2001,lot/2); f.engine.Tick();
    Throws([&] { f.engine.ApplyConfig(f.engine.state().revision,next,{3000,30,{}}); },"unconfirmed partial close blocks apply");
    f.Confirm(); f.engine.ApplyConfig(f.engine.state().revision,next,{3000,30,{}});
    Require(f.engine.state().links[0].phase==Phase::Active && f.engine.state().links[0].follower_remaining==lot/2,
        "confirmation of partial close unlocks config while position remains open");
    f.engine.ReportFault("injected");
    f.engine.ApplyConfig(f.engine.state().revision,next,{4000,40,{}});
    Require(f.engine.state().halt_reason.empty(),"runtime warning cannot block valid configuration");

    Fixture failed; failed.Open(); failed.Confirm(); failed.engine.PauseOpenings(failed.engine.state().revision);
    const auto durable=failed.store.saved;
    failed.store.fail_at=failed.store.saves+1;
    Throws([&] { failed.engine.ApplyConfig(failed.engine.state().revision,next,{3000,30,{}}); },"active-position apply requires durable commit");
    Require(failed.store.saved==durable && failed.engine.state().config_json==WriteConfigJson(Config()) &&
        failed.engine.state().links[0].phase==Phase::Active && failed.engine.storage_degraded() && failed.engine.state().halt_reason.empty(),
        "failed save retains original mapping and config while stopping further execution");
    failed.CloseSource(2001,lot); failed.engine.Tick();
    Require(failed.port.submissions.size()==1,"disk failure cannot trigger an unjournaled close");

    struct LostAckStore:MemoryStore {
        bool lose_ack=false;
        void Save(const State& state) override { MemoryStore::Save(state); if (lose_ack) throw std::runtime_error("lost durable acknowledgement"); }
    } store;
    FakePort port(store); port.sources[100]=Source();
    Engine engine(Config(),"lost-ack",port,store,true); engine.Start({1000,10,{}});
    engine.Admit(Opening(),11); engine.Tick(); port.Fill(1,lot); engine.Tick();
    engine.PauseOpenings(engine.state().revision); store.lose_ack=true;
    Throws([&] { engine.ApplyConfig(engine.state().revision,next,{3000,30,{{10,100}}}); },"lost commit acknowledgement is not reported as success");
    Require(engine.state().config_json==WriteConfigJson(Config()) && engine.storage_degraded() && engine.state().halt_reason.empty() &&
        DecodeState(store.saved).config_json==WriteConfigJson(next),"uncertain commit reports storage degradation while durable state remains authoritative");
    store.lose_ack=false;
    Engine recovered(Config(),"lost-ack",port,store,true); recovered.Start({4000,40,{{10,100}}});
    Require(recovered.state().config_json==WriteConfigJson(next) && recovered.state().openings_paused &&
        recovered.state().links[0].phase==Phase::Active && port.submissions.size()==1,
        "reload resolves lost config acknowledgement without losing or reissuing the active position");
}

void ActiveConfigRealJournal() {
    const auto dir=std::filesystem::temp_directory_path()/("tcx-active-config-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p,e); } } cleanup{dir};
    const auto path=dir/"active.journal";
    struct DurableStore:MemoryStore {
        FileJournal file;
        explicit DurableStore(const std::filesystem::path& p):file(p) {}
        bool Load(State& state) override {
            if (!file.Load(state)) return false;
            saved=EncodeState(state); return true;
        }
        void Save(const State& state) override { file.Save(state); saved=EncodeState(state); }
        void Compact(const State& state) override { file.Compact(state); saved=EncodeState(state); }
    };
    auto next=Config(SizingMode::FixedLots,"0.02"); next.relationships.push_back({2,11,30,true,SizingMode::FixedLots,"0.02"});
    {
        DurableStore store(path); FakePort port(store); port.sources[100]=Source();
        Engine engine(Config(),"active-file",port,store,true);
        engine.Start({1000,10,{}}); engine.Admit(Opening(),11); engine.Tick();
        port.Fill(1,lot); engine.Tick(); engine.PauseOpenings(engine.state().revision);
        engine.ApplyConfig(engine.state().revision,next,{3000,30,{{10,100}}});
    }
    {
        DurableStore store(path); FakePort port(store); port.sources[100]=Source(); port.positions[{20,9001}]=lot;
        Engine engine(Config(),"active-file",port,store,true); engine.Start({4000,40,{{10,100}}});
        Require(engine.state().config_json==WriteConfigJson(next) && engine.state().openings_paused &&
            engine.state().links[0].phase==Phase::Active,"real journal restores changed config with an open mapping");
        auto& source=port.sources[100]; source.remaining=0; source.closes.push_back({2001,lot,5000}); port.now=5000;
        engine.Tick();
        Require(port.submissions.size()==1 && port.submissions.back().id==2 &&
            port.submissions.back().follower_login==20 && port.submissions.back().follower_position==9001 &&
            port.submissions.back().close && port.submissions.back().units==lot,
            "file-backed restart closes original mapped target with monotonic request ID");
        port.results[2]={Result::Status::Filled,9001,lot,10009,{70002}}; port.positions[{20,9001}]=0;
        engine.Tick();
        Require(engine.state().links.empty() && DecodeState(store.saved).links.empty() && DecodeState(store.saved).next_intent_id==3,
            "old position close and monotonic ID remain durable after compatible config reload");
    }
}

void ConfigTransitionReplayGuards() {
    Fixture f; f.Open(); f.Confirm(); f.engine.PauseOpenings(f.engine.state().revision);
    State before=f.engine.state(); before.revision=1;
    auto config=Config(); config.relationships.push_back({2,11,30,true,SizingMode::FixedLots,"0.02"});
    State after=before; after.revision=2; after.config_json=WriteConfigJson(config);
    after.activation_ms=3000; after.activation_sequence=30; after.baseline={{10,100}};
    const auto root=std::filesystem::temp_directory_path()/("tcx-config-replay-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p,e); } } cleanup{root};
    unsigned file_id=0;
    const auto replay=[&](const State& first,const State& second,bool valid) {
        const auto path=root/(std::to_string(++file_id)+".journal");
        const auto check=[&] {
            { FileJournal journal(path); State empty; journal.Load(empty); journal.Save(first); journal.Save(second); }
            FileJournal journal(path); State loaded;
            Require(journal.Load(loaded) && EncodeState(loaded)==EncodeState(second),"compatible active config transition replays exactly");
        };
        if (valid) check();
        else Throws(check,"valid checksums cannot authorize changes to frozen rules or execution history");
    };
    replay(before,after,true);
    auto changed=after; auto bad=config; bad.relationships[0].follower_login=21; changed.config_json=WriteConfigJson(bad); replay(before,changed,true);
    bad=config; bad.relationships.erase(bad.relationships.begin()); changed=after; changed.config_json=WriteConfigJson(bad); replay(before,changed,true);
    bad=config; bad.relationships[0].enabled=false; changed=after; changed.config_json=WriteConfigJson(bad); replay(before,changed,true);
    bad=config; bad.relationships[0].parameter="2"; changed=after; changed.config_json=WriteConfigJson(bad); replay(before,changed,true);
    changed=after; changed.links[0].opening_rule->parameter="2"; replay(before,changed,false);
    changed=before; changed.revision=2; changed.links[0].opening_rule->parameter="2"; replay(before,changed,false);
    changed=after; changed.links[0].opening_rule.reset(); replay(before,changed,false);
    changed=after; changed.links[0].follower_position=12345; replay(before,changed,false);
    changed=after; changed.links[0].follower_remaining=lot/2; replay(before,changed,false);
    changed=after; changed.links[0].source_remaining=lot/2; replay(before,changed,false);
    changed=after; changed.links[0].processed_closes.push_back(888); replay(before,changed,false);
    changed=after; changed.links[0].source.id=999; replay(before,changed,false);
    changed=after; changed.links[0].phase=Phase::Closed; replay(before,changed,false);
    changed=after; changed.intents[0].units=lot/2; replay(before,changed,false);
    changed=after; changed.intents[0].deals[0]=999; replay(before,changed,false);
    changed=after; changed.intents[0].state=IntentState::Submitted; replay(before,changed,false);
    changed=after; changed.openings_paused=false; replay(before,changed,false);
    auto running=before; running.openings_paused=false; replay(running,after,false);
    auto pending=before; pending.links[0].phase=Phase::Opening; pending.intents[0].state=IntentState::Submitted;
    changed=pending; changed.revision=2; changed.config_json=after.config_json; replay(pending,changed,false);
    bad=config; bad.relationships[0].parameter="1.000"; changed=after; changed.config_json=WriteConfigJson(bad); replay(before,changed,true);
}

void ManagementProtocol() {
    namespace m=tradecopier::management;
    Fixture f; f.Open(); f.Confirm(); f.engine.PauseOpenings(f.engine.state().revision);
    auto view=m::Snapshot(f.engine); view.session="session"; view.config_path="D:\\中文\\config.json";
    auto encoded=m::EncodeResponse(view); auto decoded=m::DecodeResponse(encoded);
    Require(decoded.revision==view.revision && decoded.paused && decoded.active==1 &&
        decoded.links[0][8]=="1.00" && decoded.intents[0][6]=="已确认" && decoded.config_path==view.config_path,
        "management snapshot includes exact status, lots and UTF8 paths");
    m::Request request{m::Operation::Apply,"nonce",UINT64_MAX,0,WriteConfigJson(Config())};
    const auto wire=m::EncodeRequest(request); const auto read=m::DecodeRequest(wire);
    Require(read.revision==UINT64_MAX && read.config_json==request.config_json,"protocol retains full 64 bit revisions");
    for (std::size_t n=0;n<wire.size();++n) Throws([&] { (void)m::DecodeRequest(wire.substr(0,n)); },"truncated command refused");
    Throws([&] { (void)m::DecodeResponse(encoded+"x"); },"trailing response data refused");
    auto invalid=wire; invalid[8]=99;
    Throws([&] { (void)m::DecodeRequest(invalid); },"unknown management operation refused");
    invalid=wire; invalid[16]=static_cast<char>(255);
    Throws([&] { (void)m::DecodeRequest(invalid); },"oversized session refused");
    Throws([&] { (void)m::DecodeResponse(std::string(m::kMaxMessage+1,'x')); },"message cap enforced");
}
void ManagedJournalFile() {
    const auto path=std::filesystem::temp_directory_path()/("tcx-managed-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".journal");
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove(p,e); } } cleanup{path};
    MemoryStore unused; FakePort port(unused);
    auto changed=Config(SizingMode::FixedLots,"0.02");
    {
        FileJournal journal(path); Engine engine(Config(),"managed",port,journal);
        engine.Start({1000,10,{}}); engine.PauseOpenings(engine.state().revision);
        engine.ApplyConfig(engine.state().revision,changed,{2000,20,{}});
    }
    {
        FileJournal journal(path); Engine engine(Config(),"managed",port,journal,true);
        engine.Start({3000,30,{}});
        Require(engine.state().config_json==WriteConfigJson(changed) && engine.state().openings_paused,
            "real journal accepts atomic config transition and reloads authoritative config");
    }
}
CopierConfig Fanout(std::size_t count) {
    CopierConfig config; config.enabled=true;
    for (std::size_t n=0;n<count;++n) config.relationships.push_back({n+1,10,1000+n,true,SizingMode::LotRatio,"1"});
    return config;
}
void FillPending(FakePort& port,const Engine& engine,Id except=0) {
    for (const auto& intent:engine.state().intents)
        if (intent.state==IntentState::Submitted && intent.id!=except) port.Fill(intent.id,intent.units);
}
SymbolSpec TestSymbol(const std::string& name) {
    SymbolSpec s; s.name=name; s.currency_base=name.rfind("EUR",0)==0?"EUR":"XAU";
    s.currency_profit=s.currency_margin="USD"; s.contract_size=s.currency_base=="EUR"?100000:100;
    s.digits=s.currency_base=="EUR"?5:2; s.buy=s.sell=true; return s;
}
struct SymbolPort:FakePort {
    using FakePort::FakePort;
    std::map<Id,std::vector<SymbolSpec>> targets;
    std::map<std::pair<Id,Id>,std::string> position_symbols;
    unsigned resolutions=0;
    SymbolResolution ResolveSymbol(const RuleConfig& r,const std::string& source,Side side) override {
        ++resolutions; return ResolveTargetSymbol(TestSymbol(source),targets[r.follower_login],r.auto_symbol,r.source_suffix,side==Side::Buy);
    }
    VolumeLimits Limits(const RuleConfig&,const std::string& source,const std::string& target,Side,bool) override {
        Require(SameSymbolContract(TestSymbol(source),TestSymbol(target)),"limits compare actual source and target contract");
        return limits;
    }
    Units FollowerRemaining(Id login,Id position,const std::string& symbol,Side side) override {
        Require(position_symbols.at({login,position})==symbol,"target position checks use frozen target symbol, not source name");
        return FakePort::FollowerRemaining(login,position,symbol,side);
    }
    Submission Submit(const Intent& intent) override {
        const auto saved=DecodeState(store.saved);
        Require(saved.links.at(intent.link).follower_symbol==intent.symbol,"mapped target symbol is durable before submitting");
        if (!intent.close) position_symbols[{intent.follower_login,9000+intent.id}]=intent.symbol;
        else Require(position_symbols.at({intent.follower_login,intent.follower_position})==intent.symbol,
                     "partial/full closes never resolve another suffix");
        return FakePort::Submit(intent);
    }
};
void SymbolRoutingLifecycle() {
    auto config=Fanout(2);
    for (auto& rule:config.relationships) { rule.auto_symbol=true; rule.symbol_mask="XAUUSD*"; }
    const auto dir=std::filesystem::temp_directory_path()/("tcx-symbols-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path p; ~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);} } cleanup{dir};
    struct Disk:MemoryStore {
        FileJournal file;
        explicit Disk(const std::filesystem::path& p):file(p) {}
        bool Load(State& state) override { if (!file.Load(state)) return false; saved=EncodeState(state); return true; }
        void Save(const State& state) override { file.Save(state); MemoryStore::Save(state); }
        void Compact(const State& state) override { file.Compact(state); pruned_from=saved; saved=EncodeState(state); ++saves; }
    };
    std::map<std::pair<Id,Id>,Units> positions;
    std::map<std::pair<Id,Id>,std::string> symbols;
    State active;
    {
        Disk store(dir/"symbols.journal"); SymbolPort port(store);
        port.targets[1000]={TestSymbol("XAUUSD.p")}; port.targets[1001]={TestSymbol("XAUUSDp")};
        port.sources[100]=Source(); Engine engine(config,"server=1;api=6182;app=3101;symbol=XAUUSDp",port,store,true);
        engine.Start({1000,10,{}}); engine.Admit(Opening(),11); engine.Tick();
        Require(port.submissions.size()==2 && port.submissions[0].symbol!=port.submissions[1].symbol,
                "one source fans out to independently resolved p and .p account symbols");
        FillPending(port,engine); engine.Tick();
        const auto rows=management::Snapshot(engine).links;
        Require(rows[0][5].find(" → ")!=std::string::npos,"management shows both source and mapped target symbols");
        engine.PauseOpenings(engine.state().revision);
        auto next=config; for (auto& r:next.relationships) { r.symbol_mask="EURUSD*"; r.source_suffix="p"; }
        engine.ApplyConfig(engine.state().revision,next,{2500,20,{{10,100}}});
        Require(engine.state().links[0].opening_rule->symbol_mask=="XAUUSD*" &&
            engine.state().links[0].opening_rule->source_suffix.empty(),"editing symbols leaves original opening binding frozen");
        positions=port.positions; symbols=port.position_symbols; active=engine.state();
    }
    {
        Disk store(dir/"symbols.journal"); SymbolPort port(store);
        port.positions=positions; port.position_symbols=symbols; port.sources[100]=Source(); port.now=3000;
        // A changed/new candidate set cannot redirect a recovered old position.
        port.targets[1000]={TestSymbol("XAUUSDm")}; port.targets[1001]={TestSymbol("XAUUSD.c")};
        Engine engine(config,"server=1;api=6182;app=3101",port,store,true); engine.Start({3000,30,{{10,100}}});
        Require(port.resolutions==0 && engine.state().links[0].follower_symbol==active.links[0].follower_symbol,
                "restart restores target symbol and accepts legacy identity filter without resolving again");
        port.now=4000; port.sources[100].remaining=lot/2; port.sources[100].closes.push_back({2001,lot/2,4000});
        engine.Tick(); FillPending(port,engine); engine.Tick();
        Require(port.submissions.size()==2 && port.resolutions==0,"paused partial closes keep both frozen suffixes");
        port.now=5000; port.sources[100].remaining=0; port.sources[100].closes.push_back({2002,lot/2,5000});
        engine.Tick(); FillPending(port,engine); engine.Tick();
        Require(engine.state().links.empty() && engine.state().intents.empty(),"full closes settle and prune different-suffix mappings");
        for (const auto& p:port.positions) Require(p.second==0,"both target accounts are flat after source full close");
        engine.ResumeOpenings(engine.state().revision,{6000,60,{{10,100}}});
        port.now=7000; port.sources[101]=Source(); port.sources[101].first_fill_ms=7000;
        auto next=Opening(101,10,1001); next.time_ms=7000; next.symbol="EURUSDp";
        port.targets[1000]={TestSymbol("EURUSD.p")}; port.targets[1001]={TestSymbol("EURUSDp")};
        engine.Admit(next,61); engine.Tick(); FillPending(port,engine); engine.Tick();
        Require(engine.state().links.size()==2 && port.resolutions==2 && engine.state().links[0].source.symbol=="EURUSDp",
                "new positions use new symbol filter and resolve EURUSD instead of gold");
        auto excluded=Opening(102,10,1002); excluded.time_ms=7000;
        engine.Admit(excluded,62);
        Require(engine.state().links.size()==2,"new filter excludes gold without affecting previously managed gold closes");
        port.now=8000; port.sources[101].remaining=0; port.sources[101].closes.push_back({2003,lot,8000});
        engine.Tick(); FillPending(port,engine); engine.Tick();
        Require(engine.state().links.empty() && port.resolutions==2,"EUR suffix closes use saved mapping without another lookup");
    }
    // A valid CRC/new revision cannot change the symbol of an already opened position.
    {
        FileJournal file(dir/"immutable.journal"); State empty; file.Load(empty);
        auto before=active; before.revision=1; file.Save(before);
        auto after=before; ++after.revision; after.links[0].follower_symbol="XAUUSD.bad";
        for (auto& intent:after.intents) if (intent.link==0) intent.symbol="XAUUSD.bad";
        Throws([&]{file.Save(after);},"even mutually consistent link/intent edits cannot change a frozen target symbol");
    }
    {
        auto legacy=active; legacy.config_json=WriteConfigJson(Fanout(2));
        for (auto& link:legacy.links) link.follower_symbol=link.source.symbol;
        for (auto& intent:legacy.intents) intent.symbol=legacy.links[intent.link].source.symbol;
        auto prior=DecodeState(Schema4Payload(legacy));
        Require(prior.links[0].follower_symbol==prior.links[0].source.symbol && !prior.links[0].opening_rule->auto_symbol,
                "schema 4 reconstructs exact symbol binding without applying current automatic routing");
    }
}
void SymbolConflictIsolation() {
    auto config=Fanout(2); for (auto& r:config.relationships) r.auto_symbol=true;
    MemoryStore store; SymbolPort port(store); port.sources[100]=Source();
    port.targets[1000]={TestSymbol("XAUUSDp"),TestSymbol("XAUUSD.p")};
    port.targets[1001]={TestSymbol("XAUUSD.p")};
    Engine engine(config,"symbols",port,store,true); engine.Start({1000,10,{}});
    engine.Admit(Opening(),11); engine.Tick(); FillPending(port,engine); engine.Tick();
    Require(port.submissions.size()==1 && port.submissions[0].follower_login==1001 && engine.state().halt_reason.empty(),
            "ambiguous account receives no trade while another unique account continues");
    Require(engine.state().links[0].phase==Phase::Review && engine.state().links[0].reason.find("冲突")!=std::string::npos,
            "conflict remains visible with evidence and no speculative target symbol");
    engine.PauseOpenings(engine.state().revision);
    Require(engine.ApplyBlockReason().empty(),"known unsubmitted symbol block permits correcting future configuration");
    auto next=config; next.relationships[0].auto_symbol=false;
    engine.ApplyConfig(engine.state().revision,next,{2500,20,{{10,100}}});
    engine.ResumeOpenings(engine.state().revision,{3000,30,{{10,100}}});
    engine.Tick();
    Require(port.submissions.size()==1,"configuration correction never retroactively retries blocked source position");
    port.now=4000; port.sources[100].remaining=0; port.sources[100].closes.push_back({2001,lot,4000});
    engine.Tick(); FillPending(port,engine); engine.Tick();
    Require(port.positions[{1001,9001}]==0 && engine.state().links.size()==1 && engine.state().links[0].phase==Phase::Review,
            "normal target still closes while unsubmitted conflict remains available for review");
    auto inconsistent=DecodeState(store.saved);
    inconsistent.links[0].follower_position=99;
    Throws([&]{(void)DecodeState(EncodeState(inconsistent));},"unsubmitted symbol exception cannot hide an actual target position");
}

void SymbolClosePriorityAndDurability() {
    auto config=Fanout(2); for (auto& rule:config.relationships) rule.auto_symbol=true;
    config.relationships[1].source_login=11;
    MemoryStore store; SymbolPort port(store); port.sources[100]=Source(); port.sources[101]=Source();
    port.targets[1000]={TestSymbol("XAUUSD.p")}; port.targets[1001]={TestSymbol("XAUUSDp")};
    Engine engine(config,"symbols",port,store,true); engine.Start({1000,10,{}});
    engine.Admit(Opening(),11); engine.Tick(); FillPending(port,engine); engine.Tick();
    engine.Admit(Opening(101,11,1001),12);
    port.sources[100].remaining=0; port.sources[100].closes.push_back({2001,lot,2000});
    engine.Tick();
    Require(port.submissions.size()==3 && port.submissions[1].close && !port.submissions[2].close && port.resolutions==2,
            "combined batch dispatches the old-position close before its new-symbol opening");
    FillPending(port,engine); engine.Tick();
    Require(port.submissions.size()==3 && !port.submissions.back().close && port.resolutions==2,
            "new source can proceed after the close has been dispatched");

    MemoryStore failed; SymbolPort failed_port(failed); failed_port.sources[100]=Source();
    failed_port.targets[1000]={TestSymbol("XAUUSD.p")};
    Engine failed_engine(config,"symbols",failed_port,failed,true); failed_engine.Start({1000,10,{}});
    failed_engine.Admit(Opening(),11); failed.fail_at=failed.saves+1;
    failed_engine.Tick();
    Require(failed_port.resolutions==1 && failed_port.submissions.empty() &&
            DecodeState(failed.saved).links.front().follower_symbol.empty(),
            "resolved suffix is never traded if its prepared journal write fails");

    MemoryStore missing; SymbolPort missing_port(missing); missing_port.sources[100]=Source();
    Engine missing_engine(config,"symbols",missing_port,missing,true); missing_engine.Start({1000,10,{}});
    missing_engine.Admit(Opening(),11); missing_engine.Tick();
    Require(missing_port.submissions.empty() && missing_engine.state().halt_reason.empty() &&
            UnsubmittedSymbolBlock(missing_engine.state(),0),"no matching symbol is a visible unsubmitted block without an order");
}

std::size_t PendingCount(const Engine& engine) {
    return static_cast<std::size_t>(std::count_if(engine.state().intents.begin(),engine.state().intents.end(),
        [](const Intent& i) { return i.state==IntentState::Submitted; }));
}
void PipelineFanout() {
    constexpr std::size_t followers=Engine::kMaxInFlight+128;
    Fixture f(Fanout(followers));
    const auto before=f.store.saves;
    f.engine.Admit(Opening(),11);
    Require(f.store.saves==before+1,"one source fan-out admits all mappings with a single durable snapshot");
    const auto dispatch_writes=f.store.saves;
    f.engine.Tick();
    const auto first_batch=f.port.submissions.size();
    Require(first_batch>0 && first_batch<=Engine::kMaxInFlight &&
        f.store.saves==dispatch_writes+2 && f.port.source_reads==1,
        "normal bounded batch shares two durable writes and one source-history read");
    for (unsigned n=0;n<Engine::kMaxInFlight && PendingCount(f.engine)<Engine::kMaxInFlight;++n) f.engine.Tick();
    Require(f.port.submissions.size()==Engine::kMaxInFlight && PendingCount(f.engine)==Engine::kMaxInFlight,
        "bounded independent accounts are submitted before any execution acknowledgement");
    const auto full=f.port.submissions.size(); f.engine.Tick();
    Require(f.port.submissions.size()==full,"full concurrency window applies backpressure");
    f.port.Fill(Engine::kMaxInFlight,lot); f.engine.Tick();
    Require(f.engine.state().intents[0].state==IntentState::Submitted &&
        f.engine.state().intents[Engine::kMaxInFlight-1].state==IntentState::Confirmed &&
        f.port.submissions.size()==full+1,"out-of-order confirmation frees a slot without waiting for the first account");
    for (unsigned round=0;round<10 && f.port.submissions.size()<followers;++round) {
        FillPending(f.port,f.engine,1); f.engine.Tick();
        Require(PendingCount(f.engine)<=Engine::kMaxInFlight,"in-flight limit holds across fan-out waves");
    }
    Require(f.port.submissions.size()==followers && f.engine.state().intents[0].state==IntentState::Submitted,
        "one slow follower does not prevent the other accounts from being submitted");
    FillPending(f.port,f.engine); f.engine.Tick();
    Require(PendingCount(f.engine)==0 && !f.engine.HasPending(),"all independent opening confirmations drain");
    for (const auto& link:f.engine.state().links) Require(link.phase==Phase::Active,"every follower has its own confirmed position");
    f.CloseSource(2001,lot); f.engine.Tick();
    for (unsigned n=0;n<Engine::kMaxInFlight && PendingCount(f.engine)<Engine::kMaxInFlight;++n) f.engine.Tick();
    Require(f.port.submissions.size()==followers+Engine::kMaxInFlight,"source close fans out without waiting for the first close");
    for (unsigned round=0;round<10 && (f.engine.HasPending() || f.port.submissions.size()<2*followers);++round) {
        FillPending(f.port,f.engine); f.engine.Tick();
        Require(PendingCount(f.engine)<=Engine::kMaxInFlight,"close wave also respects the concurrency limit");
    }
    Require(f.port.submissions.size()==2*followers && !f.engine.HasPending(),"exactly one open and close per follower");
    Require(f.engine.state().links.empty() && f.engine.state().intents.empty(),"all fan-out mappings and requests are cleaned after close");
    for (const auto& position:f.port.positions) Require(!position.second,"fan-out closes leave no target volume");
    const auto reads=f.port.source_reads, reconciles=f.port.reconcile_reads;
    f.engine.Tick(); f.engine.Tick();
    Require(f.port.source_reads==reads && f.port.reconcile_reads==reconciles,"ended mappings and old requests produce no repeated SDK queries");
    std::cout<<"pipeline simulation: "<<followers<<" followers, first batch "<<first_batch<<", admission writes 1, first dispatch writes 2, total writes "
        <<f.store.saves-before<<" for "<<followers<<" opens + "<<followers<<" closes (not a wall-clock benchmark)\n";
}
void PipelineAccountOrdering() {
    Fixture f; std::vector<std::pair<Deal,Id>> batch;
    for (unsigned n=0;n<12;++n) { f.port.sources[100+n]=Source(); batch.emplace_back(Opening(100+n,10,1000+n),11+n); }
    const auto writes=f.store.saves;
    f.engine.AdmitBatch(batch);
    Require(f.store.saves==writes+1,"many source admissions share one durable write");
    f.engine.Tick();
    Require(f.port.submissions.size()==Engine::kAccountInFlight,"same account pipelines independent positions with a bound");
    for (auto& e:f.port.sources) { e.second.remaining=0; e.second.closes.push_back({2000+e.first,lot,2100}); }
    f.engine.Tick();
    Require(f.port.submissions.size()==Engine::kAccountInFlight,"unconfirmed opening never overlaps its own close");
    // Release only the last opening: its close may proceed while the first seven wait.
    f.port.Fill(8,lot); f.engine.Tick();
    Require(f.port.submissions.size()==9 && f.port.submissions.back().close && f.port.submissions.back().follower_position==9008,
        "independent close does not wait behind another position on the same account");
    for (unsigned n=0;n<30;++n) { FillPending(f.port,f.engine); f.engine.Tick(); }
    Require(f.port.submissions.size()==16 && f.engine.state().links.empty(),
        "eight submitted openings close exactly once; four already-closed candidates are skipped");
    for (const auto& p:f.port.positions) Require(!p.second,"all actually opened followers finish flat");
    // Failed admission persistence cannot issue a trade.
    Fixture failed; failed.port.sources[101]=Source(); failed.store.fail_at=failed.store.saves+1;
    failed.engine.AdmitBatch({{Opening(),11},{Opening(101,10,1001),12}});
    failed.engine.Tick(); Require(failed.port.submissions.empty(),"failed admission cannot trade");
}
void WorkerBatchFairness() {
    Fixture f; std::vector<std::pair<Deal,Id>> events;
    for (unsigned n=0;n<24;++n) { f.port.sources[100+n]=Source(); events.emplace_back(Opening(100+n,10,1000+n),11+n); }
    const auto writes=f.store.saves;
    f.engine.AdmitBatch(events,false);
    Require(f.store.saves==writes && f.port.submissions.empty(),"worker admissions remain side-effect-free until Tick commits them");
    f.engine.Tick(); Require(f.store.saves==writes+2,"worker admission shares the prepared snapshot");
    for (unsigned n=0;n<4;++n) {
        // More callbacks arrive while slots are occupied. Older candidates must
        // not be bypassed by the newly appended positions after wrap/pruning.
        f.port.sources[200+n]=Source(); f.engine.AdmitBatch({{Opening(200+n,10,2000+n),100+n}},false);
        f.engine.Tick(); FillPending(f.port,f.engine); f.engine.Tick();
    }
    Require(f.port.submissions.size()==28,"all older candidates progress despite fresh callbacks");
    for (unsigned n=0;n<24;++n) Require(f.port.submissions[n].source_event==1000+n,"same-account candidate cursor cannot starve older work");
    FillPending(f.port,f.engine); f.engine.Tick();
    for (auto& e:f.port.sources) { e.second.remaining=0; e.second.closes.push_back({30000+e.first,lot,2100}); }
    for(unsigned n=0;n<10;++n) { f.engine.Tick(); FillPending(f.port,f.engine); }
    f.engine.Tick(); Require(f.engine.state().links.empty(),"cursor survives repeated settled-state removals");
    // Expired dedup keys and new volatile admissions can coexist; cleanup must
    // happen after the admission/intent transaction, never against an older snapshot.
    Fixture failed; failed.engine.AdmitBatch({{Opening(),11}},false); failed.store.fail_at=failed.store.saves+1;
    failed.engine.Tick();
    Require(failed.port.submissions.empty(),"combined batch cannot trade before durability");
    Fixture burst(Fanout(1000)); burst.port.sources[100].remaining=0; burst.port.sources[100].closes.push_back({2001,lot,2100});
    burst.port.sources[101]=burst.port.sources[100];
    burst.engine.AdmitBatch({{Opening(),11},{Opening(101,10,1001),12}}); const auto before=burst.store.saves;
    burst.engine.Tick();
    Require(burst.store.saves<=before+2 && burst.engine.state().links.size()>=2000-Engine::kPlanningVisits,
        "large expired-source burst has bounded work and grouped persistence");
    for(unsigned n=0;n<10;++n)burst.engine.Tick();
    Require(burst.engine.state().links.empty() && burst.port.submissions.empty(),"provably unsubmitted closed sources retire without leaked review rows");
}
void CloseSiblingPriority() {
    Fixture f(Fanout(3));
    for (unsigned n=0;n<4;++n) { f.port.sources[100+n]=Source(); f.engine.Admit(Opening(100+n,10,1000+n),11+n); }
    f.engine.Tick(); FillPending(f.port,f.engine); f.engine.ConfirmPending();
    Require(f.port.submissions.size()==12,"priority regression starts with four fully opened source groups");
    // End a polling slice just after follower A, leaving the cursor on B.
    f.port.slow_position=9001; f.engine.Tick();
    f.port.slow_position=9004;
    f.port.sources[100].remaining=0; f.port.sources[100].closes={{30000,lot,2100}};
    f.engine.Tick();
    Require(f.port.submissions.size()==15 && std::all_of(f.port.submissions.begin()+12,f.port.submissions.end(),
        [](const Intent& i){return i.close && i.source_event==30000;}),
        "all siblings of discovered close dispatch before an unrelated slow mapping ends the slice");
    f.port.slow_position=0; FillPending(f.port,f.engine); f.engine.Tick(); f.engine.Tick();
    for (unsigned n=1;n<4;++n) { f.port.sources[100+n].remaining=0; f.port.sources[100+n].closes={{30000+n,lot,2100}}; }
    for (unsigned n=0;n<10;++n) { f.engine.Tick(); FillPending(f.port,f.engine); }
    f.engine.Tick();
    Require(f.engine.state().links.empty() && f.port.submissions.size()==24,"priority indices survive compaction with exact-once closes");
}
void SourceCloseHints() {
    Fixture f(Fanout(3));
    for (unsigned n=0;n<4;++n) { f.port.sources[100+n]=Source(); f.engine.Admit(Opening(100+n,10,1000+n),11+n); }
    f.engine.Tick(); FillPending(f.port,f.engine); f.engine.ConfirmPending();
    f.engine.NotifySourceClose(10,103); f.engine.NotifySourceClose(10,103);
    f.engine.NotifySourceClose(99,103); f.engine.NotifySourceClose(10,999);
    f.engine.Tick();
    Require(f.port.submissions.size()==12,"untrusted, duplicate and unknown hints cannot manufacture a source close");
    f.engine.PauseOpenings(f.engine.state().revision);
    f.port.sources[103].remaining=0; f.port.sources[103].closes={{30003,lot,2100}};
    f.port.slow_position=9001;
    f.engine.NotifySourceClose(10,103); f.engine.NotifySourceClose(10,103); f.engine.Tick();
    Require(f.port.submissions.size()==15 && std::all_of(f.port.submissions.begin()+12,f.port.submissions.end(),
        [](const Intent& i){return i.close && i.source_event==30003;}),
        "source-close callback bypasses a slow unrelated scan while new openings are paused");
    f.engine.NotifySourceClose(10,103); f.engine.Tick();
    Require(f.port.submissions.size()==15,"duplicate callback never overlaps an in-flight close");
    f.port.slow_position=0; FillPending(f.port,f.engine); f.engine.Tick();
    f.engine.NotifySourceClose(10,103); f.engine.Tick();
    Require(f.port.submissions.size()==15 && f.engine.state().links.size()==9,
        "late callback after compaction cannot revive closed mappings");
}

void PipelineFaults() {
    Fixture unknown(Fanout(4)); unknown.port.submission_status[2]=Submission::Status::Unknown; unknown.Open();
    Require(unknown.port.submissions.size()==4 && unknown.engine.state().intents.size()==4 &&
        unknown.engine.state().intents[1].state==IntentState::Review && unknown.engine.state().intents[2].state==IntentState::Submitted && unknown.engine.state().halt_reason.empty(),
        "unknown middle submission isolates only itself and later requests still submit");
    unknown.port.Fill(1,lot); unknown.engine.Tick();
    Require(unknown.engine.state().intents[0].state==IntentState::Confirmed && unknown.port.submissions.size()==4,
        "dispatcher accounts for known queued results without retrying the unknown request");
    Fixture partial(Fanout(4)); partial.Open(); partial.port.Fill(2,lot/2); partial.port.Fill(4,lot);
    partial.engine.Tick();
    Require(partial.engine.state().halt_reason.empty() && !partial.engine.state().openings_paused && partial.engine.state().intents[3].state==IntentState::Confirmed &&
        partial.engine.state().intents[1].state==IntentState::Review,"partial fill does not hide later independent confirmations");
    partial.port.Fill(1,lot); partial.port.Fill(3,lot); partial.engine.Tick(); partial.engine.Tick();
    Require(partial.port.submissions.size()==4 && !partial.engine.HasPending(),"all other submitted requests drain after partial-fill isolation");
    Fixture rejected(Fanout(4)); rejected.port.submission_status[1]=Submission::Status::NotSubmitted; rejected.Open();
    Require(rejected.port.submissions.size()==4 && PendingCount(rejected.engine)==3 && rejected.engine.state().halt_reason.empty(),
        "known not-submitted outcome does not serialize unrelated accounts");
    Fixture thrown(Fanout(4)); thrown.port.throw_submit_at=2;
    thrown.Open();
    Require(thrown.port.submissions.size()==4 && thrown.engine.state().intents[1].state==IntentState::Review && thrown.engine.state().intents[3].state==IntentState::Submitted,
        "single submission exception does not discard independent prepared requests");
    thrown.port.Fill(1,lot); thrown.engine.Tick();
    Require(thrown.engine.state().intents[0].state==IntentState::Confirmed,"pre-exception queued request remains reconcilable");
    Fixture read_error(Fanout(4)); read_error.Open(); read_error.port.throw_reconcile_at=2; read_error.port.Fill(1,lot);
    read_error.engine.Tick();
    read_error.port.throw_reconcile_at=0; read_error.port.Fill(3,lot); read_error.port.Fill(4,lot); read_error.engine.Tick();
    Require(read_error.port.submissions.size()==4 && !read_error.engine.HasPending() &&
        read_error.engine.state().intents[0].state==IntentState::Confirmed && read_error.engine.state().intents[3].state==IntentState::Confirmed,
        "read error preserves confirmed prefix and later submitted evidence");
    Fixture planning(Fanout(4)); planning.port.throw_limits_login=1001;
    planning.Open();
    Require(planning.port.submissions.size()==3 && planning.engine.state().links[1].phase==Phase::Review,
        "one planning failure leaves healthy requests eligible for durable submission");
    for (unsigned fail_write:{1U,2U}) {
        Fixture disk(Fanout(4)); disk.engine.Admit(Opening(),11); disk.store.fail_at=disk.store.saves+fail_write;
        disk.engine.Tick();
        Require(disk.port.submissions.size()==(fail_write==1?0:4),"durable prepared batch precedes every possible side effect");
        disk.store.fail_at=0;
        Engine restart(Fanout(4),"test-server-6182",disk.port,disk.store,true); restart.Start({3000,30,{{10,100}}}); restart.Tick();
        Require(restart.state().halt_reason.empty() &&
            std::all_of(restart.state().links.begin(),restart.state().links.end(),[](const Link& l) { return l.phase==Phase::Review; }) &&
            disk.port.submissions.size()==(fail_write==1?0:4),"restart never retries any member of an uncertain batch");
    }
}
void PipelinePauseAndPersistence() {
    Fixture f(Fanout(Engine::kMaxInFlight+8)); f.Open(); f.engine.PauseOpenings(f.engine.state().revision);
    Require(f.port.submissions.size()==Engine::kMaxInFlight && f.engine.state().links.size()==Engine::kMaxInFlight && f.engine.state().closed_sources.size()==8,
        "pause skips unsent candidates while retaining submitted batch members");
    FillPending(f.port,f.engine); f.engine.Tick();
    Require(!f.engine.HasPending() && f.port.submissions.size()==Engine::kMaxInFlight,"pause still confirms the full in-flight batch");
    f.CloseSource(2001,lot); f.engine.Tick();
    Require(f.port.submissions.size()==2*Engine::kMaxInFlight,"all established follower closes remain managed during pause");
    FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.engine.ApplyBlockReason().empty(),"settled batch permits configuration application");
    const auto path=std::filesystem::temp_directory_path()/("tcx-pipeline-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".journal");
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove(p,e); } } cleanup{path};
    Fixture concurrent(Fanout(4)); concurrent.Open();
    auto prepared=concurrent.engine.state(); prepared.revision=1;
    for (auto& intent:prepared.intents) { intent.state=IntentState::Prepared; intent.request_id=0; }
    auto submitted=concurrent.engine.state(); submitted.revision=2;
    { FileJournal file(path); State empty; file.Load(empty); file.Save(prepared); file.Save(submitted); }
    { FileJournal file(path); State loaded;
      Require(file.Load(loaded) && EncodeState(loaded)==EncodeState(submitted),"real journal replays multi-request prepared and submitted snapshots"); }
    Engine restarted(Fanout(4),"test-server-6182",concurrent.port,concurrent.store,true);
    restarted.Start({3000,30,{{10,100}}}); restarted.Tick();
    Require(concurrent.port.submissions.size()==4 && std::all_of(restarted.state().intents.begin(),restarted.state().intents.end(),
        [](const Intent& i) { return i.state==IntentState::Review; }),"all unresolved requests are isolated for review after restart, with no resubmissions");
}
void JournalRotationTests() {
    const auto dir=std::filesystem::temp_directory_path()/("tcx-live-checkpoint-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path dir; ~Cleanup() { std::error_code e; std::filesystem::remove_all(dir,e); } } cleanup{dir};
    const auto read=[](const std::filesystem::path& p) {
        std::ifstream in(p,std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)),{});
    };
    const auto write=[](const std::filesystem::path& p,const std::string& data) {
        std::ofstream out(p,std::ios::binary|std::ios::trunc); out.write(data.data(),static_cast<std::streamsize>(data.size()));
    };
    Fixture f(Fanout(2)); f.Open();
    State state=f.engine.state(); state.revision=1;
    const auto path=dir/"active.journal";
    auto archive=path; archive+=".archive";
    {
        FileJournal file(path,1024); State empty; file.Load(empty); file.Save(state);
        Require(!file.OpeningBlockReason(state,FileJournal::kOpeningSnapshotBytes/FileJournal::kOpeningMappingReserveBytes).empty(),"fan-out byte reservation blocks openings before snapshot hard limit");
        auto large=state;
        large.links.resize(22,large.links.front());
        for (auto& link:large.links) link.processed_closes.resize(100000,2001);
        Require(EncodeState(large).size()>FileJournal::kOpeningSnapshotBytes && EncodeState(large).size()<FileJournal::kMaxSnapshotBytes &&
            !file.OpeningBlockReason(large,0).empty(),"large live snapshot is rejected for new openings while below hard capacity");
        Require(DecodeState(EncodeState(large)).links.size()==large.links.size(),
            "recovery accepts expanded snapshots above the old 16 MiB reader limit");
        ++state.revision; file.Save(state);
        Require(file.rotations()==1,"threshold replaces the active file with latest complete state");
        Throws([&] { FileJournal other(path); },"stable lease excludes another writer across replacement");
        Require(read(path).substr(0,8)=="TCXJ0002" && !std::filesystem::exists(archive),"checkpoint replacement creates no historical archive");
    }
    {
        FileJournal file(path); State loaded;
        Require(file.Load(loaded) && EncodeState(loaded)==EncodeState(state),"checkpoint preserves pending batch and frozen rules");
    }
    {
        FileJournal file(path,1024); MemoryStore mirror; FakePort port(mirror);
        Engine restarted(Fanout(2),"test-server-6182",port,file,true);
        restarted.Start({3000,20,{}}); restarted.Tick();
        Require(restarted.state().halt_reason.empty() && restarted.state().links[0].phase==Phase::Review && port.submissions.empty(),"checkpoint restart cannot resend unresolved requests");
        state=restarted.state();
    }
    const auto valid=read(path); auto temp=path; temp+=".checkpoint.tmp";
    {
        FileJournal file(path,1024); State loaded; file.Load(loaded);
        ++loaded.revision; loaded.links.front().opening_rule->parameter="0.4";
        Throws([&] { file.Save(loaded); },"checkpoint cannot discard frozen-rule transition validation");
        Require(read(path)==valid,"invalid checkpoint transition preserves original state");
    }
    write(temp,"uncommitted checkpoint");
    {
        FileJournal file(path,1024); State loaded;
        Require(file.Load(loaded) && EncodeState(loaded)==EncodeState(state),"temporary file is never used as live state");
        ++loaded.revision;
        Throws([&] { file.Save(loaded); },"leftover checkpoint requires explicit recovery before overwrite");
        Require(read(path)==valid,"failed publication preserves active state");
    }
    std::filesystem::remove(temp);
    write(path,valid.substr(0,valid.size()-1));
    Throws([&] { FileJournal file(path); State loaded; file.Load(loaded); },"torn live state refuses silent recovery");
    auto corrupt=valid; corrupt[40]^=1; write(path,corrupt);
    Throws([&] { FileJournal file(path); State loaded; file.Load(loaded); },"corrupt checkpoint is rejected");
    write(path,"");
    Throws([&] { FileJournal file(path); },"durable lease marker rejects a lost empty journal");
    std::filesystem::remove(path);
    Throws([&] { FileJournal file(path); },"durable lease marker rejects a missing journal without archives");
    write(path,valid);

    Fixture settled; settled.Open(); settled.Confirm(); settled.CloseSource(2001,lot); settled.engine.Tick(); settled.Confirm();
    auto before=DecodeState(settled.store.pruned_from); before.revision=1;
    auto after=settled.engine.state(); after.revision=2;
    const auto compact_path=dir/"compact.journal";
    {
        FileJournal file(compact_path); State empty; file.Load(empty); file.Save(before); file.Compact(after);
        Require(file.bytes()==EncodeState(after).size()+32,"compaction physically removes prior closed records from active file");
    }
    {
        FileJournal file(compact_path); State loaded;
        Require(file.Load(loaded) && EncodeState(loaded)==EncodeState(after),"compact state retains counter and deduplication keys");
    }
    // Reopening a published checkpoint must leave the file cursor at EOF.
    // Multiple append/replace cycles exercise both an empty retained state and
    // a subsequent durable update without creating or truncating the main file.
    const auto unicode_path=dir/std::filesystem::u8path(u8"checkpoint space \u8d26\u672c.journal");
    {
        FileJournal file(unicode_path); State empty; file.Load(empty); file.Save(before);
        for (unsigned n=0;n<3;++n) {
            file.Compact(after);
            const auto checkpoint=read(unicode_path);
            ++after.revision; file.Save(after);
            const auto appended=read(unicode_path);
            Require(appended.size()>checkpoint.size()+32 && appended.size()<=checkpoint.size()+EncodeState(after).size()+32 &&
                appended.substr(0,checkpoint.size())==checkpoint,
                "append after checkpoint preserves published bytes, including Unicode paths");
            Throws([&] { FileJournal other(unicode_path); },"lease remains exclusive after reopen and append");
            ++after.revision;
        }
        --after.revision;
        Require(file.rotations()==3,"repeated checkpoint replacement succeeds in one session");
    }
    {
        FileJournal file(unicode_path); State loaded;
        Require(file.Load(loaded) && EncodeState(loaded)==EncodeState(after),"replacement plus appended updates replay exactly");
    }
    after.revision=2;
#ifdef _WIN32
    const auto blocked_path=dir/"blocked.journal";
    {
        FileJournal file(blocked_path); State empty; file.Load(empty); file.Save(before);
        const auto original=read(blocked_path);
        struct Handle {
            HANDLE value;
            ~Handle() { if (value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
        } blocker{CreateFileW(blocked_path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        Require(blocker.value!=INVALID_HANDLE_VALUE,"external read handle opens while denying deletion");
        std::string error;
        try { file.Compact(after); } catch (const std::exception& e) { error=e.what(); }
        Require(error=="journal checkpoint publish failed (Windows 5)" ||
                error=="journal checkpoint publish failed (Windows 32)",
                "blocked Windows replacement reports publish stage and Win32 error code");
        Require(read(blocked_path)==original && file.rotations()==0 && file.bytes()==original.size(),
                "failed replacement preserves the main file and does not report a committed checkpoint");
        auto temporary=blocked_path; temporary+=".checkpoint.tmp";
        Require(read(temporary).size()==EncodeState(after).size()+32,"failed replacement retains checkpoint evidence");
        Throws([&] { FileJournal other(blocked_path); },"stable lease remains held even after publication failure closes data handles");
        Throws([&] { file.Save(after); },"failed replacement poisons further writes in the same session");
    }
    {
        FileJournal file(blocked_path); State loaded;
        Require(file.Load(loaded) && EncodeState(loaded)==EncodeState(before),
                "restart after blocked publication reads original main and never the uncommitted temporary");
        Throws([&] { file.Compact(after); },"restart will not overwrite leftover checkpoint evidence");
    }
#endif
    for (unsigned scenario=0;scenario<3;++scenario) {
        const auto bad=dir/("unsafe-"+std::to_string(scenario)+".journal");
        auto first=before, second=after;
        if (scenario==0) first.links[0].phase=Phase::Active;
        if (scenario==1) first.intents.back().state=IntentState::Submitted;
        if (scenario==2) second.next_intent_id=1;
        FileJournal file(bad); State empty; file.Load(empty); file.Save(first); const auto original=read(bad);
        Throws([&] { file.Compact(second); },"compaction cannot remove live/unresolved work or reset IDs");
        Require(read(bad)==original,"invalid cleanup leaves original file intact");
    }
    for (unsigned version:{1U,2U,3U,4U}) {
        const auto legacy=dir/("legacy-"+std::to_string(version)+".journal");
        auto old=f.engine.state(); old.revision=1;
        auto payload=version>=3?Schema4Payload(old):LegacyPayload(old,version);
        if (version==3) { payload.resize(payload.size()-32-old.closed_sources.size()*32-old.capacity_pause.size()); payload[0]=3; }
        std::uint32_t crc=0xFFFFFFFFU;
        for (unsigned char c:payload) { crc^=c; for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xEDB88320U:0); }
        std::string record="TCXJ0001";
        for (Id v:{static_cast<Id>(payload.size()),Id{1},static_cast<Id>(crc^0xFFFFFFFFU)})
            for (unsigned n=0;n<8;++n) record.push_back(static_cast<char>((v>>(8*n))&255));
        write(legacy,record+payload);
        {
            FileJournal file(legacy,1024); State loaded; Require(file.Load(loaded),"old schema loads before checkpoint");
            Require(loaded.next_intent_id==3,"old journal seeds monotonic counter from historical maximum");
            ++loaded.revision; file.Save(loaded); old=loaded;
        }
        FileJournal file(legacy); State loaded;
        Require(file.Load(loaded) && EncodeState(loaded)==EncodeState(old),"legacy migration to sparse-ID schema preserves outstanding requests");
    }
}

void IncrementalJournalRecovery() {
    const auto dir=std::filesystem::temp_directory_path()/("tcx-delta-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path p; ~Cleanup(){std::error_code e; std::filesystem::remove_all(p,e);} } cleanup{dir};
    const auto path=dir/"delta.journal";
    const auto read=[&] { std::ifstream in(path,std::ios::binary); return std::string(std::istreambuf_iterator<char>(in),{}); };
    const auto write=[&](const std::string& data) { std::ofstream out(path,std::ios::binary|std::ios::trunc); out.write(data.data(),data.size()); };
    Fixture f(Fanout(10)); f.Open(); f.Confirm();
    State state=f.engine.state(); state.revision=1;
    std::size_t base=0;
    {
        FileJournal journal(path); State empty; journal.Load(empty); journal.Save(state); base=journal.bytes();
        for (unsigned n=0;n<10;++n) { ++state.revision; journal.Save(state); }
        Require(journal.bytes()<base+2000 && !journal.rotations(),"small state changes append small deltas without physical checkpoints");
    }
    const auto valid=read(); Require(valid.substr(base,8)=="TCXJ0004","ordinary changes use independently checksummed delta frames");
    {
        FileJournal journal(path); State loaded; journal.Load(loaded);
        Require(EncodeState(loaded)==EncodeState(state),"multiple deltas reconstruct exact full state");
        ++loaded.revision; journal.Save(loaded); state=loaded;
    }
    auto appended=read();
    for (const auto& invalid:{appended.substr(0,appended.size()-1),[&] {auto c=appended; c.back()^=1; return c;}()}) {
        write(invalid);
        Throws([&]{FileJournal journal(path); State loaded; journal.Load(loaded);},"torn or corrupt delta never silently rolls back");
    }
    // Tamper with the base revision while updating the frame CRC. The delta's
    // own semantic checks must reject this, not merely its frame checksum.
    auto wrong=valid; const auto crc=[](const std::string& data) {
        std::uint32_t c=0xFFFFFFFFU; for(unsigned char b:data){c^=b; for(unsigned n=0;n<8;++n)c=(c>>1)^((c&1)?0xEDB88320U:0U);} return c^0xFFFFFFFFU;
    };
    const auto u64=[&](std::size_t offset,std::uint64_t value){for(unsigned n=0;n<8;++n) wrong[offset+n]=static_cast<char>((value>>(n*8))&255);};
    const auto length=[&] {std::uint64_t n=0;for(unsigned k=0;k<8;++k)n|=std::uint64_t(static_cast<unsigned char>(wrong[base+8+k]))<<(k*8);return n;}();
    u64(base+40,999); u64(base+24,crc(wrong.substr(base+32,length))); write(wrong);
    Throws([&]{FileJournal journal(path); State loaded; journal.Load(loaded);},"delta rejects wrong base even with valid outer CRC");
    write(appended);
    { FileJournal journal(path); State loaded; journal.Load(loaded); ++loaded.revision; journal.Compact(loaded); state=loaded;
      Require(journal.rotations()==1,"explicit checkpoint still performs protected file replacement"); }
    { FileJournal journal(path); State loaded; journal.Load(loaded); Require(EncodeState(state)==EncodeState(loaded),"checkpoint after deltas preserves all live mappings"); }
    const auto bounded=dir/"bounded-replay.journal";
    { FileJournal journal(bounded); State empty; journal.Load(empty); state.revision=1; journal.Save(state);
      for(std::size_t n=0;n<FileJournal::kReplayRecordLimit;++n) { ++state.revision; journal.Save(state); }
      Require(journal.rotations()==1 && journal.bytes()<10000,"small deltas also checkpoint after bounded replay work"); }
    { FileJournal journal(bounded); State loaded; journal.Load(loaded);
      Require(EncodeState(loaded)==EncodeState(state),"record-budget checkpoint restores exactly"); }
    Fixture closed; closed.Open(); closed.Confirm(); closed.CloseSource(2001,lot); closed.engine.Tick(); closed.Confirm();
    auto before=DecodeState(closed.store.pruned_from); before.revision=1;
    auto after=closed.engine.state(); after.revision=2;
    const auto prune_path=dir/"prune.journal";
    { FileJournal journal(prune_path); State empty; journal.Load(empty); journal.Save(before); journal.Prune(after);
      Require(journal.rotations()==0,"logical pruning does not force physical replacement"); }
    { FileJournal journal(prune_path); State loaded; journal.Load(loaded);
      Require(EncodeState(loaded)==EncodeState(after),"prune record replays retained counters and no closed orders"); }
    { FileJournal journal(prune_path); State loaded; journal.Load(loaded); ++loaded.revision;
      loaded.next_intent_id=1; Throws([&]{journal.Prune(loaded);},"incremental prune cannot reset intent identity"); }
}

void RotatingJournalLifecycles(unsigned rounds=3,std::uint64_t threshold=64*1024) {
    const auto dir=std::filesystem::temp_directory_path()/("tcx-close-rotation-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path dir; ~Cleanup() { std::error_code e; std::filesystem::remove_all(dir,e); } } cleanup{dir};
    struct RealStore:MemoryStore {
        FileJournal file;
        std::uint64_t cumulative_bytes=0;
        RealStore(const std::filesystem::path& path,std::uint64_t threshold):file(path,threshold) {}
        bool Load(State& state) override { return file.Load(state); }
        void Save(const State& state) override {
            file.Save(state); MemoryStore::Save(state); cumulative_bytes+=32+saved.size();
        }
        void Compact(const State& state) override {
            file.Compact(state); MemoryStore::Save(state); cumulative_bytes+=32+saved.size();
        }
    };
    const auto path=dir/"active.journal";
    const CopierConfig config{true,{{1,50001,50002,true,SizingMode::FixedLots,"0.01"},
                                   {2,50001,50003,true,SizingMode::FixedLots,"0.05"}}};
    State last;
    {
        RealStore store(path,threshold); FakePort port(store); Engine engine(config,"rotation",port,store);
        engine.Start({1000,10,{}});
        Id position=100,sequence=11;
        for (unsigned round=0;round<rounds;++round) {
            const auto first=position;
            for (unsigned n=0;n<9;++n,++position) {
                port.now+=1000;
                auto deal=Opening(position,50001,1000+position,lot/10); deal.time_ms=port.now;
                auto source=Source(lot/10); source.first_fill_ms=port.now; port.sources[position]=source;
                engine.Admit(deal,sequence++); engine.Tick(); FillPending(port,engine); engine.Tick();
            }
            for (Id p=first;p<position;++p) {
                auto& source=port.sources[p]; source.remaining=0;
                source.closes.push_back({20000+p,lot/10,++port.now});
            }
            engine.Tick();
            for (unsigned n=0;n<12 && engine.HasPending();++n) { FillPending(port,engine); engine.Tick(); }
            Require(engine.state().halt_reason.empty(),"file rotation does not halt bulk closing");
            Require(engine.state().links.empty() && engine.state().intents.empty(),"completed source batch leaves no closed positions or requests");
            for (const auto& p:port.positions) Require(p.second==0,"every actual target position was fully closed");
        }
        Require(store.file.rotations()>1,"real lifecycle physically checkpoints settled orders repeatedly");
        Require(engine.state().next_intent_id==1+rounds*36,"intent IDs never reuse pruned request numbers");
        port.now+=31000; engine.Tick();
        Require(engine.state().closed_sources.empty(),"aged deduplication keys are pruned after their source admission floor is persisted");
        Require(store.file.bytes()<4096,"flat long-running copier has a small bounded live checkpoint");
        std::cout<<"live journal simulation: rounds="<<rounds<<" lifetime_intents="<<engine.state().next_intent_id-1
                 <<" cumulative_bytes="<<store.cumulative_bytes<<" rotations="<<store.file.rotations()
                 <<" active_bytes="<<store.file.bytes()<<"; all targets closed\n";
        last=engine.state();
    }
    FileJournal file(path); State loaded;
    Require(file.Load(loaded) && EncodeState(last)==EncodeState(loaded),"final rotated journal retains complete execution history");
}

void LiveRetentionAndCapacity() {
    Fixture manual; manual.Open(); manual.Confirm();
    manual.port.positions[{20,9001}]=0; manual.engine.Tick();
    Require(manual.engine.state().links.empty() && manual.engine.state().intents.empty() &&
        manual.engine.state().closed_sources.size()==1,"manual full close keeps only a short deduplication key");
    auto duplicate=Opening(); duplicate.id=1001; duplicate.time_ms=2100; manual.port.now=2100;
    manual.engine.Admit(duplicate,12); manual.engine.Tick();
    Require(manual.port.submissions.size()==1,"different fill callback cannot reopen a recently pruned source lifecycle");
    manual.port.now=33000; manual.engine.Tick();
    Require(manual.engine.state().closed_sources.empty() && manual.engine.state().source_floor_ms==3000,
        "dedup key expiry persists an admission floor before forgetting old source position");
    duplicate.time_ms=33000; manual.engine.Admit(duplicate,13); manual.engine.Tick();
    Require(manual.port.submissions.size()==1 && manual.engine.state().links.empty(),"late callback cannot reopen an aged source first fill");
    // A clock rollback cannot make an expired lifecycle new again.
    manual.port.now=2200; duplicate.time_ms=2200; manual.engine.Admit(duplicate,14); manual.engine.Tick();
    Require(manual.port.submissions.size()==1,"persisted floor survives backward server clock movement");
    Engine restart(Config(),"test-server-6182",manual.port,manual.store);
    restart.Start({34000,20,{}}); manual.port.now=35000;
    auto fresh=Opening(101,10,1002); fresh.time_ms=35000;
    auto source=Source(); source.first_fill_ms=35000; manual.port.sources[101]=source;
    restart.Admit(fresh,21); restart.Tick();
    Require(manual.port.submissions.back().id==2,"flat restart never reuses a cleaned request ID");

    Fixture partial; partial.Open(); partial.Confirm();
    for (unsigned n=0;n<5;++n) { partial.CloseSource(2000+n,lot/10); partial.engine.Tick(); partial.Confirm(); }
    Require(partial.engine.state().intents.size()==2 && partial.engine.state().intents.front().id==1 &&
        partial.engine.state().intents.back().id==6 && partial.engine.state().links[0].processed_closes.size()==5,
        "live position keeps opening evidence and latest close without an unbounded completed-request list");
    Require(partial.engine.InspectIntent(6).find("完整成交")!=std::string::npos,"sparse request IDs remain inspectable");
    Throws([&] { partial.engine.InspectIntent(2); },"cleaned request ID cannot resolve to a different row");
    partial.CloseSource(2006,lot/2); partial.engine.Tick(); partial.Confirm();
    Require(partial.engine.state().links.empty() && partial.port.positions[{20,9001}]==0,"partial-close evidence survives until final close then clears");

    Fixture capacity; capacity.Open(); capacity.Confirm(); capacity.store.capacity_reason="simulated low disk reserve";
    capacity.engine.Admit(Opening(101,10,1001),12);
    Require(!capacity.engine.state().openings_paused && capacity.engine.state().halt_reason.empty() &&
        capacity.engine.state().capacity_pause==capacity.store.capacity_reason,"capacity skip is visible and does not set a global pause");
    capacity.CloseSource(2001,lot); capacity.engine.Tick(); capacity.Confirm();
    Require(capacity.port.submissions.size()==2 && capacity.port.submissions.back().close &&
        capacity.engine.state().links.empty() && capacity.engine.state().halt_reason.empty(),"low capacity still permits full existing-position closure");
    Throws([&] { capacity.engine.ResumeOpenings(capacity.engine.state().revision,{3000,20,{}}); },"manual resume cannot bypass capacity guard");
    capacity.store.capacity_reason.clear(); capacity.engine.PauseOpenings(capacity.engine.state().revision); capacity.engine.ResumeOpenings(capacity.engine.state().revision,{3000,20,{}});
    Require(capacity.engine.state().capacity_pause.empty() && !capacity.engine.state().openings_paused,"capacity recovery allows explicit resume");

    Fixture queued; queued.Open(); queued.Confirm(); queued.port.sources[101]=Source();
    queued.engine.Admit(Opening(101,10,1001),12); queued.store.capacity_reason="reserve dropped after admission";
    queued.CloseSource(2001,lot); queued.engine.Tick(); queued.Confirm();
    Require(!queued.engine.state().openings_paused && queued.port.submissions.size()==2 && queued.port.submissions.back().close,
        "capacity change cancels queued openings and preserves already-managed closes");

    Fixture many(Fanout(1000));
    for (unsigned n=0;n<=Engine::kOpeningLinkLimit/1000;++n) many.engine.Admit(Opening(100+n,10,1000+n),11+n);
    Require(!many.engine.state().openings_paused && many.engine.state().halt_reason.empty() && many.port.submissions.empty(),
        "exceeding the mapping limit skips that admission without pausing existing work");
    Require(many.engine.state().links.size()==Engine::kOpeningLinkLimit,"capacity skip does not cancel previously accepted mappings");

    Fixture failed; failed.Open(); failed.Confirm(); failed.CloseSource(2001,lot); failed.engine.Tick();
    // Fill confirmation is the next Save; compaction follows it and must not be
    // mistaken for durable progress if its write fails; closing still finishes in memory.
    failed.store.fail_at=failed.store.saves+2;
    failed.Confirm();
    Require(failed.engine.state().links.empty() && failed.engine.storage_degraded() && failed.engine.state().halt_reason.empty() && failed.port.submissions.size()==2,
        "cleanup failure leaves old durable evidence and cannot resend a confirmed close");
}

void BatchedExpiryPruning() {
    // Closed keys arrive every 50 ms (20 source lifecycles/s), then expire
    // while no trade work remains. Each poll invokes pruning twice.
    Fixture empty;
    auto seed=empty.engine.state();
    for (unsigned n=0;n<200;++n) seed.closed_sources.push_back({1,10,1000+n,2001+50*n});
    MemoryStore store; store.saved=EncodeState(seed); FakePort port(store); port.now=31000;
    Engine engine(Config(),seed.identity,port,store); engine.Start({1000,10,{}});
    const auto before=store.saves;
    for (std::int64_t now=32001;now<=42001;now+=50) {
        port.now=now; engine.Tick();
        const auto cutoff=(now-30000)/1000*1000;
        const auto remaining=std::count_if(seed.closed_sources.begin(),seed.closed_sources.end(),
            [&](const ClosedSource& c) { return c.opening_ms>cutoff; });
        Require(engine.state().closed_sources.size()==static_cast<std::size_t>(remaining),
            "expiry batches retain keys conservatively for less than one extra second");
        Require(DecodeState(store.saved).source_floor_ms==engine.state().source_floor_ms,
            "every forgotten deduplication key is protected by a durable admission floor");
    }
    Require(store.saves-before==10 && engine.performance().prune_commits==10 &&
        engine.performance().prune_expiry_only_commits==10 &&
        engine.performance().prune_closed_sources_removed==200 &&
        engine.performance().prune_expiry_deferred_passes>200,
        "20 expiries/s become one successful durable cleanup/s without counting deferred calls as commits");
    Require(engine.state().closed_sources.empty() && port.submissions.empty(),
        "expiry-only cleanup never creates or submits trades");

    for (const bool persisted_floor:{false,true}) {
        auto bounded=empty.engine.state(); bounded.closed_sources={{1,10,100,2050},{1,10,101,2051}};
        bounded.source_floor_ms=persisted_floor?2050:0;
        if (persisted_floor) bounded.closed_sources.erase(bounded.closed_sources.begin());
        MemoryStore barrier_store; barrier_store.saved=EncodeState(bounded); FakePort barrier_port(barrier_store);
        barrier_port.now=32051;
        Engine barrier(Config(),bounded.identity,barrier_port,barrier_store);
        barrier.Start({persisted_floor?1000:2050,10,{}});
        Require(barrier.state().source_floor_ms==2050 && barrier.state().closed_sources.size()==1 &&
            barrier.state().closed_sources[0].position==101,
            "batching never rounds a persisted floor or activation barrier backward");
        barrier_port.now=33001; barrier.Tick();
        Require(barrier.state().source_floor_ms==3000 && barrier.state().closed_sources.empty(),
            "the next batch advances the floor and removes the retained boundary key");
        const auto saved=barrier_store.saved; const auto saves=barrier_store.saves;
        barrier_port.now=32000; barrier.Tick();
        Require(barrier.state().source_floor_ms==3000 && barrier_store.saved==saved && barrier_store.saves==saves,
            "clock rollback cannot decrease the durable floor or trigger redundant cleanup writes");
    }

    // Before the batch boundary, the tombstone rejects a new-time callback for
    // the same position. After cleanup/restart, verified first-fill history and
    // the persisted/startup floor provide the same no-reopen guarantee.
    for (const bool cleanup_before_restart:{false,true}) {
        auto original=empty.engine.state(); original.closed_sources={{1,10,100,2001}};
        original.next_intent_id=41;
        MemoryStore restart_store; restart_store.saved=EncodeState(original); FakePort first_port(restart_store);
        first_port.now=32001; first_port.sources[100]=Source(); first_port.sources[100].first_fill_ms=2001;
        Engine first(Config(),original.identity,first_port,restart_store); first.Start({1000,10,{}});
        auto duplicate=Opening(); duplicate.time_ms=32001;
        first.Admit(duplicate,11); first.Tick();
        Require(first.state().closed_sources.size()==1 && first_port.submissions.empty(),
            "retained batch-window tombstone prevents a late fill callback from reopening");
        if (cleanup_before_restart) { first_port.now=33000; first.Tick(); }
        Require(first.state().closed_sources.empty()==cleanup_before_restart,
            "restart test covers both retained and already-expired durable tombstones");
        FakePort restored_port(restart_store); restored_port.sources=first_port.sources; restored_port.now=34000;
        Engine restored(Config(),original.identity,restored_port,restart_store); restored.Start({34000,20,{}});
        restored_port.now=34001; duplicate.time_ms=34001;
        restored.Admit(duplicate,21); restored.Tick();
        Require(restored_port.submissions.empty() && restored.state().links.empty() &&
            restored.state().next_intent_id==41,
            "restart before or after expiry never resubmits an old source lifecycle");
        auto fresh=Opening(101,10,1001); fresh.time_ms=34002;
        restored_port.now=34002; restored_port.sources[101]=Source(); restored_port.sources[101].first_fill_ms=34002;
        restored.Admit(fresh,22); restored.Tick();
        Require(restored_port.submissions.size()==1 && restored_port.submissions[0].id==41,
            "batch retention preserves normal fresh-source admission and monotonic intent IDs");
    }

    // A failed expiry write is not a successful prune metric, and replay keeps
    // its last durable keys/floor until a successful restart barrier is saved.
    auto failed_seed=empty.engine.state(); failed_seed.closed_sources={{1,10,100,2001}};
    MemoryStore failed_store; failed_store.saved=EncodeState(failed_seed); FakePort failed_port(failed_store);
    failed_port.now=31000; Engine failed(Config(),failed_seed.identity,failed_port,failed_store);
    failed.Start({1000,10,{}}); const auto durable=failed_store.saved;
    failed_store.fail_at=failed_store.saves+1; failed_port.now=33000; failed.Tick();
    Require(failed.storage_degraded() && failed_store.saved==durable &&
        failed.performance().prune_commits==0 && failed.performance().prune_expiry_only_commits==0 &&
        failed.performance().prune_closed_sources_removed==0,
        "failed/in-memory pruning never reports a durable commit or discarded durable key");
    failed_store.fail_at=0; FakePort recovered_port(failed_store); recovered_port.now=34000;
    recovered_port.sources[100]=Source(); recovered_port.sources[100].first_fill_ms=2001;
    Engine recovered(Config(),failed_seed.identity,recovered_port,failed_store); recovered.Start({34000,20,{}});
    auto old=Opening(); old.time_ms=34001; recovered_port.now=34001;
    recovered.Admit(old,21); recovered.Tick();
    Require(recovered_port.submissions.empty(),"crash after a failed expiry flush cannot reopen an old source");

    // A full dedup-key budget must not postpone terminal-link retirement until
    // the expiry-only batch boundary. Reclaim expired keys in that same commit.
    Fixture tiny(Config(SizingMode::FixedLots,"0.001")); tiny.Open();
    auto capacity=DecodeState(tiny.store.pruned_from); capacity.links[0].source.time_ms=3000;
    capacity.closed_sources.clear(); capacity.closed_sources.reserve(100000);
    for (unsigned n=0;n<100000;++n) capacity.closed_sources.push_back({1,10,10000+n,2050});
    MemoryStore full_store; full_store.saved=EncodeState(capacity); FakePort full_port(full_store); full_port.now=32050;
    Engine full(Config(SizingMode::FixedLots,"0.001"),capacity.identity,full_port,full_store);
    full.Start({1000,10,{}});
    Require(full.state().links.empty() && full.state().closed_sources.size()==1 &&
        full.state().closed_sources[0].position==100 && full.state().source_floor_ms==2050 &&
        full.performance().prune_commits==1 && full.performance().prune_expiry_only_commits==0 &&
        full.performance().prune_closed_sources_removed==100000,
        "terminal retirement promptly frees the full key budget at the exact cutoff");
}

void CapacityTenThousand() {
    const auto started=std::chrono::steady_clock::now();
    const auto dir=std::filesystem::temp_directory_path()/("tcx-capacity-10000-"+
        std::to_string(started.time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path dir; ~Cleanup() { std::error_code e; std::filesystem::remove_all(dir,e); } } cleanup{dir};
    const auto path=dir/"capacity.journal";
    const auto config=Fanout(1000);
    Fixture sample; sample.Open(); sample.Confirm();
    State seed=sample.engine.state(); seed.config_json=WriteConfigJson(config);
    seed.links.clear(); seed.intents.clear(); seed.revision=1;
    // Restore 9,000 confirmed positions, then really admit and submit the last
    // 1,000 through Engine + FileJournal. No MT5 server is used by this test.
    for (std::size_t source=0;source<9;++source) for (const auto& rule:config.relationships) {
        const auto n=seed.links.size();
        auto link=sample.engine.state().links.front();
        link.rule=rule.id; link.opening_rule=rule; link.source=Opening(100+source,10,1000+source);
        link.follower_position=900000+n;
        auto intent=sample.engine.state().intents.front();
        intent.id=n+1; intent.link=n; intent.follower_login=rule.follower_login;
        intent.source_event=link.source.id; intent.result_position=link.follower_position;
        intent.request_id=static_cast<std::uint32_t>(1000+intent.id); intent.deals={70000+intent.id};
        seed.links.push_back(std::move(link)); seed.intents.push_back(std::move(intent));
    }
    seed.next_intent_id=seed.intents.back().id+1;
    { FileJournal file(path); State empty; file.Load(empty); file.Save(seed); }
    struct ScaleStore:MemoryStore {
        FileJournal file;
        std::uint64_t peak_bytes=0;
        explicit ScaleStore(const std::filesystem::path& path):file(path) {}
        bool Load(State& s) override { if (!file.Load(s)) return false; MemoryStore::Save(s); return true; }
        void Save(const State& s) override { file.Save(s); MemoryStore::Save(s); peak_bytes=std::max(peak_bytes,file.bytes()); }
        void Compact(const State& s) override { file.Compact(s); MemoryStore::Save(s); peak_bytes=std::max(peak_bytes,file.bytes()); }
        void Prune(const State& s) override { file.Prune(s); MemoryStore::Save(s); peak_bytes=std::max(peak_bytes,file.bytes()); }
        std::string OpeningBlockReason(const State& s,std::size_t n) override { return file.OpeningBlockReason(s,n); }
    };
    struct ScalePort:FakePort {
        unsigned checked_save=0;
        std::map<Id,Intent> prepared;
        explicit ScalePort(MemoryStore& store):FakePort(store) {}
        Submission Submit(const Intent& i) override {
            // Decode each durable batch once, not 32 full decodes per batch.
            if (checked_save!=store.saves) {
                prepared.clear();
                for (const auto& request:DecodeState(store.saved).intents)
                    if (request.state==IntentState::Prepared) prepared.emplace(request.id,request);
                checked_save=store.saves;
            }
            const auto found=prepared.find(i.id);
            Require(found!=prepared.end() && found->second.units==i.units && found->second.link==i.link &&
                found->second.follower_login==i.follower_login && found->second.follower_position==i.follower_position,
                "large batch is durable before each submission");
            submissions.push_back(i);
            return {Submission::Status::Queued,static_cast<std::uint32_t>(1000+i.id),0};
        }
    };
    State final;
    {
        ScaleStore store(path); ScalePort port(store); port.now=2500;
        for (Id n=0;n<9;++n) port.sources[100+n]=Source();
        for (const auto& link:seed.links) port.positions[{link.opening_rule->follower_login,link.follower_position}]=lot;
        Engine engine(config,"test-server-6182",port,store,true);
        engine.Start({2500,20,{}});
        Require(engine.state().links.size()==9000 && port.source_reads==9 && engine.state().halt_reason.empty(),
            "large restart retains frozen rules and shares source reads across follower mappings");
        port.now=3000; port.sources[109]=Source(); port.sources[109].first_fill_ms=3000;
        auto last=Opening(109,10,1009); last.time_ms=3000;
        engine.Admit(last,21);
        Require(engine.state().links.size()==10000 && !engine.state().openings_paused,
            "real file capacity permits the full final fan-out to exactly 10000 mappings");
        const auto drain=[&](std::size_t expected,const char* message) {
            for (unsigned round=0;round<500 && (port.submissions.size()<expected || engine.HasPending());++round) {
                FillPending(port,engine); engine.Tick();
                Require(engine.state().halt_reason.empty(),"large lifecycle must not halt");
                Require(PendingCount(engine)<=Engine::kMaxInFlight,"large lifecycle keeps per-batch bound");
                Require(store.file.bytes()<=FileJournal::kRotateBytes,"main journal stays below checkpoint threshold");
            }
            Require(port.submissions.size()==expected && !engine.HasPending(),message);
        };
        drain(1000,"last 1000 openings complete without early byte-capacity pause");
        auto view=management::DecodeResponse(management::EncodeResponse(management::Snapshot(engine)));
        Require(view.active==10000 && view.links_total==10000 && view.links.size()==management::kVisibleRows,
            "management reports 10000 exact totals with a bounded visible table");
        std::cout<<"capacity simulation: active=10000 snapshot_bytes="<<EncodeState(engine.state()).size()<<std::endl;
        // Overflow protects admission only. All 10,000 established positions
        // and their original targets remain manageable while admission is full.
        auto overflow=Opening(110,10,1010); overflow.time_ms=3100; port.now=3100;
        engine.Admit(overflow,22);
        Require(!engine.state().openings_paused && engine.state().links.size()==10000 &&
            engine.state().halt_reason.empty(),"10001+ admission is skipped without dropping managed positions or pausing");
        port.now=3200; port.sources[100].remaining=lot/2;
        port.sources[100].closes.push_back({50000,lot/2,3200});
        drain(2000,"partial source close reaches all 1000 mapped targets at capacity");
        Require(engine.state().links.size()==10000 && engine.state().intents.size()==11000,
            "partial closes retain all 10000 mappings and their evidence");
        for (const auto& link:engine.state().links) if (link.source.position==100)
            Require(link.follower_remaining==lot/2,"large partial closes preserve exact target remainder");
        // Every latest full snapshot is reloadable before proceeding to closure.
        Require(DecodeState(store.saved).links.size()==10000,"large partial-close state decodes completely");
        const auto restart_path=dir/"restart-10000.journal";
        std::filesystem::copy_file(path,restart_path);
        {
            FileJournal restored_file(restart_path); MemoryStore mirror; ScalePort restored_port(mirror);
            restored_port.sources=port.sources; restored_port.positions=port.positions; restored_port.now=3250;
            Engine restarted(config,"test-server-6182",restored_port,restored_file,true);
            restarted.Start({3250,30,{}}); restarted.Tick();
            Require(restarted.state().links.size()==10000 && !restarted.state().openings_paused &&
                restarted.state().halt_reason.empty() && restarted.state().next_intent_id==11001 &&
                std::all_of(restarted.state().links.begin(),restarted.state().links.end(),
                    [](const Link& link) { return link.phase==Phase::Active; }) && restored_port.submissions.empty(),
                "10000 partially closed mappings recover from actual file without replaying trades");
        }
        std::cout<<"capacity simulation: partial closes confirmed; draining all 10000 targets"<<std::endl;
        port.now=3300;
        for (auto& entry:port.sources) {
            entry.second.closes.push_back({60000+entry.first,entry.second.remaining,3300});
            entry.second.remaining=0;
        }
        drain(12000,"10000 final closes finish despite capacity admission refusal");
        Require(engine.state().links.empty() && engine.state().intents.empty(),"all settled large-run records are cleaned");
        for (const auto& position:port.positions) Require(position.second==0,"all 10000 actual target positions are flat");
        port.now=34000; engine.Tick(); final=engine.state();
        Require(final.closed_sources.empty() && final.next_intent_id==21001,"large run retains monotonic IDs without closed history");
        std::cout<<"capacity simulation: new_requests="<<port.submissions.size()<<" peak_main_bytes="<<store.peak_bytes
            <<" final_bytes="<<store.file.bytes()<<" checkpoints="<<store.file.rotations()<<std::endl;
    }
    FileJournal file(path); State restored;
    Require(file.Load(restored) && EncodeState(restored)==EncodeState(final),"large final checkpoint reloads exactly");
    std::cout<<"capacity simulation passed: 10000 positions, "<<checks<<" checks, elapsed_ms="
        <<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
}

RequestRefusal NoMoney(const Intent& i) {
    RequestRefusal event;
    event.intent=i.id; event.login=i.follower_login; event.position=i.follower_position;
    event.request=i.request_id; event.action=3; event.type=i.side==Side::Buy?0:1;
    event.flags=i.close?3:2; event.retcode=kRequestNoMoney; event.requested=i.units;
    const wchar_t symbol[]=L"XAUUSDp";
    std::copy(std::begin(symbol),std::end(symbol),event.symbol.begin());
    return event;
}
void RefusalEvidenceTests() {
    Intent intent; intent.id=1; intent.request_id=123; intent.follower_login=50003;
    intent.units=2*lot/100; intent.symbol="XAUUSDp";
    const auto valid=NoMoney(intent);
    for (unsigned bad=0;bad<12;++bad) {
        RequestRefusals inbox; inbox.Register(intent.id); auto event=valid;
        switch (bad) {
        case 0: ++event.login; break;
        case 1: ++event.request; break;
        case 2: ++event.position; break;
        case 3: ++event.requested; break;
        case 4: ++event.result_deal; break;
        case 5: ++event.result_order; break;
        case 6: ++event.result_volume; break;
        case 7: ++event.action; break;
        case 8: ++event.type; break;
        case 9: event.flags=3; break;
        case 10: event.symbol[6]=L'r'; break;
        case 11: event.retcode=10012; break; // Timeout is not proof of zero execution.
        }
        Require(inbox.Push(event),"refusal evidence queued");
        const auto result=inbox.Read(intent,L"XAUUSDp");
        Require(result && result->status==Result::Status::Uncertain,"mismatched or ambiguous evidence never proves no execution");
    }
    RequestRefusals inbox; inbox.Register(1);
    Require(inbox.Push(valid) && inbox.Push(valid),"duplicate callback accepted idempotently");
    auto result=inbox.Read(intent,L"XAUUSDp");
    Require(result && result->status==Result::Status::Rejected && result->retcode==10019 && !result->units &&
        !result->position && result->deals.empty(),"matching pretrade no-money refusal proves zero fill without order/deal history");
    auto conflict=valid; ++conflict.result_deal; inbox.Push(conflict);
    Require(inbox.Read(intent,L"XAUUSDp")->status==Result::Status::Uncertain,"conflicting duplicate cannot overwrite previous evidence");
    inbox.Forget(1); inbox.Push(valid);
    Require(!inbox.Read(intent,L"XAUUSDp") && !inbox.size(),"late callback after settlement cannot regrow evidence cache");
    // Sustained refusal load cannot retain every historical request.
    for (Id id=2;id<2002;++id) {
        intent.id=id; inbox.Register(id); inbox.Push(NoMoney(intent));
        Require(inbox.Read(intent,L"XAUUSDp")->status==Result::Status::Rejected,"each refusal correlates with its own registered intent");
        inbox.Forget(id);
    }
    Require(!inbox.size(),"callback evidence is bounded by live requests, not cumulative trades");
    for (Id id=1;id<=Engine::kMaxInFlight;++id) inbox.Register(id);
    Throws([&]{inbox.Register(10000);},"cannot register beyond global in-flight bound");
    RequestRefusals full;
    for(unsigned n=0;n<4096;++n) Require(full.Push(valid),"bounded callback ring accepts its capacity");
    Require(!full.Push(valid),"overflow is surfaced as a callback-loss diagnostic");
}

void NoMoneyClosesRemainManaged(unsigned source_count=9) {
    const auto started=std::chrono::steady_clock::now();
    const auto dir=std::filesystem::temp_directory_path()/("tcx-refusal-"+std::to_string(started.time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path dir; ~Cleanup(){std::error_code e;std::filesystem::remove_all(dir,e);} } cleanup{dir};
    struct Disk:MemoryStore {
        std::unique_ptr<FileJournal> file;
        explicit Disk(const std::filesystem::path& p):file(std::make_unique<FileJournal>(p)) {}
        bool Load(State& s) override { if (!file->Load(s)) return false; saved=EncodeState(s); return true; }
        void Save(const State& s) override { file->Save(s); MemoryStore::Save(s); }
        void Prune(const State& s) override { file->Prune(s); MemoryStore::Save(s); }
    };
    struct RefusalPort:FakePort {
        RequestRefusals inbox; Id reject_account=0; bool released_before_durable=false;
        explicit RefusalPort(MemoryStore& s):FakePort(s) {}
        Submission Submit(const Intent& i) override {
            inbox.Register(i.id);
            const auto submitted=FakePort::Submit(i);
            if (!i.close && i.follower_login==reject_account) {
                auto snapshot=i; snapshot.request_id=submitted.request_id;
                Require(inbox.Push(NoMoney(snapshot)),"refusal may arrive synchronously before Submit returns");
            }
            return submitted;
        }
        Result Reconcile(const Intent& i) override {
            if (auto r=inbox.Read(i,L"XAUUSDp")) return *r;
            return FakePort::Reconcile(i);
        }
        void ForgetRequest(Id id) noexcept override {
            const auto durable=DecodeState(store.saved);
            const auto found=std::find_if(durable.intents.begin(),durable.intents.end(),[&](const Intent& i){return i.id==id;});
            if (found==durable.intents.end() || found->state==IntentState::Prepared || found->state==IntentState::Submitted)
                released_before_durable=true;
            inbox.Forget(id);
        }
    };
    Disk store(dir/"refusals.journal"); RefusalPort port(store); Engine engine(Fanout(10),"refusals",port,store);
    engine.Start({1000,10,{}});
    const unsigned prior=source_count-8;
    for (unsigned n=0;n<prior;++n) {
        port.sources[100+n]=Source(); engine.Admit(Opening(100+n,10,1000+n),11+n);
        engine.Tick(); FillPending(port,engine); engine.Tick();
    }
    Require(port.submissions.size()==prior*10 && !port.inbox.size(),"all prior positions are confirmed and callback slots reclaimed");
    port.reject_account=1001;
    std::vector<std::pair<Deal,Id>> events;
    for (unsigned n=prior;n<source_count;++n) { port.sources[100+n]=Source(); events.push_back({Opening(100+n,10,1000+n),11+n}); }
    engine.AdmitBatch(events); engine.Tick();
    // First consume refusals before fake-filling only the nine healthy accounts.
    engine.ConfirmPending();
    unsigned rejected=0;
    for (const auto& i:engine.state().intents) if (i.state==IntentState::Rejected) {
        ++rejected; Require(i.retcode==10019 && !i.filled && !i.result_position,"no-money is immediately a zero-fill rejection");
    }
    Require(rejected==8 && engine.state().halt_reason.empty() && ExecutionReviewAccounts(engine.state()).empty(),
        "eight explicit refusals never become uncertain account quarantine or global HALT");
    const auto rejected_it=std::find_if(engine.state().intents.begin(),engine.state().intents.end(),
        [](const Intent& i){return i.state==IntentState::Rejected;});
    const auto reads=port.reconcile_reads; const auto inspected=engine.InspectIntent(rejected_it->id);
    Require(inspected.find("10019")!=std::string::npos && inspected.find("非本次服务器查询")!=std::string::npos &&
        port.reconcile_reads==reads,"refusal inspection identifies durable evidence before routine pruning");
    Require(!engine.state().openings_paused && tradecopier::management::Snapshot(engine).review==0,
        "confirmed zero-fill opening refusal is settled, not an account-wide review or pause");
    FillPending(port,engine); engine.Tick();
    port.now=33000; // Past the old 30-second timeout; close ALL source positions.
    for (auto& e:port.sources) { e.second.remaining=0; e.second.closes.push_back({30000+e.first,lot,33000}); }
    for (unsigned n=0;n<source_count*2+10;++n) { engine.Tick(); FillPending(port,engine); }
    engine.Tick();
    std::map<Id,unsigned> closes;
    for (const auto& i:port.submissions) if (i.close) ++closes[i.follower_login];
    Require(closes[1001]==prior,"the insufficient-margin account still closes every previously confirmed position");
    for (Id account=1000;account<1010;++account) if (account!=1001)
        Require(closes[account]==source_count,"all nine healthy followers close every confirmed source position");
    for (auto p:port.positions) Require(!p.second,"all actual positions including the refused account finish flat");
    Require(engine.state().halt_reason.empty() && !engine.HasPending() && !port.inbox.size() && !port.released_before_durable,
        "refusals have no 30-second timeout, no leaked callback cache, and no early evidence release");
    Require(engine.state().links.empty() && engine.state().intents.empty(),"settled no-money openings do not retain mapping capacity");
    const auto before=EncodeState(engine.state());
    { MemoryStore failed; RefusalPort fp(failed); Engine fe(Fanout(1),"failure",fp,failed);
      fe.Start({1000,10,{}}); fp.sources[100]=Source(); fp.reject_account=1000;
      fe.Admit(Opening(),11); fe.Tick(); failed.fail_at=failed.saves+1;
      fe.ConfirmPending();
      Require(fe.storage_degraded() && fe.state().halt_reason.empty() && fp.inbox.size()==0 && fp.released_before_durable,
          "degraded mode keeps refusal result in memory and releases callback capacity"); }
    store.file.reset();
    { FileJournal reader(dir/"refusals.journal"); State replay;
      Require(reader.Load(replay) && EncodeState(replay)==before,"real journal safely replays refusal settlement and pruning"); }
    std::cout<<"no-money regression: followers=10 source_positions="<<source_count<<" refused_opens=8 confirmed_opens="<<source_count*10-8
        <<" confirmed_closes="<<source_count*10-8<<" all_actual_positions_flat=1 elapsed_ms="
        <<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
}

void NoMoneyRoutineIsolation(unsigned rounds=64) {
    MemoryStore store; FakePort port(store); Engine engine(Fanout(10),"routine-refusal",port,store);
    engine.Start({1000,10,{}});
    Id sequence=11; std::size_t peak_bytes=0; unsigned rejected=0, opens=0, closes=0;
    const auto finish=[&] {
        for (const auto& i:engine.state().intents) if (i.state==IntentState::Submitted && !port.results.count(i.id)) {
            if (!i.close && i.follower_login==1001) {
                port.results[i.id]={Result::Status::Rejected,0,0,10019,{},"server_pretrade_no_money_zero_fill"}; ++rejected;
            } else {
                const Id position=i.close?i.follower_position:900000+i.id;
                port.results[i.id]={Result::Status::Filled,position,i.units,10009,{700000+i.id}};
                port.positions[{i.follower_login,position}]=i.close?i.follower_before-i.units:i.units;
                if (i.close) ++closes; else ++opens;
            }
        }
    };
    for (unsigned n=0;n<rounds;++n) {
        port.now=2000+static_cast<std::int64_t>(n)*1000;
        auto opening=Opening(100+n,10,1000+n); opening.time_ms=port.now;
        auto source=Source(); source.first_fill_ms=port.now; port.sources[opening.position]=source;
        engine.Admit(opening,sequence++);
        for (unsigned step=0;step<20;++step) { engine.Tick(); finish(); }
        Require(!engine.HasPending() && engine.state().links.size()==9 && !engine.state().openings_paused &&
            engine.state().halt_reason.empty(),"continuous margin refusals allow all nine other accounts to open");
        for (Id login=1000;login<1010;++login) if (login!=1001)
            Require(std::count_if(port.submissions.begin(),port.submissions.end(),[&](const Intent& i){return !i.close && i.follower_login==login;})==1,
                "each funded account opens exactly once for every new source signal");
        // The same failed signal must never be caught up after its record is pruned.
        engine.Admit(opening,sequence++); engine.Tick();
        Require(port.submissions.size()==10,"pruned refusal cannot resubmit the same source lifecycle");
        if (n==rounds/2) {
            engine.PauseOpenings(engine.state().revision);
            Require(engine.ApplyBlockReason().empty(),"routine refusals cannot block configuration while healthy positions remain live");
            auto next=Fanout(10); next.relationships[0].parameter="0.5";
            engine.ApplyConfig(engine.state().revision,next,{port.now+1,sequence++,{}});
            engine.ResumeOpenings(engine.state().revision,{port.now+2,sequence++,{}});
        }
        auto& current=port.sources[opening.position]; current.remaining=0;
        current.closes.push_back({90000+n,lot,port.now+10}); port.now+=10;
        for (unsigned step=0;step<20 && !engine.state().links.empty();++step) { engine.Tick(); finish(); }
        engine.Tick();
        Require(engine.state().links.empty() && engine.state().intents.empty() && !engine.HasPending() &&
            !engine.state().openings_paused && engine.state().halt_reason.empty(),"all funded account lifecycles settle under sustained refusal load");
        for (const auto& p:port.positions) Require(!p.second,"every funded account closes while another account remains underfunded");
        peak_bytes=std::max(peak_bytes,store.saved.size());
        Require(store.saved.size()<32768,"routine refusals cannot grow the active journal snapshot without bound");
        port.sources.clear(); port.results.clear(); port.positions.clear(); port.submissions.clear(); port.logs.clear();
    }
    Require(rejected==rounds && opens==rounds*9 && closes==rounds*9,"ten-account sustained rejection accounting matches exactly");
    // Funds later recover: only the next new signal is eligible, not failed history.
    port.now+=1000; auto fresh=Opening(999999,10,999999); fresh.time_ms=port.now;
    auto source=Source(); source.first_fill_ms=port.now; port.sources[fresh.position]=source;
    engine.Admit(fresh,sequence++); engine.Tick(); FillPending(port,engine); engine.Tick();
    Require(port.submissions.size()==10 && engine.state().links.size()==10,
        "recovered account participates in the next new signal without manual global resume or catch-up");
    std::cout<<"routine no-money isolation: followers=10 rejected_openings="<<rejected<<" healthy_opens="<<opens
        <<" healthy_closes="<<closes<<" peak_snapshot_bytes="<<peak_bytes<<" config_resume_passed=1 recovery_new_signal_only=1\n";
}

void NoMoneyRetirementGuards() {
    Fixture f; f.Open();
    f.port.results[1]={Result::Status::Rejected,0,0,10019,{},"server_pretrade_no_money_zero_fill"};
    f.engine.ConfirmPending();
    const auto settled=f.engine.state();
    Require(SettledNoMoneyOpening(settled.links[0],settled.intents[0]),"only durable explicit zero-fill opening evidence is eligible");
    const auto dir=std::filesystem::temp_directory_path()/("tcx-no-money-prune-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path dir; ~Cleanup(){std::error_code e;std::filesystem::remove_all(dir,e);} } cleanup{dir};
    for (unsigned mutation=0;mutation<13;++mutation) {
        auto bad=settled; bad.revision=1; auto& i=bad.intents[0]; auto& l=bad.links[0];
        switch (mutation) {
        case 0:i.close=true;i.side=Side::Sell;i.follower_position=42;i.follower_before=i.units;break;
        case 1:i.filled=1;break; case 2:i.result_position=42;break;
        case 3:i.deals={42};break; case 4:i.retcode=10012;break; case 5:i.state=IntentState::Review;break;
        case 6:l.follower_position=42;break; case 7:l.follower_remaining=1;break;
        case 8:l.processed_closes={42};break; case 9:l.reason="request_rejected_no_retry";break;
        case 10:i.request_id=0;break; case 11:i.source_event=42;break; case 12:i.follower_before=1;break;
        }
        Require(!SettledNoMoneyOpening(l,i),"partial, unknown, closing or mismatched evidence cannot be retired");
        FileJournal journal(dir/(std::to_string(mutation)+".journal")); State empty; journal.Load(empty); journal.Save(bad);
        auto removed=bad; ++removed.revision; removed.links.clear(); removed.intents.clear(); removed.source_floor_ms=2000;
        Throws([&]{journal.Prune(removed);},"journal itself refuses pruning an unproven rejection");
    }
    auto old=settled; old.links[0].phase=Phase::Review; // Durable 0.5.4 known refusal, not an unresolved incident.
    MemoryStore migrated; migrated.saved=EncodeState(old); FakePort mp(migrated);
    Engine upgraded(Config(),"test-server-6182",mp,migrated); upgraded.Start({3000,30,{}});
    Require(upgraded.state().links.empty() && upgraded.state().intents.empty() && mp.submissions.empty(),
        "0.5.4 known zero-fill rejection can be retired on restart without submitting trades");
    old.halt_reason="partial_or_uncertain_execution";
    MemoryStore halted; halted.saved=EncodeState(old); FakePort hp(halted);
    Engine he(Config(),"test-server-6182",hp,halted); he.Start({3000,30,{}});
    Require(he.state().halt_reason.empty() && he.state().links.empty() && !he.warning().empty(),"legacy HALT becomes diagnostic and proven zero-fill refusal may retire");
    MemoryStore crash; crash.saved=EncodeState(settled); FakePort cp(crash);
    Engine recovered(Config(),"test-server-6182",cp,crash); recovered.Start({3000,30,{}});
    Require(recovered.state().links.empty() && cp.submissions.empty(),"restart between refusal save and prune never repeats a refused opening");
}

void AccountIsolationBulkClose(unsigned source_count=24) {
    const auto started=std::chrono::steady_clock::now();
    const auto dir=std::filesystem::temp_directory_path()/("tcx-isolation-"+std::to_string(started.time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup { std::filesystem::path dir; ~Cleanup(){std::error_code e;std::filesystem::remove_all(dir,e);} } cleanup{dir};
    struct Disk:MemoryStore {
        std::unique_ptr<FileJournal> file;
        explicit Disk(const std::filesystem::path& p):file(std::make_unique<FileJournal>(p)) {}
        bool Load(State& s) override { if (!file->Load(s)) return false; saved=EncodeState(s); return true; }
        void Save(const State& s) override { file->Save(s); MemoryStore::Save(s); }
        void Prune(const State& s) override { file->Prune(s); MemoryStore::Save(s); }
    };
    struct Test {
        Disk store; FakePort port; Engine engine;
        explicit Test(const std::filesystem::path& p):store(p),port(store),engine(Fanout(10),"test-server-6182",port,store) {
            engine.Start({1000,10,{}});
        }
    } f(dir/"isolation.journal");
    constexpr Id isolated_account=1001;
    std::vector<Id> uncertain;
    // All ten followers open each source. The final eight openings on one
    // account remain unconfirmed while the other nine accounts keep filling.
    for (unsigned n=0;n<source_count;++n) {
        f.port.sources[100+n]=Source();
        f.engine.Admit(Opening(100+n,10,1000+n),11+n);
        f.engine.Tick();
        for (const auto& i:f.engine.state().intents) if (i.state==IntentState::Submitted) {
            if (i.follower_login==isolated_account && i.link/10>=source_count-8) {
                if (std::find(uncertain.begin(),uncertain.end(),i.id)==uncertain.end()) uncertain.push_back(i.id);
            } else f.port.Fill(i.id,i.units);
        }
        f.engine.ConfirmPending();
    }
    Require(uncertain.size()==8 && f.port.submissions.size()==source_count*10,"fault injection has eight unconfirmed requests in ten-account fanout");
    // One request actually filled but its confirmation is invisible. Never
    // infer 'not filled' from zero evidence or replay the request later.
    f.port.Fill(uncertain[0],lot); f.port.results.erase(uncertain[0]);
    f.port.now=31999; f.engine.ConfirmPending();
    Require(!f.engine.state().openings_paused && f.engine.HasPending(),"29,999 ms is still inside confirmation window");
    for (auto& e:f.port.sources) {
        e.second.remaining=0; e.second.closes.push_back({30000+e.first,lot,32000});
    }
    f.port.now=32000; f.engine.Tick();
    Require(f.engine.state().halt_reason.empty() && !f.engine.state().openings_paused &&
        ExecutionReviewAccounts(f.engine.state())==std::vector<Id>{isolated_account},"30-second timeout isolates only the eight unresolved mappings");
    const auto durable=DecodeState(f.store.saved);
    Require(!durable.openings_paused && ExecutionReviewAccounts(durable)==std::vector<Id>{isolated_account},
        "mapping reviews are durable and other closes are still admitted");
    const auto view=management::Snapshot(f.engine);
    Require(view.review==8 && view.apply_block.empty() && !view.paused,"review counts do not block independent subscription changes");
    Require(!f.engine.state().openings_paused,"pending execution review does not pause other mappings");
    for (unsigned step=0;step<source_count*2;++step) {
        FillPending(f.port,f.engine); f.engine.Tick();
        if (!f.engine.HasPending()) break;
    }
    std::map<Id,unsigned> closes;
    for (const auto& i:f.port.submissions) if (i.close) {
        ++closes[i.follower_login];
    }
    for (Id account=1000;account<1010;++account) if (account!=isolated_account)
        Require(closes[account]==source_count,"every healthy account completes every bulk close exactly once");
    for (const auto& position:f.port.positions) if (position.first.first!=isolated_account)
        Require(!position.second,"healthy follower accounts finish flat");
    Require(f.engine.state().links.size()==8 && closes[isolated_account]==source_count-8 && f.port.positions[{isolated_account,9000+uncertain[0]}]==lot,
        "only uncertain mappings remain; confirmed positions on that same account close");
    const auto before=EncodeState(f.engine.state()); const auto sends=f.port.submissions.size();
    f.port.Fill(uncertain[0],lot);
    const auto evidence=f.engine.InspectIntent(uncertain[0]);
    Require(evidence.find("完整成交")!=std::string::npos && EncodeState(f.engine.state())==before,
        "read-only inspection cannot mutate unresolved mapping evidence");
    f.engine.Admit(Opening(99999,10,99999),99999); f.engine.Tick();
    Require(f.port.submissions.size()==sends,"inspection and an expired source callback never replay an unresolved mapping");
    // Release the writer lease before reading/reopening the real journal.
    f.store.file.reset();
    { FileJournal reader(dir/"isolation.journal"); State replay;
      Require(reader.Load(replay) && EncodeState(replay)==before,"real journal replays mapping reviews and all confirmed close pruning exactly"); }
    f.store.file=std::make_unique<FileJournal>(dir/"isolation.journal");
    Engine restarted(Fanout(10),"test-server-6182",f.port,f.store); restarted.Start({33000,99999,{}}); restarted.Tick();
    Require(restarted.state().halt_reason.empty() && restarted.state().links.size()==8 && f.port.submissions.size()==sends,
        "restart retains unresolved mappings for review without a global stop");
    std::cout<<"account isolation simulation: source_positions="<<source_count<<" followers=10 uncertain=8 healthy_closes="
        <<source_count*9<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
}

void AccountIsolationGuards() {
    // Partial close and uncertain evidence share a per-mapping boundary.
    for (bool partial:{false,true}) {
        Fixture f(Fanout(3)); f.Open(); FillPending(f.port,f.engine); f.engine.Tick();
        f.CloseSource(2001,lot/2); f.engine.Tick();
        const auto failed=f.port.submissions[4];
        if (partial) f.port.Fill(failed.id,lot/4);
        else f.port.results[failed.id].status=Result::Status::Uncertain;
        for (const auto& i:f.engine.state().intents) if (i.state==IntentState::Submitted && i.id!=failed.id) f.port.Fill(i.id,i.units);
        f.engine.Tick();
        Require(f.engine.state().halt_reason.empty() && !f.engine.state().openings_paused &&
            ExecutionReviewAccounts(f.engine.state())==std::vector<Id>{1001},"partial/uncertain close isolates only its mapping");
        f.CloseSource(2002,lot/2); f.engine.Tick(); FillPending(f.port,f.engine); f.engine.Tick();
        Require(f.engine.state().links.size()==1 && !f.port.positions[{1000,9001}] && !f.port.positions[{1002,9003}],
            "healthy accounts complete remaining partial-close lifecycle without replaying the ambiguous close");
    }
    Fixture candidates(Fanout(2)); candidates.Open(); candidates.port.Fill(1,lot);
    candidates.engine.ConfirmPending(); candidates.port.sources[101]=Source();
    candidates.engine.Admit(Opening(101,10,1001),12);
    candidates.port.now=32000; candidates.engine.Tick();
    Require(candidates.port.submissions.size()==2 && candidates.engine.state().links.size()==2,
        "isolation drops unsubmitted candidates without filling or retaining them as pending trades");
    Fixture failed(Fanout(2)); failed.Open(); failed.port.Fill(1,lot); failed.engine.ConfirmPending();
    failed.CloseSource(2001,lot); failed.port.allow_degraded_close=true; failed.port.now=32000; failed.store.fail_at=failed.store.saves+1;
    failed.engine.Tick();
    failed.engine.Tick();
    Require(failed.engine.storage_degraded() && failed.engine.state().halt_reason.empty() && failed.port.submissions.size()==3 && failed.port.submissions.back().close,
        "known healthy close continues in memory despite unavailable journal");
}

void ConfirmationReuseAndTiming() {
    Fixture f; f.Open();
    auto reads=f.port.reconcile_reads;
    f.engine.Tick(true);
    Require(f.port.reconcile_reads==reads+1,"reuse without a preceding confirmation must obtain fresh evidence");
    f.engine.ConfirmPending();
    reads=f.port.reconcile_reads; const auto cycles=f.port.read_cycles;
    f.engine.AdmitBatch({},false); f.engine.Tick(true);
    Require(f.port.reconcile_reads==reads && f.port.read_cycles==cycles,
        "same worker pass reuses confirmation and history cache exactly once");
    f.engine.Tick(true);
    Require(f.port.reconcile_reads==reads+1,"next worker pass cannot reuse consumed confirmation");
    WorkerWake wake; const auto token=wake.Snapshot();
    f.engine.ConfirmPending(); reads=f.port.reconcile_reads;
    f.port.Fill(1,lot); f.port.results[1].first_deal_ms=2012; f.port.results[1].last_deal_ms=2013;
    wake.Signal(); f.engine.Tick(wake.Snapshot()==token);
    Require(f.port.reconcile_reads==reads+1 && f.engine.state().links[0].follower_position==9001,
        "fill callback between confirmation and tick forces fresh confirmation before planning");
    Require(std::any_of(f.port.logs.begin(),f.port.logs.end(),[](const std::string& s) {
        return s.find("CONFIRMED intent=1 ")==0 && s.find("target_first_deal_ms=2012 target_last_deal_ms=2013")!=std::string::npos;
    }),"confirmed log carries verified fill timestamps for external latency joins");
    const auto saved=DecodeState(f.store.saved);
    Require(saved.intents[0].state==IntentState::Confirmed && f.engine.performance().confirmation_saves==1,
        "timing instrumentation retains durable confirmation");
    Require(f.engine.performance().confirmation_reuses==1,"only the eligible confirmation was reused");

    Fixture failed; failed.Open(); failed.engine.ConfirmPending();
    failed.port.throw_begin_cycle=true;
    Throws([&]{failed.engine.ConfirmPending();},"cycle failure propagates");
    failed.port.throw_begin_cycle=false; reads=failed.port.reconcile_reads;
    failed.engine.Tick(true);
    Require(failed.port.reconcile_reads==reads+1,"failed pass cannot leave a reusable confirmation token");

    Fixture invalid; invalid.Open(); invalid.port.Fill(1,lot);
    invalid.port.results[1].first_deal_ms=3000; invalid.port.results[1].last_deal_ms=2000;
    invalid.engine.Tick();
    Require(invalid.engine.state().links[0].phase==Phase::Active &&
        std::none_of(invalid.port.logs.begin(),invalid.port.logs.end(),[](const std::string& s) {
            return s.find("target_first_deal_ms=")!=std::string::npos;
        }),"invalid optional diagnostic timestamps neither claim latency nor change execution");
}

void WorkerWakeTests() {
    WorkerWake wake; std::atomic<bool> stopping{false};
    const auto during_work=wake.Snapshot(); wake.Signal();
    Require(wake.WaitSince(during_work,std::chrono::seconds(1),stopping),"callback during SDK work cannot be lost before worker enters wait");
    const auto idle=wake.Snapshot();
    Require(!wake.WaitSince(idle,std::chrono::milliseconds(1),stopping),"idle worker retains bounded polling fallback");
    std::thread callback([&] { wake.Signal(); });
    const bool signalled=wake.WaitSince(idle,std::chrono::seconds(1),stopping); callback.join();
    Require(signalled,"callback can wake a concurrent waiter");
    stopping.store(true);
    Require(wake.WaitSince(wake.Snapshot(),std::chrono::seconds(1),stopping),"shutdown does not wait for the polling timer");
}

void ConcurrentQueue() {
    EventQueue<Id,4> small;
    Require(small.Push(1)&&small.Push(2)&&small.Push(3)&&small.Push(4)&&!small.Push(5),"bounded queue detects overflow");
    Id value=0,sequence=0;
    for (Id i=1;i<=4;++i) Require(small.Pop(value,sequence)&&value==i&&sequence==i,"queue maintains reservation order");
    EventQueue<Id,1024> queue;
    constexpr unsigned producers=4,per=10000;
    std::vector<std::thread> threads;
    for (unsigned p=0;p<producers;++p) threads.emplace_back([&,p] {
        for (unsigned n=1;n<=per;++n) while(!queue.Push(static_cast<Id>(p)*per+n)) std::this_thread::yield();
    });
    std::array<unsigned,producers> last{};
    for (unsigned n=0;n<producers*per;) {
        if (!queue.Pop(value,sequence)) { std::this_thread::yield(); continue; }
        const auto producer=static_cast<unsigned>((value-1)/per),ordinal=static_cast<unsigned>((value-1)%per)+1;
        Require(producer<producers && ordinal==++last[producer] && sequence==++n,"concurrent callbacks delivered once in sequence");
    }
    for (auto& t:threads) t.join();
    Require(!queue.Pop(value,sequence),"concurrent queue fully drained");
}
void LiveSubscriptions() {
    Fixture f(Fanout(2)); f.Open(); FillPending(f.port,f.engine); f.engine.Tick();
    auto joined=Fanout(3); const auto old_revision=f.engine.state().revision;
    f.engine.UpdateSubscriptions(f.engine.state().config_json,joined,{2500,20,{}});
    Require(!f.engine.state().openings_paused && f.engine.state().activation_ms==1000 &&
        f.engine.state().revision>old_revision,"live join does not reset other subscriptions or pause dispatch");
    auto queued=Opening(101,10,1001); queued.time_ms=2400;
    f.port.sources[101]=Source(); f.port.sources[101].first_fill_ms=2400; f.port.now=2600;
    f.engine.Admit(queued,19); f.engine.Tick(); FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.port.submissions.size()==4,"queued event predating join still reaches both original followers only");
    auto split=Opening(102,10,1002); split.time_ms=2600;
    f.port.sources[102]=Source(); f.port.sources[102].first_fill_ms=2400;
    f.engine.Admit(split,21); f.engine.Tick(); FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.port.submissions.size()==6,"later partial fill cannot enroll an old source position into new relation");
    auto fresh=Opening(103,10,1003); fresh.time_ms=3000; f.port.now=3000;
    f.port.sources[103]=Source(); f.port.sources[103].first_fill_ms=3000;
    f.engine.Admit(fresh,22); f.engine.Tick();
    Require(f.port.submissions.size()==9,"new source opening reaches new subscriber immediately");
    const auto waiting=f.port.submissions.back();
    f.port.positions[{1002,987654}]=lot; // Manual position outside any mapping.
    f.engine.UpdateSubscriptions(f.engine.state().config_json,Fanout(2),{3100,23,{}});
    f.engine.Tick(); Require(f.port.submissions.size()==9,"exit waits for already-submitted opening confirmation");
    FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.port.submissions.size()==10 && f.port.submissions.back().close &&
        f.port.submissions.back().follower_login==1002 && f.port.submissions.back().follower_position==9000+waiting.id,
        "exit closes only its own late-confirmed opening while source remains open");
    FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.port.positions[{1002,987654}]==lot && f.engine.state().links.size()==8,
        "exit leaves manual positions and both original subscriber mappings untouched");
    Throws([&]{f.engine.UpdateSubscriptions(WriteConfigJson(joined),joined,{3200,24,{}});},"stale configuration cannot overwrite subscription changes");
    f.engine.UpdateSubscriptions(f.engine.state().config_json,joined,{3200,24,{}});
    f.engine.Admit(fresh,25); f.engine.Tick();
    Require(f.port.submissions.size()==10,"rejoin never catches up a still-open historical source position");
    auto edited=joined; edited.relationships[0].parameter="0.5";
    f.engine.UpdateSubscriptions(f.engine.state().config_json,edited,{3300,26,{}});
    for (const auto& l:f.engine.state().links) Require(l.opening_rule->parameter=="1","old mappings retain their frozen opening rule after hot edit");
    for (auto& source:f.port.sources) { source.second.remaining=0; source.second.closes={{60000+source.first,lot,3400}}; }
    for (unsigned n=0;n<8;++n) {f.engine.Tick(); FillPending(f.port,f.engine);}
    Require(f.engine.state().links.empty() && f.port.submissions.size()==18,"remaining subscribers finish their own source closes once after churn");
}

void SubscriptionRestartSequence() {
    const auto dir=std::filesystem::temp_directory_path()/("tcx-subscription-restart-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}} cleanup{dir};
    struct Disk:MemoryStore {
        FileJournal file; explicit Disk(const std::filesystem::path& p):file(p){}
        bool Load(State& s)override{if(!file.Load(s))return false;saved=EncodeState(s);return true;}
        void Save(const State& s)override{file.Save(s);MemoryStore::Save(s);}
        void Prune(const State& s)override{file.Prune(s);MemoryStore::Save(s);}
    };
    const auto config=Fanout(3);
    State before;
    {
        Disk store(dir/"state.journal");FakePort port(store);
        Engine engine(Fanout(2),"restart-sequence",port,store,true);engine.Start({1000,0,{}});
        engine.UpdateSubscriptions(engine.state().config_json,config,{2500,20000,{}});
        before=engine.state();
        Require(before.subscriptions.size()==3 && before.subscriptions.back().sequence==20000,
            "late third subscription retains the previous queue high watermark");
    }
    State after;
    {
        Disk store(dir/"state.journal");FakePort port(store);port.now=5000;
        Engine engine(Fanout(2),"restart-sequence",port,store,true);
        engine.Start({4000,0,{}}); // A new server EventQueue starts at zero.
        auto fresh=Opening();fresh.time_ms=5000;
        port.sources[100]=Source();port.sources[100].first_fill_ms=5000;
        engine.Admit(fresh,1);engine.Tick();
        Require(port.submissions.size()==3,"first post-restart opening must reach all three subscriptions, including late joiner");
        Require(!engine.storage_degraded() && engine.state().config_json==before.config_json,
            "restart retains authoritative configuration with a durable queue barrier reset");
        for(std::size_t n=0;n<before.subscriptions.size();++n)
            Require(engine.state().subscriptions[n].rule==before.subscriptions[n].rule &&
                engine.state().subscriptions[n].time_ms==before.subscriptions[n].time_ms &&
                engine.state().subscriptions[n].sequence==0,"restart preserves join time and rebases only process-local sequence");
        FillPending(port,engine);engine.Tick();
        port.sources[100].remaining=0;port.sources[100].closes={{2001,lot,5100}};
        engine.NotifySourceClose(10,100);engine.Tick();FillPending(port,engine);engine.Tick();
        Require(port.submissions.size()==6 && engine.state().links.empty(),"all three post-restart openings close exactly once");
        auto old=Opening(101,10,1001);old.time_ms=3500;
        engine.Admit(old,2);engine.Tick();
        Require(port.submissions.size()==6,"restart never backfills an opening before startup");
        // A live join in this new process still fences callbacks already queued.
        auto four=Fanout(4);engine.UpdateSubscriptions(engine.state().config_json,four,{6000,4,{}});
        fresh=Opening(102,10,1002);fresh.time_ms=6001;port.now=6001;
        port.sources[102]=Source();port.sources[102].first_fill_ms=6001;
        engine.Admit(fresh,4);engine.Tick();FillPending(port,engine);engine.Tick();
        Require(port.submissions.size()==9,"new session join barrier still excludes pre-join queued signal only for new account");
        fresh=Opening(103,10,1003);fresh.time_ms=7000;port.now=7000;
        port.sources[103]=Source();port.sources[103].first_fill_ms=7000;
        engine.Admit(fresh,5);engine.Tick();FillPending(port,engine);engine.Tick();
        Require(port.submissions.size()==13,"new session subscriber receives the next fresh source position");
        after=engine.state();
    }
    { FileJournal file(dir/"state.journal");State replay;
      Require(file.Load(replay) && EncodeState(replay)==EncodeState(after),"restart barrier and subsequent joins replay exactly from disk"); }
    for(unsigned variant=0;variant<8;++variant) {
        FileJournal file(dir/("guard-"+std::to_string(variant)+".journal"));State empty;
        Require(!file.Load(empty),"new restart guard journal");auto original=before;original.revision=1;file.Save(original);
        auto changed=original;++changed.revision;changed.activation_ms=4000;changed.activation_sequence=7;
        for(auto& b:changed.subscriptions)b.sequence=7;
        switch(variant) {
        case 0: --changed.subscriptions.back().time_ms;break;
        case 1: changed.subscriptions.pop_back();break;
        case 2: changed.subscriptions.back().rule=99;break;
        case 3: changed.subscriptions.back().sequence=8;break;
        case 4: changed.subscriptions.front().sequence=0;break;
        case 5: changed.activation_ms=before.activation_ms;changed.activation_sequence=before.activation_sequence;
                for(auto& b:changed.subscriptions)b.sequence=changed.activation_sequence;break;
        case 6: std::swap(changed.subscriptions.front(),changed.subscriptions.back());break;
        default: break;
        }
        if(variant<7) Throws([&]{file.Save(changed);},"restart exception must not relax join times, rule identity or queue watermark guards");
        else file.Save(changed);
    }
    std::cout<<"subscription restart sequence: late_join_watermark=20000 new_queue_watermark=0 all_three_opened=1 all_three_closed=1 old_openings_skipped=1\n";
}

void SubscriptionRestartWithExistingState() {
    auto initial=Fanout(2);initial.relationships[0].parameter="0.5";
    Fixture f(initial);f.Open();FillPending(f.port,f.engine);f.engine.Tick();
    auto config=initial;config.relationships.push_back({3,10,1002,true,SizingMode::LotRatio,"2"});
    f.engine.UpdateSubscriptions(f.engine.state().config_json,config,{2500,20000,{}});
    auto seed=f.engine.state();
    for(unsigned n=0;n<1000;++n) {
        auto review=seed.links.front();review.source=Opening(1000+n,10,5000+n,lot/100);
        review.source_initial=review.source_remaining=lot/100;review.follower_position=review.follower_remaining=0;
        review.phase=Phase::Review;review.reason="skip_below_minimum";seed.links.push_back(review);
    }
    for(bool paused:{false,true}) for(Id watermark:{Id(0),Id(7)}) {
        seed.openings_paused=paused;
        MemoryStore store;store.saved=EncodeState(seed);FakePort port(store);port.positions=f.port.positions;
        port.sources=f.port.sources;port.now=5000;
        Engine engine(initial,seed.identity,port,store,true);engine.Start({4000,watermark,{{10,100}}});
        Require(engine.state().links.size()==2 && engine.state().openings_paused==paused &&
            engine.state().config_json==seed.config_json && engine.state().next_intent_id==seed.next_intent_id,
            "1000 minimum reviews retire without changing config, pause, old positions or intent counter");
        for(const auto& link:engine.state().links)
            Require(link.phase==Phase::Active && port.positions[{link.opening_rule->follower_login,link.follower_position}]==link.follower_remaining,
                "old healthy positions remain bound across queue reset and minimum migration");
        auto fresh=Opening(101,10,1001);fresh.time_ms=5000;
        port.sources[101]=Source();port.sources[101].first_fill_ms=5000;
        engine.Admit(fresh,watermark);engine.Tick();
        Require(port.submissions.empty(),"callbacks at the new startup queue watermark remain excluded");
        engine.Admit(fresh,watermark+1);engine.Tick();
        Require(port.submissions.size()==(paused?0U:3U),"restart respects operator pause and admits every enabled subscriber otherwise");
        FillPending(port,engine);engine.Tick();
        port.sources[100].remaining=0;port.sources[100].closes={{2001,lot,5100}};
        engine.NotifySourceClose(10,100);engine.Tick();FillPending(port,engine);engine.Tick();
        Require(port.submissions.size()==(paused?2U:5U) && !port.positions[{1000,9001}] && !port.positions[{1001,9002}],
            "pre-restart old positions close under their original sizes even while new openings are paused");
        Require(std::count_if(port.logs.begin(),port.logs.end(),[](const std::string& s){
            return s.find("MIGRATION cleared_unsubmitted_minimum_reviews=1000;")!=std::string::npos;
        })==1,"minimum review migration remains independently visible");
    }
    seed.openings_paused=false;
    MemoryStore failed;failed.saved=EncodeState(seed);failed.fail_at=1;FakePort port(failed);
    port.positions=f.port.positions;port.sources=f.port.sources;port.now=5000;
    Engine engine(initial,seed.identity,port,failed,true);engine.Start({4000,0,{}});
    auto fresh=Opening(101,10,1001);fresh.time_ms=5000;
    engine.Admit(fresh,1);engine.Tick();
    Require(engine.storage_degraded() && port.submissions.empty() && engine.state().halt_reason.empty(),
        "failed restart write blocks unrecorded openings without global halt");
    failed.fail_at=0;Engine retry(initial,seed.identity,port,failed,true);retry.Start({6000,0,{}});
    fresh.time_ms=7000;port.now=7000;port.sources[101]=Source();port.sources[101].first_fill_ms=7000;
    retry.Admit(fresh,1);retry.Tick();
    Require(!retry.storage_degraded() && port.submissions.size()==3,"retry after interrupted restart restores all three subscriptions");
    std::cout<<"subscription restart state: minimum_reviews=1000 paused_and_nonzero_watermarks_checked=1 old_positions_preserved=1 write_failure_recovery=1\n";
}

void SharedAccountExitAndFaults() {
    auto config=Config(); config.relationships.push_back({2,11,20,true,SizingMode::LotRatio,"1"});
    config.relationships.push_back({3,10,30,true,SizingMode::LotRatio,"1"});
    Fixture f(config); f.Open(); FillPending(f.port,f.engine); f.engine.Tick();
    f.port.sources[101]=Source(); f.engine.Admit(Opening(101,11,1001),12); f.engine.Tick(); FillPending(f.port,f.engine); f.engine.Tick();
    auto next=config; next.relationships.erase(next.relationships.begin());
    f.engine.UpdateSubscriptions(f.engine.state().config_json,next,{2500,20,{}}); f.engine.Tick();
    Require(f.port.submissions.size()==4 && f.port.submissions.back().follower_position==9001,
        "removing one source subscription cannot flatten the whole target account");
    FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.port.positions[{20,9003}]==lot && f.port.positions[{30,9002}]==lot,"shared account and other target subscription retain positions");
    next.relationships.clear();
    f.engine.UpdateSubscriptions(f.engine.state().config_json,next,{2600,21,{}});
    f.engine.Tick(); FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.engine.state().links.empty() && f.port.submissions.size()==6,"empty subscription configuration drains all copier mappings without global halt");
    // A stop-out before confirmation is a verified volume reduction, not a global fault.
    Fixture stopout(Fanout(3)); stopout.Open(); FillPending(stopout.port,stopout.engine);
    stopout.port.positions[{1001,9002}]=0; stopout.engine.Tick();
    Require(stopout.engine.state().links.size()==2 && stopout.engine.state().halt_reason.empty(),"full stopout between fill and confirmation retires only that mapping");
    stopout.port.sources[101]=Source(); stopout.engine.Admit(Opening(101,10,1001),12); stopout.engine.Tick();
    Require(stopout.port.submissions.size()==6,"stopout does not stop future signals on any subscription");
}

void SubscriptionJournalRecovery() {
    const auto dir=std::filesystem::temp_directory_path()/("tcx-live-subscriptions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}} cleanup{dir};
    struct Disk:MemoryStore {
        FileJournal file; explicit Disk(const std::filesystem::path& p):file(p,4096){}
        bool Load(State& s)override {if(!file.Load(s))return false; saved=EncodeState(s);return true;}
        void Save(const State& s)override{file.Save(s);MemoryStore::Save(s);}
        void Prune(const State& s)override{file.Prune(s);MemoryStore::Save(s);}
    };
    State saved;
    std::map<std::pair<Id,Id>,Units> positions;
    auto config=Fanout(3); auto next=Fanout(2);
    {
        Disk store(dir/"live.journal"); FakePort port(store); port.sources[100]=Source();
        Engine engine(config,"live",port,store,true);engine.Start({1000,10,{}});
        engine.Admit(Opening(),11);engine.Tick();FillPending(port,engine);engine.Tick();
        engine.UpdateSubscriptions(engine.state().config_json,next,{2500,20,{}});
        saved=engine.state(); positions=port.positions;
        Require(saved.links.back().exit_requested && store.file.rotations()>0,"live configuration publishes durable exit intent through checkpoint");
        auto corrupt=saved;++corrupt.revision;corrupt.links.back().exit_requested=false;
        Throws([&]{store.file.Save(corrupt);},"journal forbids silently cancelling exit intent");
    }
    {
        Disk store(dir/"live.journal");FakePort port(store);port.positions=positions;port.sources[100]=Source();port.now=3000;
        Engine engine(config,"live",port,store,true);engine.Start({3000,30,{}});engine.Tick();
        Require(port.submissions.size()==1 && port.submissions[0].close && port.submissions[0].follower_login==1002,
            "restart resumes durable exit without changing other subscriptions");
        FillPending(port,engine);engine.Tick();
        Require(engine.state().links.size()==2 && engine.state().config_json==WriteConfigJson(next),"exit completion prunes only drained relation and keeps authoritative config");
    }
    Fixture fail(Fanout(2));fail.Open();FillPending(fail.port,fail.engine);fail.engine.Tick();
    const auto previous=fail.engine.state().config_json;fail.store.fail_at=fail.store.saves+1;
    Throws([&]{fail.engine.UpdateSubscriptions(previous,Fanout(1),{2500,20,{}});},"failed durable exit is never acknowledged");
    Require(fail.engine.state().config_json==previous && !fail.engine.state().links.back().exit_requested && fail.engine.storage_degraded(),
        "failed exit commit preserves original relationship ownership in memory");
}

void ExitingUnknownAndDegradedCloses() {
    Fixture f(Fanout(2));f.port.submission_status_by_login[1001]=Submission::Status::Unknown;f.Open();
    f.port.Fill(1,lot);f.engine.Tick();
    f.engine.UpdateSubscriptions(f.engine.state().config_json,Fanout(1),{2500,20,{}});
    f.port.now=2500;f.engine.Tick();
    Require(f.port.submissions.size()==2 && f.engine.state().links[1].exit_requested &&
        f.engine.state().links[1].phase==Phase::Review,"unknown opening remains visibly exiting without a duplicate trade");
    f.port.Fill(2,lot); f.port.submission_status_by_login.clear();f.port.now=3000;f.engine.Tick();
    Require(f.port.submissions.size()==3 && f.port.submissions.back().close && f.port.submissions.back().follower_position==9002,
        "late full history proof permits exact-position exit after an unknown submit");
    FillPending(f.port,f.engine);f.engine.Tick();
    Require(f.engine.state().links.size()==1 && f.port.positions[{1000,9001}]==lot && !f.port.positions[{1001,9002}],
        "reconciled exit never changes other subscriber positions");
    Fixture reviewed;reviewed.Open();reviewed.Confirm();reviewed.port.throw_read=true;reviewed.engine.Tick();
    Require(reviewed.engine.state().links[0].phase==Phase::Review,"source lookup failure isolates mapped position");
    auto disabled=Config();disabled.relationships.clear();
    reviewed.engine.UpdateSubscriptions(reviewed.engine.state().config_json,disabled,{2500,20,{}});reviewed.engine.Tick();
    Require(reviewed.port.submissions.size()==2 && reviewed.port.submissions.back().close,
        "explicit exit can close verified target despite unreadable source history");
    reviewed.Confirm();Require(reviewed.engine.state().links.empty(),"source-read review does not strand an explicitly exiting known position");
    Fixture disk(Fanout(2));disk.Open();FillPending(disk.port,disk.engine);disk.engine.Tick();
    disk.port.allow_degraded_close=true;disk.store.fail_at=disk.store.saves+1;
    disk.CloseSource(2001,lot);disk.engine.Tick();FillPending(disk.port,disk.engine);disk.engine.Tick();
    Require(disk.engine.storage_degraded() && disk.engine.state().halt_reason.empty() && disk.engine.state().links.empty() &&
        disk.port.submissions.size()==4 && !disk.port.positions[{1000,9001}] && !disk.port.positions[{1001,9002}],
        "storage failure before close preparation cannot globally terminate verifiable close management");
    disk.port.sources[101]=Source();disk.engine.Admit(Opening(101,10,1001),12);disk.engine.Tick();
    Require(disk.port.submissions.size()==4,"storage degraded mode never creates unrecorded new positions");
}

void LiveSubscriptionStress(unsigned source_count=1000) {
    const auto started=std::chrono::steady_clock::now();
    struct BatchPort:FakePort {
        unsigned checked_save=0;std::map<Id,Intent> prepared;
        explicit BatchPort(MemoryStore& s):FakePort(s){}
        Submission Submit(const Intent& i)override {
            if (checked_save!=store.saves) {
                prepared.clear(); for (const auto& intent:DecodeState(store.saved).intents)
                    if (intent.state==IntentState::Prepared) prepared.emplace(intent.id,intent);
                checked_save=store.saves;
            }
            Require(prepared.count(i.id) && prepared.at(i.id).follower_login==i.follower_login && prepared.at(i.id).units==i.units,
                "churn stress retains durable request identity before submission");
            submissions.push_back(i);return {Submission::Status::Queued,static_cast<unsigned>(1000+i.id),0};
        }
    };
    MemoryStore store;BatchPort port(store);auto config=Fanout(10);Engine engine(config,"churn-stress",port,store,true);
    engine.Start({1000,10,{}});Id sequence=11;std::size_t peak_links=0,peak_snapshot=0;unsigned joins=0,exits=0;
    const auto finish=[&]{
        for (const auto& i:engine.state().intents) if (i.state==IntentState::Submitted) {
            const auto position=i.close?i.follower_position:900000+i.id;
            port.results[i.id]={Result::Status::Filled,position,i.units,10009,{700000+i.id}};
            port.positions[{i.follower_login,position}]=i.close?i.follower_before-i.units:i.units;
        }
    };
    const unsigned hold=300;
    for (unsigned n=0;n<source_count+hold+4;++n) {
        port.now=2000+static_cast<std::int64_t>(n)*1000/30;
        if (n==source_count*3/10 || n==source_count*6/10 || n==source_count*8/10) {
            if (n==source_count*3/10) {config.relationships[9].enabled=false;++exits;}
            if (n==source_count*6/10) {config.relationships[9].enabled=true;++joins;}
            if (n==source_count*8/10) {config.relationships[8].enabled=false;++exits;}
            engine.UpdateSubscriptions(engine.state().config_json,config,{port.now-1,sequence-1,{}});
        }
        if (n<source_count) {
            auto deal=Opening(100+n,10,1000+n);deal.time_ms=port.now;
            auto source=Source();source.first_fill_ms=port.now;port.sources[deal.position]=source;
            engine.Admit(deal,sequence++);
        }
        if (n>=hold && n-hold<source_count) {
            auto& source=port.sources[100+n-hold];source.remaining=0;source.closes={{60000+n,lot,port.now}};
        }
        for (unsigned round=0;round<3;++round) {engine.Tick();finish();}
        Require(!engine.state().openings_paused && engine.state().halt_reason.empty() && !engine.storage_degraded(),
            "subscription churn never pauses or globally terminates other followers");
        peak_links=std::max(peak_links,engine.state().links.size());
        if (n%30==0) peak_snapshot=std::max(peak_snapshot,EncodeState(engine.state()).size());
    }
    for (unsigned n=0;n<100 && !engine.state().links.empty();++n){engine.Tick();finish();}engine.Tick();
    std::map<Id,unsigned> opens,closes;
    for (const auto& i:port.submissions) ++(i.close?closes:opens)[i.follower_login];
    for (Id account=1000;account<1008;++account) Require(opens[account]==source_count && closes[account]==source_count,
        "every unchanged subscription receives all 1000 source opens and closes exactly once");
    const auto expected_rejoin=source_count*3/10+source_count-source_count*6/10;
    Require(opens[1009]==expected_rejoin && closes[1009]==expected_rejoin &&
        opens[1008]==source_count*8/10 && closes[1008]==source_count*8/10,"joining skips old positions and each exiting relationship fully drains");
    for (const auto& p:port.positions) Require(!p.second,"all actual stress target positions finish flat");
    Require(engine.state().links.empty() && engine.state().intents.empty(),"churn stress leaves no orphan mapping or unresolved request");
    std::cout<<"live subscription stress: followers=10 source_positions="<<source_count<<" rate_per_second=30 hold_ms=10000 joins="<<joins
        <<" exits="<<exits<<" target_opens="<<port.submissions.size()/2<<" target_closes="<<port.submissions.size()/2
        <<" unchanged_subscribers_all_matched=1 all_targets_flat=1 peak_links="<<peak_links<<" peak_snapshot_bytes="<<peak_snapshot
        <<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()
        <<" logical_clock_not_mt5_latency=1"<<std::endl;
}


void DirectionLifecycles() {
    const SizingMode modes[]={SizingMode::LotRatio,SizingMode::EquityRatio,SizingMode::FixedLots};
    const char* parameters[]={"0.5","1.5","0.2"};
    const Units volumes[]={lot/2,3*lot/4,lot/5};
    for (auto direction:{CopyDirection::Forward,CopyDirection::Reverse}) for (auto source_side:{Side::Buy,Side::Sell})
    for (unsigned n=0;n<3;++n) {
        auto config=Config(modes[n],parameters[n]); config.relationships[0].direction=direction;
        config.relationships[0].auto_symbol=true;
        Fixture f(config); auto source=Opening(); source.side=source_side;
        const auto target=direction==CopyDirection::Forward?source_side:(source_side==Side::Buy?Side::Sell:Side::Buy);
        f.engine.Admit(source,11); f.engine.Tick();
        Require(f.port.submissions.size()==1 && f.port.submissions[0].side==target && f.port.submissions[0].units==volumes[n],
            "direction transforms side without altering any sizing mode");
        Require(f.port.resolution_sides==std::vector<Side>{target} && f.port.limit_sides.back()==std::make_pair(target,false),
            "symbol resolution and opening eligibility check target side");
        f.Confirm();
        const auto view=tradecopier::management::Snapshot(f.engine);
        Require(view.links[0][6]==std::string(source_side==Side::Buy?"Buy":"Sell")+" → "+(target==Side::Buy?"Buy":"Sell"),
            "management distinguishes source and follower position side");
        f.CloseSource(2001,2*lot/5); f.engine.Tick();
        Require(f.port.submissions.back().close && f.port.submissions.back().side!=target &&
            f.port.submissions.back().follower_position==9001 && f.port.submissions.back().units==2*volumes[n]/5,
            "partial close reverses target side and preserves proportional sizing");
        Require(f.port.limit_sides.back()==std::make_pair(target==Side::Buy?Side::Sell:Side::Buy,true),
            "close eligibility uses execution side of mapped position close");
        f.Confirm(); f.CloseSource(2002,3*lot/5); f.engine.Tick(); f.Confirm();
        Require(f.engine.state().links.empty() && f.port.positions[{20,9001}]==0,"full close ends directional lifecycle");
    }
}
void DirectionFreezeAndRestart() {
    auto config=Config(); config.relationships[0].direction=CopyDirection::Reverse;
    Fixture f(config); f.Open(); f.Confirm();
    config.relationships[0].direction=CopyDirection::Forward;
    f.port.now=2100; f.engine.UpdateSubscriptions(f.engine.state().config_json,config,{2100,11,{}});
    auto fresh=Opening(101,10,1001); fresh.time_ms=2200;
    f.port.now=2200; f.port.sources[101]=Source(); f.port.sources[101].first_fill_ms=2200;
    f.engine.Admit(fresh,12); f.engine.Tick(); f.Confirm();
    Require(f.port.submissions[0].side==Side::Sell && f.port.submissions[1].side==Side::Buy,
        "direction edit affects new source positions only");
    Require(f.engine.state().links[0].opening_rule->direction==CopyDirection::Reverse &&
        f.engine.state().links[1].opening_rule->direction==CopyDirection::Forward,"old and new frozen rules remain distinct");
    FakePort restored(f.store); restored.now=2400; restored.sources=f.port.sources;
    restored.positions=f.port.positions; restored.position_sides=f.port.position_sides;
    Engine engine(config,"test-server-6182",restored,f.store,true); engine.Start({2300,50,{}});
    Require(engine.state().links[0].phase==Phase::Active && engine.state().links[1].phase==Phase::Active && restored.submissions.empty(),
        "restart verifies reverse and forward positions without reopening");
    for (auto& source:restored.sources) {source.second.remaining=3*lot/5;source.second.closes={{2000+source.first,2*lot/5,2400}};}
    engine.Tick();
    Require(restored.submissions.size()==2 && restored.submissions[0].side==Side::Buy && restored.submissions[1].side==Side::Sell,
        "after restart each close uses its original frozen direction");
    FillPending(restored,engine); engine.Tick();
    auto disabled=config; disabled.relationships.clear();
    engine.UpdateSubscriptions(engine.state().config_json,disabled,{2500,51,{}}); engine.Tick();
    Require(restored.submissions.size()==4 && restored.submissions[2].side==Side::Buy && restored.submissions[3].side==Side::Sell,
        "subscription exit drains both directions using exact mapped targets");
    FillPending(restored,engine); engine.Tick();
    Require(engine.state().links.empty(),"exiting mixed frozen directions leaves no orphan positions");
}
void ReverseManualCloseAndRefusal() {
    auto config=Config(); config.relationships[0].direction=CopyDirection::Reverse;
    Fixture f(config); f.Open(); f.Confirm();
    f.port.positions[{20,9001}]=lot/2;
    f.CloseSource(2001,2*lot/5); f.engine.Tick();
    Require(f.port.submissions.back().units==lot/5 && f.port.submissions.back().side==Side::Buy,
        "reverse follows source partial close using manually reduced target volume");
    f.Confirm(); f.port.positions[{20,9001}]=0; f.engine.Tick();
    f.CloseSource(2002,3*lot/5); f.engine.Tick(); f.engine.Admit(Opening(),12); f.engine.Tick();
    Require(f.port.submissions.size()==2 && f.engine.state().links.empty(),"manual full close of reverse position stops all further operations");
    Fixture refusal(config); refusal.Open();
    refusal.port.results[1]={Result::Status::Rejected,0,0,10019,{},"server_pretrade_no_money_zero_fill"};
    refusal.engine.Tick();
    Require(refusal.engine.state().links.empty() && refusal.port.submissions.size()==1,
        "proven zero-fill no-money reverse opening retires without retry");
}
void MixedDirectionFanout() {
    auto config=Config(); config.relationships.push_back({2,10,21,true,SizingMode::LotRatio,"1"});
    config.relationships[1].direction=CopyDirection::Reverse;
    config.relationships.push_back({3,11,20,true,SizingMode::LotRatio,"1"});
    config.relationships[2].direction=CopyDirection::Reverse;
    Fixture f(config); f.Open(); f.Confirm(); f.Confirm();
    f.port.sources[101]=Source(); f.engine.Admit(Opening(101,11,1001),12); f.engine.Tick(); f.Confirm();
    Require(f.port.submissions.size()==3 && f.port.submissions[0].side==Side::Buy && f.port.submissions[1].side==Side::Sell &&
        f.port.submissions[2].side==Side::Sell && f.port.submissions[2].follower_login==20,
        "many-to-many relationships select directions independently including one hedged follower");
    f.CloseSource(2001,lot); f.engine.Tick(); FillPending(f.port,f.engine); f.engine.Tick();
    Require(f.engine.state().links.size()==1 && f.port.positions[{20,9003}]==lot,
        "closing first source leaves opposite-direction position from another source untouched");
}

}
int main(int argc,char** argv) {
    try {
        if (argc==2 && std::string(argv[1])=="--subscription-soak") {LiveSubscriptionStress();return 0;}
        if (argc==2 && std::string(argv[1])=="--subscription-restart") {SubscriptionRestartSequence();SubscriptionRestartWithExistingState();return 0;}
        if (argc==2 && std::string(argv[1])=="--minimum-volume-soak") {MinimumOpeningRetirement(10050);return 0;}
        if (argc==2 && std::string(argv[1])=="--close-priority") {CloseSiblingPriority();SourceCloseHints();return 0;}
        if (argc==2 && std::string(argv[1])=="--journal-soak") {
            RotatingJournalLifecycles(30,FileJournal::kRotateBytes); return 0;
        }
        if (argc==2 && std::string(argv[1])=="--capacity-soak") { CapacityTenThousand(); return 0; }
        if (argc==2 && std::string(argv[1])=="--isolation-soak") { AccountIsolationBulkClose(1000); return 0; }
        if (argc==2 && std::string(argv[1])=="--no-money-soak") { NoMoneyClosesRemainManaged(1000); return 0; }
        if (argc==2 && std::string(argv[1])=="--routine-no-money-soak") { NoMoneyRoutineIsolation(10050); return 0; }
        if (argc!=1) throw std::runtime_error("Usage: tradecopier_execution_tests [--journal-soak|--capacity-soak|--isolation-soak|--no-money-soak|--routine-no-money-soak]");
        unsigned failures=0;
        const auto run=[&](const char* name,auto test) {
            try { test(); } catch (const std::exception& e) { ++failures; std::cerr<<"FAILED: "<<name<<": "<<e.what()<<std::endl; }
        };
        run("DirectionLifecycles", []{DirectionLifecycles();});
        run("DirectionFreezeAndRestart", []{DirectionFreezeAndRestart();});
        run("ReverseManualCloseAndRefusal", []{ReverseManualCloseAndRefusal();});
        run("MixedDirectionFanout", []{MixedDirectionFanout();});
        run("ExitingUnknownAndDegradedCloses", []{ExitingUnknownAndDegradedCloses();});
        run("SubscriptionRestartSequence", []{SubscriptionRestartSequence();});
        run("SubscriptionRestartWithExistingState", []{SubscriptionRestartWithExistingState();});
        run("LiveSubscriptions", []{LiveSubscriptions();}); run("SharedAccountExitAndFaults", []{SharedAccountExitAndFaults();}); run("SubscriptionJournalRecovery", []{SubscriptionJournalRecovery();});
        run("SymbolRoutingLifecycle", [] { SymbolRoutingLifecycle(); }); run("SymbolConflictIsolation", [] { SymbolConflictIsolation(); });
        run("SymbolClosePriorityAndDurability", [] { SymbolClosePriorityAndDurability(); });
        run("AccountIsolationBulkClose", [] { AccountIsolationBulkClose(); });
        run("AccountIsolationGuards", [] { AccountIsolationGuards(); });
        run("RefusalEvidenceTests", [] { RefusalEvidenceTests(); });
        run("NoMoneyClosesRemainManaged", [] { NoMoneyClosesRemainManaged(); });
        run("NoMoneyRoutineIsolation", [] { NoMoneyRoutineIsolation(); });
        run("NoMoneyRetirementGuards", [] { NoMoneyRetirementGuards(); });
        run("MinimumOpeningRetirement", [] { MinimumOpeningRetirement(); });
        run("CloseSiblingPriority", [] { CloseSiblingPriority(); });
        run("SourceCloseHints", [] { SourceCloseHints(); });
        run("ConfirmationReuseAndTiming", [] { ConfirmationReuseAndTiming(); });
        run("BatchedExpiryPruning", [] { BatchedExpiryPruning(); });
        run("Lifecycle", [] { Lifecycle(); }); run("SourceClosesDuringOpening", [] { SourceClosesDuringOpening(); }); run("SellAndPartialExecution", [] { SellAndPartialExecution(); }); run("ManualActions", [] { ManualActions(); }); run("ActivationAndAggregation", [] { ActivationAndAggregation(); }); run("EquityAndLimits", [] { EquityAndLimits(); });
        run("ManyToMany", [] { ManyToMany(); }); run("FaultsAndRestart", [] { FaultsAndRestart(); }); run("HistoryFailures", [] { HistoryFailures(); }); run("JournalFiles", [] { JournalFiles(); }); run("ManagementControls", [] { ManagementControls(); }); run("LegacyUnsubmittedReviewCleanup", [] { LegacyUnsubmittedReviewCleanup(); }); run("AddSubscriptionsWithOpenPositions", [] { AddSubscriptionsWithOpenPositions(); }); run("ActiveConfigGuards", [] { ActiveConfigGuards(); }); run("FrozenRulesAcrossEdits", [] { FrozenRulesAcrossEdits(); }); run("LegacyRuleMigration", [] { LegacyRuleMigration(); }); run("LegacyJournalUpgrade", [] { LegacyJournalUpgrade(); }); run("ActiveConfigPendingAndFailure", [] { ActiveConfigPendingAndFailure(); }); run("ActiveConfigRealJournal", [] { ActiveConfigRealJournal(); }); run("ConfigTransitionReplayGuards", [] { ConfigTransitionReplayGuards(); }); run("ManagementProtocol", [] { ManagementProtocol(); }); run("ManagedJournalFile", [] { ManagedJournalFile(); }); run("PipelineFanout", [] { PipelineFanout(); }); run("PipelineAccountOrdering", [] { PipelineAccountOrdering(); }); run("WorkerBatchFairness", [] { WorkerBatchFairness(); }); run("PipelineFaults", [] { PipelineFaults(); }); run("PipelinePauseAndPersistence", [] { PipelinePauseAndPersistence(); }); run("JournalRotationTests", [] { JournalRotationTests(); }); run("IncrementalJournalRecovery", [] { IncrementalJournalRecovery(); }); run("RotatingJournalLifecycles", [] { RotatingJournalLifecycles(); }); run("LiveRetentionAndCapacity", [] { LiveRetentionAndCapacity(); }); run("WorkerWakeTests", [] { WorkerWakeTests(); }); run("ConcurrentQueue", [] { ConcurrentQueue(); });
        if (failures) throw std::runtime_error(std::to_string(failures)+" test groups failed");
        std::cout<<"execution tests passed: "<<checks<<" checks\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<"FAILED: "<<e.what()<<'\n'; return 1; }
}

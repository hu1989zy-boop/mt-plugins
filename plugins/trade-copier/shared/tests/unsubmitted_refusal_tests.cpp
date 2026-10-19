#include "tradecopier/journal.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

using namespace tradecopier;
using namespace tradecopier::execution;
namespace {
unsigned checks=0;
constexpr bool mt4=std::string_view(kPlatformName)=="mt4";
void Check(bool ok,const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
struct Disk final : Store {
    FileJournal file;
    State saved;
    std::optional<State> refusal;
    explicit Disk(const std::filesystem::path& path):file(path) {}
    bool Load(State& state) override { const bool found=file.Load(state); if (found) saved=state; return found; }
    void Save(const State& state) override {
        file.Save(state); saved=state;
        for (const auto& link:state.links) if (link.reason=="mt4_not_submitted_no_money") refusal=state;
    }
    void Prune(const State& state) override { file.Prune(state); saved=state; }
};
struct FakePort final : Port {
    Disk* disk=nullptr;
    std::int64_t now=2000;
    Submission result{Submission::Status::NotSubmitted,0,134};
    std::vector<Intent> sent;
    std::map<Id,SourceView> sources;
    std::map<Id,Result> fills;
    std::map<Id,Units> positions;
    std::int64_t NowMs() override { return now; }
    SourceView ReadSource(const Deal& d) override { return sources.at(d.position); }
    VolumeLimits Limits(const RuleConfig&,const std::string&,const std::string&,Side,bool) override {
        return {kUnitsPerLot/100,100*kUnitsPerLot,kUnitsPerLot/100};
    }
    EquitySnapshot Equities(const RuleConfig&) override { throw std::runtime_error("unexpected equity lookup"); }
    Units FollowerRemaining(Id,Id ticket,const std::string&,Side) override { return positions.at(ticket); }
    Submission Submit(const Intent& intent) override {
        const auto found=std::find_if(disk->saved.intents.begin(),disk->saved.intents.end(),
            [&](const Intent& i){return i.id==intent.id && i.state==IntentState::Prepared && i.units==intent.units;});
        Check(found!=disk->saved.intents.end(),"submission has durable Prepared evidence");
        sent.push_back(intent); return result;
    }
    Result Reconcile(const Intent& intent) override {
        const auto found=fills.find(intent.id); return found==fills.end()?Result{}:found->second;
    }
    void Log(const std::string&) override {}
};
struct Fixture {
    std::filesystem::path directory;
    CopierConfig config;
    FakePort port;
    std::unique_ptr<Disk> disk;
    std::unique_ptr<Engine> engine;
    explicit Fixture() {
        static unsigned serial=0;
        directory=std::filesystem::temp_directory_path()/(std::string(kPlatformName)+"-unsubmitted-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        std::filesystem::create_directory(directory);
        config.enabled=true; config.relationships.push_back({1,10,20,true,SizingMode::LotRatio,"1"});
        Start(1000,0);
    }
    ~Fixture() { engine.reset(); disk.reset(); std::error_code error; std::filesystem::remove_all(directory,error); }
    void Start(std::int64_t at,Id sequence) {
        engine.reset(); disk.reset(); disk=std::make_unique<Disk>(directory/"state.journal"); port.disk=disk.get();
        engine=std::make_unique<Engine>(config,"unsubmitted-refusal-test",port,*disk);
        engine->Start({at,sequence,{}});
    }
    Deal Opening(Id id=100) {
        Deal d; d.id=d.order=d.position=id; d.login=10; d.units=kUnitsPerLot;
        d.time_ms=port.now; d.symbol="EURUSD"; d.opening=true;
        port.sources[id]={ReadState::Ready,kUnitsPerLot,kUnitsPerLot,port.now,{}, {}};
        return d;
    }
    void Open(Id id=100) { engine->Admit(Opening(id),id); engine->Tick(); }
};
void SynchronousRefusal() {
    Fixture f; f.Open();
    Check(f.port.sent.size()==1,"one synchronous submission attempt");
    if constexpr (!mt4) {
        Check(f.engine->state().links.size()==1 && f.engine->state().links[0].phase==Phase::Review &&
            !f.disk->refusal,"MT5 never retires a synchronous MT4 refusal code");
        return;
    }
    Check(f.disk->refusal.has_value(),"MT4 non-submission refusal is saved before compaction");
    const auto durable=*f.disk->refusal;
    Check(durable.intents.size()==1 && !durable.intents[0].request_id &&
        SettledUnsubmittedNoMoneyOpening(durable.links[0],durable.intents[0]),"durable refusal has its own zero-request-ID proof");
    Check(!SettledNoMoneyOpening(durable.links[0],durable.intents[0]),"non-submission is not misrepresented as callback rejection");
    Check(f.engine->state().links.empty() && f.engine->state().intents.empty() && !f.engine->storage_degraded(),
        "real journal accepts compaction of a definitely unsubmitted MT4 opening");
    f.engine->Admit(f.Opening(),101); f.engine->Tick();
    Check(f.port.sent.size()==1,"duplicate source callback cannot retry the refused opening");
    for (Id id=101;id<125;++id) { f.port.now+=1000; f.Open(id); }
    Check(f.port.sent.size()==25 && f.engine->state().links.empty() && f.engine->state().intents.empty(),
        "routine zero-fill refusals do not retain unbounded mappings");
    const auto counter=f.engine->state().next_intent_id;
    f.port.now+=1000; f.Start(f.port.now,1000); f.engine->Tick();
    Check(f.port.sent.size()==25 && f.engine->state().next_intent_id==counter && f.engine->state().links.empty(),
        "journal replay preserves identity without resubmitting refusals");
    for (unsigned n=0;n<16;++n) {
        auto link=durable.links[0]; auto intent=durable.intents[0];
        switch (n) {
        case 0:intent.request_id=9;break; case 1:intent.close=true;break;
        case 2:intent.filled=1;break; case 3:intent.result_position=1;break;
        case 4:intent.deals.push_back(1);break; case 5:intent.follower_position=1;break;
        case 6:intent.follower_before=1;break; case 7:++intent.source_event;break;
        case 8:intent.retcode=10019;break; case 9:intent.state=IntentState::Review;break;
        case 10:link.follower_remaining=1;break; case 11:link.processed_closes.push_back(1);break;
        case 12:link.opening_rule.reset();break; case 13:link.phase=Phase::Review;break;
        case 14:link.reason="request_not_submitted";break; case 15:intent.symbol="OTHER";break;
        }
        Check(!SettledUnsubmittedNoMoneyOpening(link,intent),"uncertain, closing, mismatched or partial evidence cannot retire");
    }
}
void AmbiguousResultsRemain() {
    for (const auto result:std::vector<Submission>{{Submission::Status::Unknown,0,134},
        {Submission::Status::NotSubmitted,7,134},{Submission::Status::NotSubmitted,0,10019},
        {Submission::Status::NotSubmitted,0,131}}) {
        Fixture f; f.port.result=result; f.Open(); f.engine->Tick();
        Check(f.engine->state().links.size()==1 && f.engine->state().links[0].phase==Phase::Review &&
            f.engine->state().intents.size()==1 && f.port.sent.size()==1 && !f.disk->refusal,
            "unknown or noneligible synchronous result remains reviewable without retry");
    }
    Fixture close;
    close.port.result={Submission::Status::Queued,8,0}; close.Open();
    close.port.positions[900]=kUnitsPerLot;
    close.port.fills[1]={Result::Status::Filled,900,kUnitsPerLot,0,{900},"filled"};
    close.engine->ConfirmPending();
    close.port.now+=1000;
    close.port.sources[100].remaining=0;
    close.port.sources[100].closes.push_back({101,kUnitsPerLot,close.port.now});
    close.port.result={Submission::Status::NotSubmitted,0,134}; close.engine->Tick();
    Check(close.port.sent.size()==2 && close.port.sent.back().close && close.engine->state().links.size()==1 &&
        close.engine->state().links[0].phase==Phase::Review && close.port.positions[900]==kUnitsPerLot,
        "synchronous close refusal retains the actual open mapping");
}
}
int main() {
    try { SynchronousRefusal(); AmbiguousResultsRemain();
        std::cout<<kPlatformName<<" unsubmitted refusal checks: "<<checks<<" passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"FAILED: "<<error.what()<<'\n'; return 1; }
}

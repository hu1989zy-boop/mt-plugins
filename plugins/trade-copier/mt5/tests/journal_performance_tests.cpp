#include "tradecopier/journal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace tradecopier;
using namespace tradecopier::execution;

namespace {
unsigned checks=0;
void Require(bool condition,const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void Throws(F action,const char* message) {
    bool failed=false;
    try { action(); } catch (const std::exception&) { failed=true; }
    Require(failed,message);
}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("tradecopier-journal-performance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { std::filesystem::create_directories(path); }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path,error); }
};
State Snapshot(std::size_t count=16) {
    CopierConfig config{true,{{1,10,20,true,SizingMode::LotRatio,"1"}}};
    State state;
    state.identity="journal-performance-test";
    state.config_json=WriteConfigJson(config); state.revision=1;
    state.activation_ms=1000; state.activation_sequence=10;
    state.subscriptions.push_back({1,1000,10});
    for (std::size_t n=0;n<count;++n) {
        Link link;
        link.rule=1;
        link.source={1000+n,10,100+n,100+n,kUnitsPerLot,2000,"EURUSD",Side::Buy,true,false};
        link.phase=Phase::Active; link.source_initial=kUnitsPerLot;
        link.source_remaining=kUnitsPerLot; link.follower_remaining=kUnitsPerLot;
        link.follower_position=9001+n; link.follower_symbol="EURUSD";
        link.opening_rule=config.relationships[0]; state.links.push_back(link);
        Intent intent;
        intent.id=1+n; intent.link=n; intent.source_event=1000+n; intent.follower_login=20;
        intent.side=Side::Buy; intent.symbol="EURUSD"; intent.units=kUnitsPerLot;
        intent.created_ms=2001; intent.state=IntentState::Confirmed;
        intent.request_id=static_cast<std::uint32_t>(1001+n); intent.retcode=10009;
        intent.result_position=9001+n; intent.filled=kUnitsPerLot;
        intent.deals={70001+n}; state.intents.push_back(intent);
    }
    state.next_intent_id=count+1;
    return state;
}
constexpr std::array<std::uint64_t JournalTimings::*,8> kFields={
    &JournalTimings::total_ns,&JournalTimings::encode_ns,&JournalTimings::validation_ns,
    &JournalTimings::delta_frame_ns,&JournalTimings::write_ns,&JournalTimings::durable_flush_ns,
    &JournalTimings::checkpoint_ns,&JournalTimings::state_update_ns};
void Accumulate(JournalTimings& total,JournalTimings& maximum,const JournalCommitSample& sample) {
    for (const auto field:kFields) {
        total.*field+=sample.timings.*field;
        maximum.*field=std::max(maximum.*field,sample.timings.*field);
    }
    const auto& t=sample.timings;
    Require(t.total_ns>=t.encode_ns+t.validation_ns+t.delta_frame_ns+t.write_ns+t.durable_flush_ns+t.state_update_ns,
            "disjoint phase timings fit inside whole commit");
    Require(t.total_ns>=t.checkpoint_ns,"inclusive checkpoint fits inside whole commit");
}
void EqualTimings(const JournalTimings& actual,const JournalTimings& expected,const char* message) {
    for (const auto field:kFields) Require(actual.*field==expected.*field,message);
}
void ReplayEquals(const std::filesystem::path& path,const State& expected) {
    FileJournal journal(path); State restored;
    Require(journal.Load(restored),"nonempty journal replays");
    Require(EncodeState(restored)==EncodeState(expected),"replay exactly preserves execution state");
    Require(journal.performance().attempts==0 && journal.performance().write_calls==0,
            "load and replay are excluded from commit diagnostics");
}
void DiagnosticsAndDeltaReplay() {
    Directory dir;
    const auto path=dir.path/"delta.journal";
    auto state=Snapshot();
    JournalTimings total,maximum;
    {
        FileJournal journal(path); State empty;
        Require(!journal.Load(empty),"new journal empty");
        Require(journal.performance().attempts==0,"empty journal has no commits");
        journal.Save(state);
        auto stats=journal.performance();
        Require(stats.attempts==1 && stats.succeeded==1 && !stats.failed,"first successful commit counted");
        Require(stats.last.succeeded && !stats.last.checkpoint,"first save is a snapshot append");
        Require(stats.last.write_calls==2 && stats.last.durable_flush_calls==2,
                "first save reports lease and journal durable writes");
        Require(stats.last.payload_bytes==EncodeState(state).size() && stats.last.record_bytes==stats.last.payload_bytes+32,
                "snapshot payload/frame byte accounting");
        Accumulate(total,maximum,stats.last);
        for (unsigned n=0;n<8;++n) {
            ++state.revision;
            state.links[n].reason=std::string(n*3,'x');
            state.openings_paused=n%2!=0;
            journal.Save(state);
            stats=journal.performance();
            Require(stats.last.succeeded && !stats.last.checkpoint,"ordinary changes append");
            Require(stats.last.record_bytes<stats.last.payload_bytes,"sparse changes retain delta compression");
            Require(stats.last.write_calls==1 && stats.last.durable_flush_calls==1,
                    "each delta retains its durable flush");
            Accumulate(total,maximum,stats.last);
        }
        Require(stats.attempts==9 && stats.succeeded==9 && !stats.failed && !stats.checkpoints,
                "cumulative commit outcomes exact");
        Require(stats.write_calls==10 && stats.durable_flush_calls==10,"cumulative write/flush counts exact");
        EqualTimings(stats.total,total,"phase totals equal sum of completed samples");
        EqualTimings(stats.max,maximum,"phase maxima equal max completed sample");
        const auto before=journal.bytes();
        auto invalid=state; ++invalid.revision; invalid.identity="different-server";
        Throws([&]{journal.Save(invalid);},"identity transition still rejected");
        stats=journal.performance();
        Require(stats.attempts==10 && stats.failed==1 && stats.succeeded==9 && !stats.last.succeeded,
                "failed validation commit separately counted");
        Require(!stats.last.write_calls && !stats.last.durable_flush_calls && journal.bytes()==before,
                "validation failure cannot append or flush trading state");
        Accumulate(total,maximum,stats.last);
        EqualTimings(stats.total,total,"failed partial phases included in totals");
        EqualTimings(stats.max,maximum,"failed partial phases included in maxima");
    }
    ReplayEquals(path,state);
}
void CheckpointsAndFailure() {
    Directory dir;
    auto state=Snapshot();
    const auto forced=dir.path/"forced.journal";
    {
        FileJournal journal(forced); State empty;
        Require(!journal.Load(empty),"forced test journal empty"); journal.Save(state);
        ++state.revision; journal.Compact(state);
        const auto stats=journal.performance();
        Require(stats.attempts==2 && stats.succeeded==2 && stats.checkpoints==1 && journal.rotations()==1,
                "forced compact counted once");
        Require(stats.last.checkpoint && stats.last.record_bytes==EncodeState(state).size()+32,
                "checkpoint reports full snapshot bytes");
        Require(stats.last.write_calls==1 && stats.last.durable_flush_calls==1,
                "checkpoint replacement durably flushed before publication");
        Require(stats.last.timings.checkpoint_ns>=stats.last.timings.write_ns+stats.last.timings.durable_flush_ns,
                "checkpoint metric includes replacement write and durable flush");
        JournalTimings total,maximum; Accumulate(total,maximum,stats.last);
    }
    ReplayEquals(forced,state);

    const auto rotated=dir.path/"threshold.journal"; state=Snapshot();
    {
        FileJournal journal(rotated,1024); State empty;
        Require(!journal.Load(empty),"threshold test journal empty"); journal.Save(state);
        ++state.revision; journal.Save(state);
        Require(journal.performance().last.checkpoint && journal.performance().checkpoints==1,
                "size threshold still rotates small delta to full checkpoint");
    }
    ReplayEquals(rotated,state);

    const auto failed=dir.path/"failed-checkpoint.journal"; state=Snapshot();
    {
        FileJournal journal(failed); State empty;
        Require(!journal.Load(empty),"failed checkpoint test journal empty"); journal.Save(state);
        { std::ofstream leftover(failed.string()+".checkpoint.tmp",std::ios::binary); leftover<<"retained recovery evidence"; }
        auto next=state; ++next.revision;
        Throws([&]{journal.Compact(next);},"unexpected checkpoint file remains fatal");
        const auto stats=journal.performance();
        Require(stats.attempts==2 && stats.failed==1 && !stats.checkpoints && stats.last.checkpoint,
                "checkpoint attempt/failure not counted as successful checkpoint");
        Require(!stats.last.succeeded && !stats.last.write_calls && !stats.last.durable_flush_calls,
                "failed create never writes over checkpoint evidence");
    }
    ReplayEquals(failed,state);
}
void PruningValidation() {
    Directory dir;
    const auto path=dir.path/"prune.journal";
    auto state=Snapshot(2);
    state.links[0].phase=Phase::Closed;
    state.links[0].source_remaining=0; state.links[0].follower_remaining=0;
    auto pruned=state; ++pruned.revision;
    pruned.closed_sources.push_back({1,10,100,2000});
    pruned.links.erase(pruned.links.begin()); pruned.intents.erase(pruned.intents.begin());
    pruned.intents[0].link=0;
    {
        FileJournal journal(path); State empty;
        Require(!journal.Load(empty),"prune test journal empty"); journal.Save(state); journal.Prune(pruned);
        auto altered=pruned; ++altered.revision; ++altered.links[0].follower_position;
        Throws([&]{journal.Prune(altered);},"prune still rejects altered retained execution state");
        Require(journal.performance().failed==1,"prune validation failure diagnosed");
    }
    ReplayEquals(path,pruned);
}
void RecordLimitCheckpoint() {
    Directory dir;
    const auto path=dir.path/"record-limit.journal";
    auto state=Snapshot(1);
    {
        FileJournal journal(path); State empty;
        Require(!journal.Load(empty),"record limit test journal empty");
        for (std::size_t n=0;n<FileJournal::kReplayRecordLimit+1;++n) {
            if (n) ++state.revision;
            journal.Save(state);
            Require(journal.performance().last.checkpoint==(n==FileJournal::kReplayRecordLimit),
                    "unchanged record-count rotation boundary");
        }
        const auto stats=journal.performance();
        Require(stats.attempts==FileJournal::kReplayRecordLimit+1 && stats.checkpoints==1,
                "automatic count checkpoint recorded as successful commit");
        Require(stats.last.record_bytes==EncodeState(state).size()+32 && journal.rotations()==1,
                "count checkpoint publishes complete snapshot");
    }
    ReplayEquals(path,state);
}
}
int main() {
    try {
        DiagnosticsAndDeltaReplay(); CheckpointsAndFailure(); PruningValidation(); RecordLimitCheckpoint();
        std::cout<<checks<<" journal performance checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"journal performance test failure: "<<error.what()<<'\n';
        return 1;
    }
}

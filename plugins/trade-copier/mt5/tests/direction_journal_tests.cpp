#include "tradecopier/journal.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace tradecopier;
using namespace tradecopier::execution;

namespace {
unsigned checks=0;
void Require(bool value,const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void Throws(F action,const char* message) {
    bool threw=false;
    try { action(); } catch (const std::exception&) { threw=true; }
    Require(threw,message);
}
void Number(std::string& bytes,std::uint64_t value) {
    for (unsigned n=0;n<8;++n) bytes.push_back(static_cast<char>((value>>(n*8))&255));
}
void PutNumber(std::string& bytes,std::size_t offset,std::uint64_t value) {
    std::string encoded; Number(encoded,value); bytes.replace(offset,8,encoded);
}
std::uint32_t Crc(const std::string& bytes) {
    std::uint32_t crc=0xFFFFFFFFU;
    for (unsigned char byte:bytes) {
        crc^=byte;
        for (unsigned n=0;n<8;++n) crc=(crc>>1)^((crc&1)?0xEDB88320U:0U);
    }
    return crc^0xFFFFFFFFU;
}
std::string Frame(const std::string& payload,Id revision) {
    std::string bytes="TCXJ0001";
    Number(bytes,payload.size()); Number(bytes,revision); Number(bytes,Crc(payload));
    return bytes+payload;
}
void Write(const std::filesystem::path& path,const std::string& bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("test journal write failed");
}
struct TemporaryDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("tradecopier-direction-journal-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path,error); }
};
State Snapshot(CopyDirection direction,Side side=Side::Buy) {
    CopierConfig config{true,{{1,10,20,true,SizingMode::LotRatio,"1"}}};
    config.relationships[0].direction=direction;
    State state;
    state.identity="direction-test-server";
    state.config_json=WriteConfigJson(config); state.revision=1;
    state.activation_ms=1000; state.activation_sequence=10;
    state.subscriptions.push_back({1,1000,10});
    Link link;
    link.rule=1;
    link.source={1000,10,100,100,kUnitsPerLot,2000,"EURUSD",side,true,false};
    link.phase=Phase::Active; link.source_initial=kUnitsPerLot;
    link.source_remaining=kUnitsPerLot; link.follower_remaining=kUnitsPerLot;
    link.follower_position=9001; link.follower_symbol="EURUSD";
    link.opening_rule=config.relationships[0]; state.links.push_back(link);
    Intent intent;
    intent.id=1; intent.link=0; intent.source_event=1000; intent.follower_login=20;
    intent.side=FollowerSide(link); intent.symbol="EURUSD"; intent.units=kUnitsPerLot;
    intent.created_ms=2001; intent.state=IntentState::Confirmed; intent.request_id=1001;
    intent.retcode=10009; intent.result_position=9001; intent.filled=kUnitsPerLot;
    intent.deals={70001}; state.intents.push_back(intent); state.next_intent_id=2;
    return state;
}
std::string Schema6(const State& state) {
    auto bytes=EncodeState(state);
    std::size_t extension=8;
    for (const auto& link:state.links) if (link.opening_rule) extension+=8;
    bytes.resize(bytes.size()-extension); PutNumber(bytes,0,6);
    return bytes;
}
void Codec() {
    for (auto direction:{CopyDirection::Forward,CopyDirection::Reverse})
        for (auto side:{Side::Buy,Side::Sell}) {
            const auto state=Snapshot(direction,side);
            const auto bytes=EncodeState(state);
            Require(bytes[0]==7,"MT5 writes schema 7 even for forward-only state");
            const auto restored=DecodeState(bytes);
            Require(restored.links[0].opening_rule->direction==direction &&
                    restored.intents[0].side==FollowerSide(state.links[0]),
                    "frozen direction and actual follower side round trip");
            Require(EncodeState(restored)==bytes,"direction snapshot has stable canonical encoding");
        }
    auto old=Snapshot(CopyDirection::Forward);
    Link historical; historical.phase=Phase::Closed;
    old.links.push_back(historical);
    const auto legacy=DecodeState(Schema6(old));
    Require(legacy.links[0].opening_rule->direction==CopyDirection::Forward &&
            !legacy.links[1].opening_rule,"schema 6 defaults bound rules forward and preserves unbound closed history");
    Require(EncodeState(legacy)[0]==7,"legacy state upgrades on the next write");
    const auto restored=DecodeState(EncodeState(old));
    Require(restored.links.size()==2 && !restored.links[1].opening_rule,
            "direction extension permits unbound terminal links");

    auto frozen=Snapshot(CopyDirection::Reverse);
    CopierConfig next; std::string error;
    Require(ParseConfigJson(frozen.config_json,next,error),"test configuration parses");
    next.relationships[0].direction=CopyDirection::Forward;
    frozen.config_json=WriteConfigJson(next);
    Require(DecodeState(EncodeState(frozen)).links[0].opening_rule->direction==CopyDirection::Reverse,
            "restoration preserves opening direction after the current rule changes");

    auto invalid=Snapshot(CopyDirection::Reverse);
    invalid.links[0].opening_rule->direction=static_cast<CopyDirection>(2);
    Throws([&]{EncodeState(invalid);},"serializer refuses invalid frozen direction enum");
    const auto valid=EncodeState(Snapshot(CopyDirection::Reverse));
    auto corrupt=valid; PutNumber(corrupt,corrupt.size()-8,2);
    Throws([&]{DecodeState(corrupt);},"decoder refuses unknown frozen direction enum");
    corrupt=valid; PutNumber(corrupt,corrupt.size()-16,2);
    Throws([&]{DecodeState(corrupt);},"decoder refuses mismatched direction binding count");
    corrupt=valid; corrupt.resize(corrupt.size()-8);
    Throws([&]{DecodeState(corrupt);},"decoder refuses truncated direction binding");
    corrupt=valid; Number(corrupt,0);
    Throws([&]{DecodeState(corrupt);},"decoder refuses extra direction entries");
    corrupt=valid; PutNumber(corrupt,0,8);
    Throws([&]{DecodeState(corrupt);},"decoder refuses unsupported journal versions");
    invalid=Snapshot(CopyDirection::Reverse); invalid.intents[0].side=Side::Buy;
    Throws([&]{DecodeState(EncodeState(invalid));},"restored intent must agree with frozen follower side");
}
void Durability() {
    TemporaryDirectory dir;
    const auto legacy_path=dir.path/"legacy.journal";
    auto old=Snapshot(CopyDirection::Forward);
    Write(legacy_path,Frame(Schema6(old),1));
    {
        FileJournal journal(legacy_path); State restored;
        Require(journal.Load(restored),"legacy schema 6 journal replays");
        Require(restored.links[0].opening_rule->direction==CopyDirection::Forward,
                "legacy journal retains forward direction");
        ++restored.revision; journal.Save(restored);
    }
    {
        FileJournal journal(legacy_path); State restored;
        Require(journal.Load(restored) && restored.revision==2,
                "mixed schema 6/7 journal replays after upgrade");
    }

    const auto original=Snapshot(CopyDirection::Reverse);
    auto changed=original; ++changed.revision;
    changed.links[0].opening_rule->direction=CopyDirection::Forward;
    changed.intents[0].side=Side::Buy;
    {
        FileJournal journal(dir.path/"binding-change.journal"); State empty;
        Require(!journal.Load(empty),"new direction journal is empty"); journal.Save(original);
        Throws([&]{journal.Save(changed);},"durable transition refuses changing frozen direction and intent together");
    }
    const auto tampered_path=dir.path/"tampered-binding.journal";
    Write(tampered_path,Frame(EncodeState(original),1)+Frame(EncodeState(changed),2));
    {
        FileJournal journal(tampered_path); State restored;
        Throws([&]{journal.Load(restored);},"replay refuses changed frozen direction despite valid frame checksums");
    }
    auto downgraded=Snapshot(CopyDirection::Forward); downgraded.revision=2;
    const auto downgrade_path=dir.path/"downgrade.journal";
    Write(downgrade_path,Frame(EncodeState(old),1)+Frame(Schema6(downgraded),2));
    {
        FileJournal journal(downgrade_path); State restored;
        Throws([&]{journal.Load(restored);},"replay refuses schema 7 to 6 downgrade");
    }

    const auto pruned_path=dir.path/"pruned-reverse.journal";
    auto closed=original;
    closed.links[0].phase=Phase::Closed; closed.links[0].source_remaining=0;
    closed.links[0].follower_remaining=0;
    auto pruned=closed; ++pruned.revision;
    pruned.closed_sources.push_back({1,10,100,2000}); pruned.links.clear(); pruned.intents.clear();
    {
        FileJournal journal(pruned_path); State empty;
        Require(!journal.Load(empty),"new pruning journal is empty"); journal.Save(closed); journal.Prune(pruned);
        ++pruned.revision; journal.Save(pruned);
    }
    {
        FileJournal journal(pruned_path); State restored;
        Require(journal.Load(restored) && restored.revision==3 && restored.links.empty() &&
                restored.closed_sources.size()==1,"pruning last reverse link keeps schema monotonic and replays");
        Require(EncodeState(restored)[0]==7,"empty MT5 snapshots retain schema 7 after reverse history is pruned");
    }
}
}

int main() {
    try {
        Codec(); Durability();
        std::cout<<checks<<" direction journal checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"direction journal test failed: "<<error.what()<<'\n';
        return 1;
    }
}

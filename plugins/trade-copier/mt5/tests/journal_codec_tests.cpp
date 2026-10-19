#include "tradecopier/journal.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

using namespace tradecopier;
using namespace tradecopier::execution;
namespace {
unsigned checks=0;
void Require(bool value,const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
template<class F> void Throws(F action,const char* text) {
    bool failed=false; try { action(); } catch (const std::exception&) { failed=true; }
    Require(failed,text);
}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("tradecopier-journal-codec-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { std::filesystem::create_directories(path); }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path,error); }
};
void Number(std::string& s,std::uint64_t n) {
    for (unsigned b=0;b<8;++b) s.push_back(static_cast<char>((n>>(8*b))&255));
}
std::uint64_t NumberAt(const std::string& s,std::size_t offset) {
    Require(offset<=s.size() && s.size()-offset>=8,"test frame has complete number");
    std::uint64_t result=0;
    for (unsigned b=0;b<8;++b) result|=std::uint64_t(static_cast<unsigned char>(s[offset+b]))<<(8*b);
    return result;
}
void ReplaceNumber(std::string& s,std::size_t offset,std::uint64_t n) {
    std::string bytes; Number(bytes,n); s.replace(offset,bytes.size(),bytes);
}
// Independently retain the 0.7.3 byte-at-a-time codec as a wire-format oracle.
// Production slicing-by-8 tables must not provide their own expected checksum.
std::uint32_t ReferenceCrc(const std::string& s) {
    static const auto table=[] {
        std::array<std::uint32_t,256> result{};
        for (std::uint32_t n=0;n<256;++n) {
            auto value=n;
            for (unsigned b=0;b<8;++b) value=(value>>1)^((value&1)?0xEDB88320U:0U);
            result[n]=value;
        }
        return result;
    }();
    std::uint32_t crc=0xffffffffU;
    for (unsigned char c:s) crc=(crc>>8)^table[(crc^c)&255];
    return crc^0xffffffffU;
}
std::string ReferenceDelta(const std::string& before,const std::string& after,Id base,bool prune) {
    std::string changes;
    std::size_t patches=0,p=0;
    while (p<after.size()) {
        if (p<before.size() && before[p]==after[p]) { ++p; continue; }
        const auto start=p; auto end=++p;
        while (p<after.size()) {
            if (p>=before.size() || before[p]!=after[p]) end=p+1;
            ++p;
            if (p-end>=32) break;
        }
        Number(changes,start); Number(changes,end-start); changes.append(after,start,end-start); ++patches;
        if (changes.size()>=after.size()) return {};
    }
    std::string delta; Number(delta,prune); Number(delta,base); Number(delta,after.size());
    Number(delta,ReferenceCrc(after)); Number(delta,patches); delta+=changes;
    return delta.size()<after.size()?delta:std::string{};
}
std::string ReferenceFrame(const std::string& before,const std::string& after,Id revision,bool prune) {
    auto delta=before.empty()?std::string{}:ReferenceDelta(before,after,revision-1,prune);
    const auto& payload=delta.empty()?after:delta;
    std::string result=delta.empty()?(prune?"TCXJ0003":"TCXJ0001"):"TCXJ0004";
    Number(result,payload.size()); Number(result,revision); Number(result,ReferenceCrc(payload));
    result+=payload; return result;
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary); Require(bool(file),"test journal opens");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& path,const std::string& bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    Require(bool(file),"test journal written completely");
}
State Snapshot(std::size_t count) {
    CopierConfig config{true,{{1,10,20,true,SizingMode::LotRatio,"1"}}};
    State state;
    state.identity="journal-codec-test"; state.config_json=WriteConfigJson(config); state.revision=1;
    state.activation_ms=1000; state.activation_sequence=10; state.subscriptions.push_back({1,1000,10});
    for (std::size_t n=0;n<count;++n) {
        Link link;
        link.rule=1; link.source={1000+n,10,100+n,100+n,kUnitsPerLot,2000,"XAUUSDp",Side::Buy,true,false};
        link.phase=Phase::Closed; link.source_initial=kUnitsPerLot;
        link.follower_position=9001+n; link.follower_symbol="XAUUSDr";
        link.opening_rule=config.relationships[0]; link.reason=std::string(256,'\0');
        state.links.push_back(link);
        Intent intent;
        intent.id=1+n; intent.link=n; intent.source_event=1000+n; intent.follower_login=20;
        intent.side=Side::Buy; intent.symbol="XAUUSDr"; intent.units=kUnitsPerLot;
        intent.created_ms=2001; intent.state=IntentState::Confirmed;
        intent.request_id=static_cast<std::uint32_t>(1001+n); intent.retcode=10009;
        intent.result_position=9001+n; intent.filled=kUnitsPerLot; intent.deals={70001+n};
        state.intents.push_back(intent);
    }
    state.next_intent_id=count+1; return state;
}
void Replay(const std::filesystem::path& path,const State& expected) {
    FileJournal journal(path); State actual;
    Require(journal.Load(actual),"journal replay nonempty");
    Require(EncodeState(actual)==EncodeState(expected),"journal replay preserves exact state");
}
void RandomizedFramesAndPrunes() {
    Directory dir; const auto path=dir.path/"frames.journal";
    auto state=Snapshot(128);
    std::mt19937_64 random(0x07403264);
    std::string expected,before;
    std::uint64_t expected_flushes=0;
    {
        FileJournal journal(path); State empty; Require(!journal.Load(empty),"new codec journal empty");
        for (unsigned trial=0;trial<160;++trial) {
            bool prune=false;
            if (trial) {
                ++state.revision;
                if (trial%7==0) {
                    prune=true;
                    const auto removed=std::size_t{1}+static_cast<std::size_t>(random()%3);
                    for (std::size_t n=0;n<removed;++n) {
                        const auto& link=state.links[n];
                        state.closed_sources.push_back({link.rule,link.source.login,link.source.position,link.source.time_ms});
                    }
                    state.links.erase(state.links.begin(),state.links.begin()+removed);
                    state.intents.erase(state.intents.begin(),state.intents.begin()+removed);
                    for (auto& intent:state.intents) intent.link-=removed;
                } else {
                    auto& reason=state.links[static_cast<std::size_t>(random()%state.links.size())].reason;
                    switch (trial%6) {
                    case 0: reason.resize(static_cast<std::size_t>(random()%1024),'x'); break;
                    case 1: for (auto& c:reason) c=static_cast<char>(random()); break;
                    case 2: reason.insert(0,static_cast<std::size_t>(random()%65),'\0'); break;
                    case 3: reason.erase(0,static_cast<std::size_t>(random()%65)); break;
                    case 4:
                        for (std::size_t p=0;p<reason.size();p+=31+trial%4) reason[p]^=1;
                        break;
                    default: state.openings_paused=!state.openings_paused; break;
                    }
                }
            }
            const auto after=EncodeState(state);
            const auto frame=ReferenceFrame(before,after,state.revision,prune);
            if (prune) journal.Prune(state); else journal.Save(state);
            expected+=frame; before=after; expected_flushes+=trial?1:2;
            Require(journal.performance().last.record_bytes==frame.size(),"encoded frame size equals reference");
            Require(journal.performance().durable_flush_calls==expected_flushes,"optimization retains every durable flush");
            // The production codec must emit exactly the old codec's bytes, not
            // merely a frame that the new decoder happens to accept.
            if (trial%17==0 || trial==159) Require(Read(path)==expected,"new full/delta/prune bytes equal 0.7.3 oracle");
        }
    }
    Replay(path,state);
    const auto old=dir.path/"old-codec.journal"; Write(old,expected); Replay(old,state);
}
void CorruptionAndBoundaries() {
    Directory dir; const auto path=dir.path/"valid.journal";
    auto first=Snapshot(4),last=first; ++last.revision;
    // Exercise patches separated by exactly 31/32/33 and 63/64/65 bytes.
    for (const auto gap:{31U,32U,33U,63U,64U,65U}) {
        const auto index=gap%last.links.size();
        last.links[index].reason[0]='a'; last.links[index].reason[gap+1]='z';
    }
    {
        FileJournal journal(path); State empty; journal.Load(empty); journal.Save(first); journal.Save(last);
    }
    const auto bytes=Read(path);
    const auto first_payload=EncodeState(first),last_payload=EncodeState(last);
    Require(bytes==ReferenceFrame({},first_payload,1,false)+ReferenceFrame(first_payload,last_payload,2,false),
            "boundary deltas and CRCs byte identical");
    const auto offset=std::size_t{32}+first_payload.size();
    Require(bytes.substr(offset,8)=="TCXJ0004","corruption fixture contains delta");
    const auto delta_size=static_cast<std::size_t>(NumberAt(bytes,offset+8));
    Require(NumberAt(bytes,offset+32+32)>0,"corruption fixture contains patches");
    const auto corrupt=[&](unsigned kind) {
        auto broken=bytes;
        switch (kind) {
        case 0: broken[offset+32+24]^=1; break; // Reconstructed state CRC; outer CRC repaired below.
        case 1: ReplaceNumber(broken,offset+32+8,0); break; // Wrong base revision.
        case 2: ReplaceNumber(broken,offset+32+40,last_payload.size()+1); break; // Patch out of bounds.
        case 3: ReplaceNumber(broken,offset+32+48,0); break; // Empty patch.
        case 4: broken.back()^=1; break; // Raw frame checksum mismatch.
        default: broken.pop_back(); break; // Torn final write.
        }
        if (kind<4) ReplaceNumber(broken,offset+24,ReferenceCrc(broken.substr(offset+32,delta_size)));
        const auto bad=dir.path/("corrupt-"+std::to_string(kind)+".journal"); Write(bad,broken);
        Throws([&]{FileJournal journal(bad); State output; journal.Load(output);},"corrupt delta is refused, not silently replayed");
    };
    for (unsigned kind=0;kind<6;++kind) corrupt(kind);
    Replay(path,last);
}
}
int main() {
    try {
        Require(ReferenceCrc("123456789")==0xcbf43926U,"independent CRC oracle matches standard vector");
        RandomizedFramesAndPrunes(); CorruptionAndBoundaries();
        std::cout<<checks<<" journal codec checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr<<"journal codec test failure: "<<error.what()<<'\n'; return 1;
    }
}

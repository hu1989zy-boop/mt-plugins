// Wall-clock paced, real durable journal, asynchronous simulated fills. No MT5 API.
#include "tradecopier/executor.hpp"
#include "tradecopier/journal.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <thread>
using namespace tradecopier;
using namespace tradecopier::execution;
using Clock=std::chrono::steady_clock;
namespace {
void Check(bool ok,const char* what) { if (!ok) throw std::runtime_error(what); }
struct Disk:Store {
    FileJournal file;
    unsigned delay,writes=0;
    std::uint64_t peak=0;
    std::map<Id,std::pair<Intent,Deal>> prepared;
    explicit Disk(const std::filesystem::path& p,unsigned ms):file(p),delay(ms) {}
    bool Load(State& s) override { return file.Load(s); }
    void Record(const State& s) {
        ++writes; peak=std::max(peak,file.bytes()); 
        prepared.clear();
        for (const auto& i:s.intents) if (i.state==IntentState::Prepared) prepared.emplace(i.id,std::make_pair(i,s.links.at(i.link).source));
    }
    void Save(const State& s) override { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); file.Save(s); Record(s); }
    void Prune(const State& s) override { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); file.Prune(s); Record(s); }
    void Compact(const State& s) override { file.Compact(s); Record(s); }
    std::string OpeningBlockReason(const State& s,std::size_t n) override { return file.OpeningBlockReason(s,n); }
};
struct Sample { Intent intent; Deal source; std::int64_t submitted=0,fill=0,confirmed=0; Id position=0; bool applied=false; };
struct TimedPort:Port {
    Clock::time_point origin=Clock::now();
    Disk& disk;
    unsigned fill_ms,source_accounts=1,followers_per_source=10,rate=30,orders=1000,hold_ms=10000,read_us=0;
    bool bulk_close=false;
    std::uint64_t source_reads=0,target_reads=0;
    std::map<Id,Sample> samples;
    std::map<std::pair<std::int64_t,Id>,Id> due_fills;
    std::map<Id,Units> positions;
    std::map<std::pair<Id,Id>,unsigned> opens,closes;
    std::ofstream log;
    explicit TimedPort(Disk& d,unsigned fill,const std::filesystem::path& p):disk(d),fill_ms(fill),log(p) {}
    std::int64_t NowMs() override { return 1000000+std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-origin).count(); }
    std::int64_t OpenMs(unsigned n) const { return 1000100+std::int64_t(n/source_accounts)*1000/rate; }
    std::int64_t CloseMs(const Deal& d) const { return bulk_close?OpenMs(source_accounts*orders-1)+1000:d.time_ms+hold_ms; }
    void ReadCost() { if (read_us) std::this_thread::sleep_for(std::chrono::microseconds(read_us)); }
    SourceView ReadSource(const Deal& d) override {
        ++source_reads; ReadCost();
        SourceView s{ReadState::Ready,d.units,d.units,d.time_ms,{}, {}};
        if (NowMs()>=CloseMs(d)) { s.remaining=0; s.closes.push_back({100000+d.position,d.units,CloseMs(d)}); }
        return s;
    }
    VolumeLimits Limits(const RuleConfig&,const std::string&,const std::string&,Side,bool) override { return {kUnitsPerLot/100,100*kUnitsPerLot,kUnitsPerLot/100}; }
    EquitySnapshot Equities(const RuleConfig&) override { return {}; }
    void FillDue() {
        const auto now=NowMs();
        while (!due_fills.empty() && due_fills.begin()->first.first<=now) {
            auto& s=samples.at(due_fills.begin()->second);
            auto& volume=positions[s.position];
            if (s.intent.close) { Check(volume==s.intent.follower_before,"simulated close volume/order mismatch"); volume-=s.intent.units; }
            else { Check(!volume,"duplicate simulated target open"); volume=s.intent.units; }
            s.applied=true; due_fills.erase(due_fills.begin());
        }
    }
    Units FollowerRemaining(Id,Id p,const std::string&,Side) override { ++target_reads; ReadCost(); FillDue(); return positions[p]; }
    Submission Submit(const Intent& i) override {
        auto it=disk.prepared.find(i.id);
        Check(it!=disk.prepared.end() && it->second.first.state==IntentState::Prepared &&
            it->second.first.follower_login==i.follower_login && it->second.first.units==i.units &&
            it->second.first.symbol==i.symbol && it->second.first.follower_position==i.follower_position,
            "trade attempted without durable prepared evidence");
        auto source=it->second.second;
        const auto now=NowMs();
        const auto pos=i.close?i.follower_position:1000000+i.id;
        Check(samples.emplace(i.id,Sample{i,source,now,now+fill_ms,0,pos,false}).second,"duplicate request submission");
        due_fills.emplace(std::make_pair(now+fill_ms,i.id),i.id);
        const auto key=std::make_pair(i.follower_login,source.position);
        Check(++(i.close?closes:opens)[key]==1,"source copied twice to same account");
        return {Submission::Status::Queued,static_cast<std::uint32_t>(i.id),0};
    }
    Result Reconcile(const Intent& i) override {
        FillDue(); auto& s=samples.at(i.id);
        if (!s.applied) return {};
        if (!s.confirmed) s.confirmed=NowMs();
        return {Result::Status::Filled,s.position,i.units,10009,{2000000+i.id}};
    }
    void Log(const std::string& s) override { log<<NowMs()<<' '<<s<<'\n'; }
};
std::int64_t Percentile(std::vector<std::int64_t> v,unsigned p) {
    if (v.empty()) return 0;
    std::sort(v.begin(),v.end()); return v[(v.size()*p+99)/100-1];
}
void Stats(std::ostream& out,const std::vector<std::int64_t>& v) {
    out<<"{\"n\":"<<v.size()<<",\"p50\":"<<Percentile(v,50)<<",\"p95\":"<<Percentile(v,95)
       <<",\"p99\":"<<Percentile(v,99)<<",\"max\":"<<Percentile(v,100)<<",\"over_1000ms\":"<<std::count_if(v.begin(),v.end(),[](auto x){return x>1000;})<<"}";
}
}
int main(int argc,char** argv) {
    try {
        if (argc<2 || argc>11) throw std::runtime_error("Usage: tradecopier_stress_simulation output-directory [extra-flush-ms=0] [fill-ms=20] [sources=1] [followers-per-source=10] [rate-per-source=30] [orders-per-source=1000] [hold-ms=10000] [read-us=0] [bulk-close=0]");
        const std::filesystem::path dir=argv[1];
        Check(!std::filesystem::exists(dir),"output directory must be new (never reuse runtime journal)");
        std::filesystem::create_directories(dir);
        const auto path=dir/"simulation.journal";
        const unsigned delay=argc>=3?static_cast<unsigned>(std::stoul(argv[2])):0;
        const unsigned fill=argc>=4?static_cast<unsigned>(std::stoul(argv[3])):20;
        const auto number=[&](int index,unsigned fallback){return argc>index?static_cast<unsigned>(std::stoul(argv[index])):fallback;};
        const unsigned source_accounts=number(4,1),followers=number(5,10),rate=number(6,30),orders=number(7,1000),hold=number(8,10000),read_us=number(9,0);
        const bool bulk=number(10,0)!=0;
        Check(source_accounts && followers && rate && orders && hold && source_accounts<=100 && followers<=1000 &&
            std::uint64_t(source_accounts)*followers<=1000 && orders<=10000 && rate<=1000 && hold<=60000 && fill<=10000 && delay<=1000 && read_us<=100000,
            "invalid bounded simulation settings");
        const auto total_sources=source_accounts*orders,expected=total_sources*followers;
        CopierConfig config; config.enabled=true;
        for (unsigned source=0;source<source_accounts;++source) for (unsigned n=0;n<followers;++n) {
            const unsigned ordinal=source*followers+n;
            config.relationships.push_back({ordinal+1,50001+source,60000+ordinal,true,SizingMode::LotRatio,"1"});
        }
        State final; unsigned sends=0; bool passed=false;
        {
            Disk disk(path,delay); TimedPort port(disk,fill,dir/"executor.log");
            port.source_accounts=source_accounts; port.followers_per_source=followers; port.rate=rate; port.orders=orders;
            port.hold_ms=hold; port.read_us=read_us; port.bulk_close=bulk;
            Engine engine(config,"paced-simulation",port,disk,true); engine.Start({port.NowMs(),0,{}});
            struct Callback { std::int64_t time; Deal deal; bool opening; };
            std::vector<Callback> callbacks;
            for (unsigned n=0;n<total_sources;++n) {
                Deal d{10000+n,50001+n%source_accounts,1000+n,1000+n,kUnitsPerLot/100,
                    port.OpenMs(n),"XAUUSDp",Side::Buy,true,false};
                callbacks.push_back({d.time_ms,d,true}); callbacks.push_back({port.CloseMs(d),d,false});
            }
            std::stable_sort(callbacks.begin(),callbacks.end(),[](const auto& a,const auto& b){return a.time<b.time;});
            std::size_t consumed=0;
            unsigned admitted=0,peak_links=0,peak_pending=0,peak_account=0,peak_backlog=0;
            std::int64_t next_progress=port.NowMs()+5000;
            while (port.NowMs()<port.OpenMs(total_sources-1)+hold+60000) {
                engine.ConfirmPending(); // Same ordering as the server worker.
                std::vector<std::pair<Deal,Id>> events;
                const auto now=port.NowMs();
                unsigned due=0; while (due<total_sources && port.OpenMs(due)<=now) ++due;
                peak_backlog=std::max(peak_backlog,due-admitted);
                for (unsigned n=0;n<Engine::kAdmissionBatch && consumed<callbacks.size() && callbacks[consumed].time<=now;++n) {
                    const auto& event=callbacks[consumed++];
                    if (event.opening) { events.emplace_back(event.deal,consumed); ++admitted; }
                    else engine.NotifySourceClose(event.deal.login,event.deal.position);
                }
                engine.AdmitBatch(events,false); engine.Tick();
                peak_links=std::max(peak_links,static_cast<unsigned>(engine.state().links.size()));
                std::map<Id,unsigned> inflight; unsigned pending=0;
                for (const auto& i:engine.state().intents) if (i.state==IntentState::Submitted) { ++pending; ++inflight[i.follower_login]; }
                peak_pending=std::max(peak_pending,pending);
                for (auto e:inflight) { peak_account=std::max(peak_account,e.second); Check(e.second<=Engine::kAccountInFlight,"account concurrency exceeded"); }
                Check(pending<=Engine::kMaxInFlight,"global concurrency exceeded");
                Check(engine.state().halt_reason.empty(),engine.state().halt_reason.c_str());
                Check(!engine.state().openings_paused,"unexpected capacity pause");
                if (port.NowMs()>=next_progress) {
                    std::cout<<"elapsed_ms="<<port.NowMs()-1000000<<" admitted="<<admitted<<" requests="<<port.samples.size()
                        <<" live="<<engine.state().links.size()<<" pending="<<pending<<std::endl; next_progress+=5000;
                }
                if (admitted==total_sources && engine.state().links.empty() && !engine.HasPending()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            port.FillDue(); final=engine.state(); sends=static_cast<unsigned>(port.samples.size());
            std::vector<std::int64_t> open_latency,close_latency,confirm_latency,open_submit,close_submit;
            std::map<Id,std::pair<std::vector<std::int64_t>,std::vector<std::int64_t>>> account_latency;
            std::ofstream trades(dir/"trades.jsonl");
            for (const auto& e:port.samples) { const auto& s=e.second;
                const auto event=s.intent.close?port.CloseMs(s.source):s.source.time_ms;
                (s.intent.close?close_latency:open_latency).push_back(s.fill-event);
                (s.intent.close?close_submit:open_submit).push_back(s.submitted-event);
                auto& values=account_latency[s.intent.follower_login];
                (s.intent.close?values.second:values.first).push_back(s.fill-event);
                confirm_latency.push_back(s.confirmed?s.confirmed-s.fill:90000);
                trades<<"{\"intent\":"<<s.intent.id<<",\"account\":"<<s.intent.follower_login<<",\"source_position\":"<<s.source.position
                    <<",\"target_position\":"<<s.position<<",\"close\":"<<(s.intent.close?"true":"false")<<",\"source_event_ms\":"
                    <<event<<",\"submit_ms\":"<<s.submitted<<",\"fill_ms\":"<<s.fill<<",\"confirm_ms\":"<<s.confirmed<<"}\n";
            }
            passed=port.opens.size()==expected && port.closes.size()==expected && final.links.empty() && final.intents.empty() &&
                std::all_of(port.positions.begin(),port.positions.end(),[](const auto& p){return p.second==0;}) &&
                std::all_of(port.samples.begin(),port.samples.end(),[](const auto& e){return e.second.confirmed!=0;});
            std::ofstream summary(dir/"summary.json");
            summary<<"{\n\"passed\":"<<(passed?"true":"false")<<",\"source_accounts\":"<<source_accounts<<",\"followers\":"<<source_accounts*followers<<",\"followers_per_source\":"<<followers
                <<",\"source_positions\":"<<total_sources<<",\"rate_per_source\":"<<rate<<",\"hold_ms\":"<<hold<<",\"bulk_close\":"<<(bulk?"true":"false")
                <<",\"simulated_read_us\":"<<read_us<<",\"source_reads\":"<<port.source_reads<<",\"target_reads\":"<<port.target_reads<<","
                <<"\"fill_ms\":"<<fill<<",\"extra_flush_ms\":"<<delay<<",\"elapsed_ms\":"<<port.NowMs()-1000000<<",\"requests\":"<<sends
                <<",\"peak_links\":"<<peak_links<<",\"peak_pending\":"<<peak_pending<<",\"peak_account_pending\":"<<peak_account<<",\"peak_source_backlog\":"<<peak_backlog
                <<",\"durable_writes\":"<<disk.writes<<",\"encoded_bytes\":"<<disk.file.logical_bytes()<<",\"written_bytes\":"<<disk.file.written_bytes()<<",\"peak_file_bytes\":"<<disk.peak
                <<",\"final_file_bytes\":"<<disk.file.bytes()<<",\"checkpoints\":"<<disk.file.rotations()<<",\n\"open_latency_ms\":";
            Stats(summary,open_latency); summary<<",\"close_latency_ms\":"; Stats(summary,close_latency);
            summary<<",\"fill_to_confirmation_ms\":"; Stats(summary,confirm_latency); summary<<",\n\"open_submit_latency_ms\":"; Stats(summary,open_submit);
            summary<<",\n\"close_submit_latency_ms\":"; Stats(summary,close_submit); summary<<",\n\"accounts\":[";
            for (unsigned n=0;n<source_accounts*followers;++n) {
                if (n) summary<<',';
                const Id account=60000+n;
                auto count=[&](const auto& values){return std::count_if(values.begin(),values.end(),[&](const auto& v){return v.first.first==account;});};
                summary<<"{\"login\":"<<account<<",\"opens\":"<<count(port.opens)<<",\"closes\":"<<count(port.closes)<<",\"open_latency_ms\":";
                Stats(summary,account_latency[account].first); summary<<",\"close_latency_ms\":";
                Stats(summary,account_latency[account].second); summary<<"}";
            }
            summary<<"]\n}\n";
            std::cout<<"passed="<<passed<<" opens="<<port.opens.size()<<" closes="<<port.closes.size()<<" writes="<<disk.writes
                <<" checkpoints="<<disk.file.rotations()<<" open_p99_ms="<<Percentile(open_latency,99)<<" close_p99_ms="<<Percentile(close_latency,99)<<std::endl;
        }
        // Reopen and byte-compare the latest state, then start and prove no replay.
        { FileJournal file(path); State recovered; Check(file.Load(recovered),"simulation journal missing");
          Check(EncodeState(recovered)==EncodeState(final),"replayed journal differs from final live state"); }
        Check(passed && sends==expected*2,"stress did not achieve 100% opens/closes and flat targets");
        { Disk disk(path,0); TimedPort port(disk,20,dir/"restart.log");
          port.origin-=std::chrono::milliseconds(std::int64_t(orders)*1000/rate+hold+100000);
          Engine restarted(config,"paced-simulation",port,disk,true);
          restarted.Start({port.NowMs(),total_sources*2+1,{}}); restarted.Tick();
          Check(port.samples.empty() && restarted.state().links.empty() && restarted.state().halt_reason.empty() &&
              restarted.state().next_intent_id==expected*2+1,"restart sent duplicate trades or lost request identity"); }
        std::ofstream(dir/"recovery.txt")<<"Exact journal replay verified. Engine restarted with 0 new requests; next intent "<<expected*2+1<<".\n";
        std::cout<<"journal replay and restart without resubmission verified; simulation passed (not a Windows/MT5 benchmark)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAILED: "<<e.what()<<'\n'; return 1; }
}

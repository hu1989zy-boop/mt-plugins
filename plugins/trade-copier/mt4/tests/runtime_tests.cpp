#include "runtime.hpp"
#include "event_queue.hpp"
#include "deferred_admission.hpp"
#include "worker_wake.hpp"
#include "diagnostic_text.hpp"
#include <chrono>
#include <deque>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

namespace {
unsigned checks=0;
void Check(bool value,const char* message) {
    ++checks; if (!value) throw std::runtime_error(message);
}
template<class F> void Reject(F f,const char* message) {
    bool rejected=false;
    try { f(); } catch (const std::exception&) { rejected=true; }
    Check(rejected,message);
}
void Parameters() {
    auto values=mt4copier::DefaultParameters();
    const auto parsed=mt4copier::ParseParameters(values);
    Check(!parsed.armed && !parsed.management && parsed.app==3401 && parsed.build==1470 && parsed.dealer_login==0,
          "default disarmed identity and unconfigured dealer");
    values["Armed"]="1";
    Reject([&]{mt4copier::ParseParameters(values);},"armed configuration needs identity and paths");
    values["ExpectedServerName"]="Demo-1470";
    values["ConfigPath"]="D:\\MT4\\copier.json";
    values["JournalPath"]="D:\\MT4\\state.journal";
    Reject([&]{mt4copier::ParseParameters(values);},"armed plugin refuses placeholder dealer even with paths configured");
    values["DealerLogin"]="1234";
    const auto armed=mt4copier::ParseParameters(values);
    Check(armed.armed && armed.dealer_login==1234,"fully specified startup parsed");
    auto largest_dealer=values; largest_dealer["DealerLogin"]="2147483647";
    Check(mt4copier::ParseParameters(largest_dealer).dealer_login==2147483647u,"native signed login upper bound accepted");
    auto parked=values; parked["Armed"]="0"; parked["DealerLogin"]="0";
    Check(mt4copier::ParseParameters(parked).dealer_login==0,"disarmed deployment may retain placeholder dealer");
    for (const auto& invalid:{"0","-1","+1"," 1","1 ","1e3","2147483648","4294967295","18446744073709551616"}) {
        auto copy=values; copy["DealerLogin"]=invalid;
        Reject([&]{mt4copier::ParseParameters(copy);},"armed dealer must be a positive native signed integer");
    }
    parked["DealerLogin"]="2147483648";
    Reject([&]{mt4copier::ParseParameters(parked);},"disarmed dealer still respects native login range");
    auto missing_dealer=values; missing_dealer.erase("DealerLogin");
    Reject([&]{mt4copier::ParseParameters(missing_dealer);},"missing dealer parameter is incomplete");
    for (const auto& invalid:{"-1","+1"," 1","1 ","1e3","18446744073709551616"})
        Reject([&]{mt4copier::Unsigned(invalid);},"invalid or overflowing numeric text rejected");
    Check(mt4copier::Unsigned("18446744073709551615")==UINT64_MAX,"uint64 maximum is exact");
    for (const auto& name:{"Armed","ManagementEnabled"}) {
        auto copy=values; copy[name]="2";
        Reject([&]{mt4copier::ParseParameters(copy);},"boolean is strict");
    }
    for (const auto& item:std::vector<std::pair<std::string,std::string>>{
        {"ServerKey","0"},{"ApiAppId","4096"},{"ApiAppId","0"},{"ExpectedServerBuild","32768"},
        {"AllowedSymbol",""},{"Unexpected","1"},{"JournalPath",std::string(128,'x')}}) {
        auto copy=values; copy[item.first]=item.second;
        Reject([&]{mt4copier::ParseParameters(copy);},"invalid bounded identity or parameter rejected");
    }
    auto binary=values; binary["ConfigPath"]=std::string("a\0b",3);
    Reject([&]{mt4copier::ParseParameters(binary);},"embedded NUL rejected");
}
void DiagnosticText() {
    using mt4copier::DiagnosticText;
    Check(DiagnosticText("XAUUSDp")=="\"XAUUSDp\"","diagnostic values are quoted");
    const auto hostile=DiagnosticText("terminal\r\nMT4_REQUEST_APPLY request=999\t\x1b\x7f");
    Check(hostile.find('\r')==std::string::npos && hostile.find('\n')==std::string::npos &&
          hostile.find('\t')==std::string::npos && hostile.find('\x1b')==std::string::npos &&
          hostile.find('\x7f')==std::string::npos,"native request text cannot inject another log line or control sequence");
    Check(hostile.find("\\x0d\\x0a")!=std::string::npos,"diagnostics preserve escaped line endings");
    Check(DiagnosticText("a\"\\b")=="\"a\\\"\\\\b\"","quote and escape delimiters cannot masquerade as fields");
    Check(DiagnosticText(std::string("a\0b",3))=="\"a\\x00b\"","embedded NUL cannot hide diagnostic suffix");
    Check(DiagnosticText(u8"黄金")==u8"\"黄金\"","UTF-8 request data stays readable");
}
void Lifetime() {
    mt4copier::CallGate gate;
    Check(!gate.Enter(),"gate starts closed");
    gate.Open(); Check(gate.Enter(),"first call admitted");
    gate.Close(); Check(!gate.Enter(),"new calls refused after close");
    std::atomic<bool> drained{false};
    std::thread waiter([&]{gate.Drain(); drained.store(true);});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Check(!drained.load(),"cleanup cannot drain earlier live call");
    gate.Leave(); waiter.join(); Check(drained.load(),"cleanup completes after earlier call returns");
    gate.Open();
    try { mt4copier::GateUse use(gate); throw std::runtime_error("exception"); } catch (const std::runtime_error&) {}
    gate.Close(); gate.Drain(); Check(!gate.Enter(),"RAII returns gate count on exception");
}
void Queue() {
    tradecopier::execution::EventQueue<std::uint64_t,8> bounded;
    for (std::uint64_t n=0;n<8;++n) Check(bounded.Push(n),"bounded queue admission");
    Check(!bounded.Push(8),"overflow is explicit");
    std::uint64_t value=0,sequence=0;
    for (std::uint64_t n=0;n<8;++n) {
        Check(bounded.Pop(value,sequence) && value==n && sequence==n+1,"FIFO and activation watermark");
    }
    Check(!bounded.Pop(value,sequence),"empty queue");
    tradecopier::execution::EventQueue<std::uint64_t,4096> concurrent;
    std::vector<std::thread> producers;
    std::atomic<bool> overflow{false};
    for (std::uint64_t p=0;p<4;++p) producers.emplace_back([&,p]{
        for (std::uint64_t n=0;n<500;++n) if (!concurrent.Push(p*500+n)) overflow.store(true);
    });
    for (auto& thread:producers) thread.join();
    Check(!overflow.load() && concurrent.Watermark()==2000,"MPSC producers publish one event each");
    std::set<std::uint64_t> ids;
    while (concurrent.Pop(value,sequence)) {
        Check(ids.insert(value).second,"concurrent callback payload never duplicated");
        Check(sequence==ids.size(),"queue sequences are contiguous");
    }
    Check(ids.size()==2000,"all callback payloads copied");
}
void Wake() {
    tradecopier::execution::WorkerWake wake;
    std::atomic<bool> stopping{false};
    const auto token=wake.Snapshot(); wake.Signal();
    Check(wake.WaitSince(token,std::chrono::milliseconds(1),stopping),"signal during work cannot be lost before wait");
    stopping.store(true);
    Check(wake.WaitSince(wake.Snapshot(),std::chrono::milliseconds(1),stopping),"stop wakes worker");
}
struct AdmissionEvent { unsigned ticket=0; std::int64_t source_ms=0; bool opening=true; };
template<std::size_t Capacity> struct AdmissionHarness {
    using Queue=mt4copier::DeferredAdmissions<AdmissionEvent,Capacity>;
    using Item=typename Queue::Item;
    using Skip=typename Queue::Skip;
    Queue deferred;
    std::deque<Item> callbacks;
    std::set<unsigned> unstable;
    std::map<unsigned,unsigned> evidence,close_hints;
    std::vector<Item> attempts,admitted;
    std::vector<std::pair<Item,Skip>> skipped;
    std::int64_t now=1000;
    std::uint64_t baseline=0;
    void Add(unsigned ticket,std::uint64_t sequence,bool opening=true) {
        Item item; item.event={ticket,123456,opening}; item.sequence=sequence; callbacks.push_back(item);
    }
    void Run(std::size_t budget=32) {
        deferred.Process(budget,baseline,
            [&](AdmissionEvent& event,std::uint64_t& sequence) {
                if(callbacks.empty()) return false;
                event=callbacks.front().event; sequence=callbacks.front().sequence; callbacks.pop_front(); return true;
            },
            [&](const AdmissionEvent& event) {
                ++evidence[event.ticket]; ++close_hints[event.ticket]; return event.opening;
            },
            [&](const Item& item) {
                attempts.push_back(item);
                if(unstable.count(item.event.ticket)) return false;
                admitted.push_back(item); return true;
            },
            [&](const Item& item,Skip reason) { skipped.emplace_back(item,reason); },
            [&] { return now; });
    }
};
void DeferredAdmission() {
    AdmissionHarness<8> h;
    h.unstable.insert(10); h.Add(10,901); h.Add(11,902); h.Add(12,903,false); h.Run();
    Check(h.deferred.size()==1 && h.admitted.size()==1 && h.admitted.front().sequence==902,
          "incomplete source snapshot is retained while a later opening proceeds");
    Check(h.evidence[10]==1 && h.close_hints[10]==1 && h.evidence[12]==1 && h.attempts.size()==2,
          "fresh evidence and close hints run once, non-openings do not consume admission slots");
    h.now=2000; h.Run();
    Check(h.attempts.size()==3 && h.deferred.size()==1,"one unstable item is attempted only once per worker cycle");
    h.unstable.clear(); h.now=3000; h.Run();
    Check(h.deferred.size()==0 && h.admitted.size()==2 && h.admitted.back().sequence==901 &&
          h.admitted.back().event.ticket==10 && h.admitted.back().event.source_ms==123456,
          "successful retry preserves the original event, source time and callback sequence");
    Check(h.admitted.back().first_attempt_ms==1000 && h.evidence[10]==1 && h.close_hints[10]==1,
          "retry neither extends the deadline nor repeats evidence and close hints");
    h.Run(); Check(h.admitted.size()==2,"completed retry is removed and cannot admit twice");

    AdmissionHarness<2> expired;
    expired.unstable.insert(1); expired.Add(1,100); expired.Run();
    expired.now=20000; expired.Run(); expired.now=30999; expired.Run();
    Check(expired.attempts.size()==3 && expired.deferred.size()==1,"retry allowed strictly before the original thirty-second deadline");
    expired.now=31000; expired.Run();
    Check(expired.attempts.size()==3 && expired.deferred.size()==0 && expired.skipped.size()==1 &&
          expired.skipped.front().second==AdmissionHarness<2>::Skip::Expired && expired.skipped.front().first.sequence==100,
          "thirty-second expiry skips explicitly without another SDK lookup");
    expired.unstable.clear(); expired.Run(); Check(expired.admitted.empty(),"expired source is never caught up later");

    AdmissionHarness<2> barrier;
    barrier.unstable.insert(1); barrier.Add(1,100); barrier.Run();
    barrier.baseline=100; barrier.unstable.clear(); barrier.Run();
    Check(barrier.admitted.empty() && barrier.deferred.size()==0 && barrier.attempts.size()==1 &&
          barrier.skipped.front().second==AdmissionHarness<2>::Skip::BeforeBaseline,
          "resume watermark fences an already deferred source instead of regenerating its sequence");
    barrier.Add(2,101); barrier.Run();
    Check(barrier.admitted.size()==1 && barrier.admitted.front().sequence==101,"post-baseline callback remains eligible");
    AdmissionHarness<2> rewind;
    rewind.unstable.insert(1); rewind.Add(1,100); rewind.Run(); rewind.now=999; rewind.Run();
    Check(rewind.deferred.size()==0 && rewind.skipped.front().second==AdmissionHarness<2>::Skip::Expired,
          "regressed monotonic clock fails closed");

    AdmissionHarness<2> bounded;
    bounded.unstable={1,2,3}; bounded.Add(1,201); bounded.Add(2,202); bounded.Add(3,203); bounded.Run();
    Check(bounded.deferred.size()==2 && bounded.skipped.size()==1 &&
          bounded.skipped.front().second==AdmissionHarness<2>::Skip::CapacityReached &&
          bounded.skipped.front().first.sequence==203,"full defer queue explicitly skips the new item without replacing older items");
    bounded.unstable.clear(); bounded.Run();
    Check(bounded.admitted.size()==2 && bounded.admitted[0].sequence==201 && bounded.admitted[1].sequence==202,
          "full queue retains earlier original identities and drains in order");
    bounded.Add(4,204); bounded.unstable.insert(4); bounded.Run(); bounded.unstable.clear(); bounded.Run();
    Check(bounded.admitted.back().sequence==204,"defer ring remains valid after capacity wraparound");

    AdmissionHarness<8> fair;
    for(unsigned n=1;n<=4;++n) { fair.unstable.insert(n); fair.Add(n,300+n); }
    fair.Run(8); fair.attempts.clear();
    for(unsigned n=5;n<=8;++n) fair.Add(n,300+n);
    fair.Run(4);
    Check(fair.attempts.size()==4 && fair.admitted.size()==2 && fair.callbacks.size()==2,
          "unstable backlog preserves half the budget for fresh callback progress");
    Check(fair.attempts[0].event.ticket==1 && fair.attempts[1].event.ticket==2 &&
          fair.attempts[2].event.ticket==5 && fair.attempts[3].event.ticket==6,"retry and fresh work share a single bounded budget");
    fair.attempts.clear(); fair.Run(4);
    Check(fair.attempts[0].event.ticket==3 && fair.attempts[1].event.ticket==4 && fair.admitted.size()==4,
          "persistent failures rotate and do not block other deferred or fresh events");
    fair.attempts.clear(); fair.Run(4);
    std::set<unsigned> retried;
    for(const auto& item:fair.attempts) retried.insert(item.event.ticket);
    Check(fair.attempts.size()==4 && retried.size()==4,"spare fresh budget retries untouched old items only once each");
    for(unsigned n=1;n<=8;++n)
        Check(fair.evidence[n]==1 && fair.close_hints[n]==1,"retry rotation never repeats one-shot callback effects");

    AdmissionHarness<32> batch;
    for(unsigned n=1;n<=16;++n) { batch.unstable.insert(n); batch.Add(n,400+n); }
    batch.Run(); batch.unstable.clear(); batch.attempts.clear();
    for(unsigned n=17;n<=48;++n) batch.Add(n,400+n);
    batch.Run();
    Check(batch.attempts.size()==32 && batch.admitted.size()==32 && batch.callbacks.size()==16 && batch.deferred.size()==0,
          "successful deferred plus fresh openings never exceed the engine admission batch of thirty-two");
    batch.Run(); Check(batch.admitted.size()==48,"remaining fresh callbacks progress next cycle without duplication");
    const auto before=batch.evidence.size(); batch.Add(49,449); batch.Run(0);
    Check(batch.evidence.size()==before && batch.callbacks.size()==1,"zero budget consumes no callbacks");
}
}
int main() {
    try { Parameters(); DiagnosticText(); Lifetime(); Queue(); Wake(); DeferredAdmission(); std::cout<<"MT4 runtime: "<<checks<<" checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

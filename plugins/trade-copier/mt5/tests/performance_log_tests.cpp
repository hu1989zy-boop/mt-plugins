#include "performance_log.hpp"
#include <iostream>
#include <stdexcept>
using namespace tradecopier::execution;
namespace {
unsigned checks=0;
void Check(bool ok,const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
}
int main() {
    try {
        PerformanceLog log; log.Reset(100);
        ExecutionPerformance e; JournalPerformance j; HistoryMetrics h;
        e.confirmation_passes=10; e.confirmation_reuses=9; e.confirmation_save_us=500;
        e.prune_commits=5; e.prune_expiry_only_commits=2;
        e.prune_closed_sources_removed=30; e.prune_expiry_deferred_passes=18;
        j.attempts=2; j.total.encode_ns=120000; j.max.encode_ns=80000;
        j.total.durable_flush_ns=250000; j.max.durable_flush_ns=150000;
        h.deal_queries=4; h.cached_windows=12;
        Check(log.Poll(5099,e,j,h,8,9,1000).empty(),"no logging before interval");
        auto lines=log.Poll(5100,e,j,h,8,9,1000,1100,9000);
        Check(lines.size()==3,"one bounded aggregate group on activity");
        Check(lines[0].find("confirm_reuses=9")!=std::string::npos,"engine activity included");
        Check(lines[0].find("prune_commits=5 prune_expiry_only_commits=2 prune_closed_sources_removed=30 prune_expiry_deferred_passes=18")!=std::string::npos,
            "prune commits, removed keys and deferred calls are distinct diagnostics");
        Check(lines[1].find("encode_us=120 encode_max_us_total=80")!=std::string::npos,"nanosecond totals converted to microseconds");
        Check(lines[1].find("flush_us=250 flush_max_us_total=150")!=std::string::npos,"flush measurements separately visible");
        Check(lines[1].find("committed_frame_bytes=1100 logical_snapshot_bytes=9000")!=std::string::npos,
            "successful frame bytes and uncompressed snapshot bytes separately reported");
        for (const auto& line:lines) Check(line.size()<4096,"aggregate record fits port log bound");
        Check(log.Poll(10100,e,j,h,8,9,1000,1100,9000).empty(),"unchanged idle state stays quiet");
        j.attempts=3; j.total.encode_ns=135000; h.deal_queries=5;
        e.prune_commits=6; e.prune_expiry_deferred_passes=20;
        lines=log.Poll(15100,e,j,h,8,9,1000,1300,12000);
        Check(lines[1].find("attempts=1")!=std::string::npos && lines[1].find("encode_us=15 ")!=std::string::npos,
            "interval counters exclude preceding intervals");
        Check(lines[2].find("deal_queries=1")!=std::string::npos,"history delta excludes preceding intervals");
        Check(lines[0].find("prune_commits=1 prune_expiry_only_commits=0 prune_closed_sources_removed=0 prune_expiry_deferred_passes=2")!=std::string::npos,
            "prune counters are interval deltas, not cumulative counts");
        Check(lines[1].find("committed_frame_bytes=200 logical_snapshot_bytes=3000")!=std::string::npos,
            "byte counters exclude preceding intervals including quiet samples");
        log.Reset(20000); e={}; j={}; h={}; j.attempts=1;
        lines=log.Poll(25000,e,j,h,0,0,1,32,96);
        Check(lines.size()==3 && lines[1].find("attempts=1")!=std::string::npos,"new plugin lifetime resets metric baselines");
        Check(lines[1].find("committed_frame_bytes=32 logical_snapshot_bytes=96")!=std::string::npos,
            "new lifetime resets byte counters");
        Check(log.Poll(1,e,j,h,0,0,1).empty(),"clock rollback rebases without unsigned interval");
        std::cout<<"performance log tests passed: "<<checks<<" checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

#include "history_index.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

using tradecopier::execution::HistoryIndex;
namespace {
unsigned checks=0;
void Check(bool condition,const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
HistoryIndex Build(const std::vector<HistoryIndex::Keys>& rows) {
    return HistoryIndex::Build(static_cast<std::uint32_t>(rows.size()),
        [&](std::uint32_t n) { return rows[n]; });
}
void DuplicatesAndNulls() {
    const auto index=Build({{true,21,90},{true,22,90},{true,21,91},
                            {true,21,90},{false,21,90},{true,0,0},{}});
    Check(index.Primary(21)==HistoryIndex::Rows({0,2,3}),"duplicate order/intent evidence must survive in original order");
    Check(index.Secondary(90)==HistoryIndex::Rows({0,1,3}),"position index includes every entry/exit row");
    Check(index.Primary(0)==HistoryIndex::Rows({5}),"malformed zero key evidence must not silently disappear");
    Check(index.Secondary(0)==HistoryIndex::Rows({5}),"zero position evidence is retained");
    Check(index.Primary(999).empty() && index.Secondary(999).empty(),"missing keys are empty");
    Check(index.HasNullRows(),"even a null row outside the selected key taints the snapshot");
}
void ReplacementAndFailedBuild() {
    auto snapshot=Build({{true,100,500}});
    auto wider=Build({{true,99,499},{true,100,500},{true,100,500}});
    snapshot=std::move(wider);
    Check(snapshot.Primary(100)==HistoryIndex::Rows({1,2}),"widened window must use new row numbers, not append old indices");
    Check(snapshot.Primary(99)==HistoryIndex::Rows({0}),"expanded window includes older evidence");
    try {
        snapshot=HistoryIndex::Build(3,[](std::uint32_t n) -> HistoryIndex::Keys {
            if(n==1) throw std::runtime_error("simulated SDK metadata read failure");
            return {true,123,456};
        });
        throw std::runtime_error("expected reader failure");
    } catch(const std::runtime_error& e) {
        Check(std::string(e.what())=="simulated SDK metadata read failure","metadata read failures must propagate");
    }
    Check(snapshot.Primary(100)==HistoryIndex::Rows({1,2}) && snapshot.Primary(123).empty(),
          "failed expansion cannot publish a partial index");
    snapshot=HistoryIndex{};
    Check(snapshot.Primary(100).empty() && snapshot.Secondary(500).empty() && !snapshot.HasNullRows(),
          "next cycle cannot retain prior-row or null-row state");
    snapshot=Build({{true,100,500}});
    Check(snapshot.Primary(100)==HistoryIndex::Rows({0}),"new cycle rebuild reflects refreshed snapshot only");
}
void FilteredOrders() {
    struct Row { std::uint64_t marker,server,intent; };
    const std::vector<Row> rows{{8,1,10},{9,1,10},{8,2,10},{8,1,11},{8,1,10}};
    const auto index=HistoryIndex::Build(static_cast<std::uint32_t>(rows.size()),[&](std::uint32_t n) -> HistoryIndex::Keys {
        const auto& row=rows[n];
        if(row.marker!=8) return {};
        return {true,row.intent,{}};
    });
    Check(index.Primary(10)==HistoryIndex::Rows({0,2,4}),"candidate index retains same-intent rows for final server validation");
    HistoryIndex::Rows verified;
    for (const auto n:index.Primary(10))
        if (rows[n].marker==8 && rows[n].intent==10 && rows[n].server==1) verified.push_back(n);
    Check(verified==HistoryIndex::Rows({0,4}),"final server check excludes foreign server and retains exact-identity duplicates");
    Check(index.Primary(11)==HistoryIndex::Rows({3}),"intent groups stay separate");
}
void CompareFullScan() {
    // A dense account history with repeated keys, duplicate evidence and
    // unrelated rows. Every lookup must yield the exact old full-scan row set.
    constexpr std::uint32_t count=100000;
    std::vector<HistoryIndex::Keys> rows;
    rows.reserve(count);
    for(std::uint32_t n=0;n<count;++n) rows.push_back({true,n%1999,(n*17ULL)%701});
    const auto index=Build(rows);
    std::uint64_t candidates=0;
    for(std::uint64_t key=0;key!=2001;++key) {
        HistoryIndex::Rows expected;
        for(std::uint32_t n=0;n<count;++n) if(rows[n].primary==key) expected.push_back(n);
        Check(index.Primary(key)==expected,"intent/order lookup differs from full scan");
        candidates+=index.Primary(key).size();
    }
    Check(candidates==count,"large-history lookups visit each indexed evidence row once rather than per-intent full scans");
    for(std::uint64_t key=0;key!=703;++key) {
        HistoryIndex::Rows expected;
        for(std::uint32_t n=0;n<count;++n) if(rows[n].secondary==key) expected.push_back(n);
        Check(index.Secondary(key)==expected,"position lookup differs from full scan");
    }
    Check(!index.HasNullRows(),"dense valid history must remain valid");
}
}
int main() {
    try {
        DuplicatesAndNulls(); ReplacementAndFailedBuild(); FilteredOrders(); CompareFullScan();
        std::cout<<"history index tests passed: "<<checks<<" checks\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"history index tests failed: "<<e.what()<<'\n'; return 1;
    }
}

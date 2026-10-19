#pragma once
#include "tradecopier/executor.hpp"
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>

namespace mt4copier {
using tradecopier::Id;
using tradecopier::Units;
using tradecopier::execution::Side;
constexpr Units kUnitsPerMt4Volume = tradecopier::kUnitsPerLot / 100;
// A bounded SDK snapshot raced a trade or has not published all lineage rows.
// Source reads may wait for the engine's existing timeout; never treat as zero.
class IncompleteSnapshot : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Exact SDK values: TS_OPEN_NORMAL=0, TS_OPEN_REMAND=1, TS_OPEN_RESTORED=2,
// TS_CLOSED_NORMAL=3, TS_CLOSED_PART=4, TS_CLOSED_BY=5, TS_DELETED=6.
struct Order {
    Id ticket=0, login=0;
    std::string symbol, comment;
    Side side=Side::Buy;
    std::int32_t volume=0;
    std::int64_t open_ms=0, close_ms=0;
    double open_price=0;
    unsigned digits=0;
    int state=0, reason=0;
};
struct Account {
    std::string currency;
    std::uint64_t equity=0;
    unsigned currency_digits=2;
    std::int64_t query_ms=0;
    bool enabled=false;
    // Nonzero MT4 close_reopen is unsupported: its closed record reports a
    // full close plus a new opening, not the requested partial-close volume.
    bool close_reopen=false;
};
struct Symbol {
    tradecopier::SymbolSpec spec;
    tradecopier::VolumeLimits limits;
    bool market_execution=false;
    bool close_allowed=false; // independent of opening restrictions/long_only
};
struct Request {
    Id intent=0, login=0, ticket=0; // ticket is the actual current remainder
    bool close=false;
    Side position_side=Side::Buy; // MT4 CLOSE cmd is the position's direction
    std::string symbol, token;
    std::int32_t volume=0;
    std::int32_t expected_remaining=0; // CLOSE: fresh SDK order must equal this
};
struct RequestEvidence {
    enum class Kind { Filled, Rejected } kind=Kind::Filled;
    std::uint32_t request_id=0, retcode=0;
    Id login=0, ticket=0;
    bool close=false;
    Side position_side=Side::Buy;
    std::string symbol, token;
    std::int32_t volume=0;
    std::int64_t time_ms=0;
    // Filled is allowed only for a result callback carrying this exact
    // RequestInfo/token, never RequestApply/queue acceptance alone. Rejected
    // requires authoritative final zero-fill evidence, not absent history.
};
class Broker {
public:
    virtual ~Broker()=default;
    virtual std::int64_t NowMs()=0;
    // Complete BUY/SELL records for this account whose opening OR closing
    // intersects the interval, PLUS every current open BUY/SELL order. Throw
    // on incomplete/failed query. Port requests from=0 to reconstruct lineage.
    virtual std::vector<Order> Orders(Id login,std::int64_t from_ms,std::int64_t to_ms)=0;
    virtual Account AccountInfo(Id login)=0;
    virtual std::vector<Symbol> Symbols(Id login)=0;
    // Submit TT_ORDER_MK_OPEN/CLOSE via RequestsAdd, then have the configured
    // dealer lock/confirm only that exact accepted request. No database writes
    // or client permission changes. Return acceptance, never claim a fill here.
    virtual tradecopier::execution::Submission Submit(const Request&)=0;
    virtual void Log(const std::string&)=0;
};
}

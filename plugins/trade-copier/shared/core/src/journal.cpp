#include "tradecopier/journal.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tradecopier::execution {
namespace {
constexpr std::size_t kMaxSnapshot = FileJournal::kMaxSnapshotBytes;
constexpr std::uint64_t kMaxJournal = 256ULL * 1024 * 1024;
constexpr char kMagic[] = "TCXJ0001";
constexpr char kCheckpointMagic[] = "TCXJ0002";
constexpr char kPruneMagic[] = "TCXJ0003";
constexpr char kDeltaMagic[] = "TCXJ0004";
// MT5 freezes the per-relationship direction in schema 7. MT4 remains on its
// existing schema because that platform currently supports forward copying only.
constexpr Id kCurrentSchema = std::string_view(kPlatformName)=="mt5" ? 7 : 6;
using JournalClock=std::chrono::steady_clock;
std::uint64_t SaturatingAdd(std::uint64_t a,std::uint64_t b) noexcept {
    return b>std::numeric_limits<std::uint64_t>::max()-a?std::numeric_limits<std::uint64_t>::max():a+b;
}
std::uint64_t ElapsedNs(JournalClock::time_point start) noexcept {
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(JournalClock::now()-start).count();
    return elapsed>0?static_cast<std::uint64_t>(elapsed):0;
}
class PhaseTimer {
    std::uint64_t* total_;
    JournalClock::time_point start_;
public:
    explicit PhaseTimer(std::uint64_t* total) noexcept
        :total_(total),start_(total?JournalClock::now():JournalClock::time_point{}) {}
    explicit PhaseTimer(std::uint64_t& total) noexcept :PhaseTimer(&total) {}
    ~PhaseTimer() { if (total_) *total_=SaturatingAdd(*total_,ElapsedNs(start_)); }
};
class CommitTimer {
    JournalPerformance& stats_;
    JournalClock::time_point start_=JournalClock::now();
public:
    JournalCommitSample sample;
    explicit CommitTimer(JournalPerformance& stats) noexcept :stats_(stats) {}
    ~CommitTimer() {
        sample.timings.total_ns=ElapsedNs(start_);
        stats_.attempts=SaturatingAdd(stats_.attempts,1);
        auto& outcome=sample.succeeded?stats_.succeeded:stats_.failed;
        outcome=SaturatingAdd(outcome,1);
        if (sample.succeeded && sample.checkpoint) stats_.checkpoints=SaturatingAdd(stats_.checkpoints,1);
        stats_.write_calls=SaturatingAdd(stats_.write_calls,sample.write_calls);
        stats_.durable_flush_calls=SaturatingAdd(stats_.durable_flush_calls,sample.durable_flush_calls);
        constexpr std::array<std::uint64_t JournalTimings::*,8> fields={
            &JournalTimings::total_ns,&JournalTimings::encode_ns,&JournalTimings::validation_ns,
            &JournalTimings::delta_frame_ns,&JournalTimings::write_ns,&JournalTimings::durable_flush_ns,
            &JournalTimings::checkpoint_ns,&JournalTimings::state_update_ns};
        for (const auto field:fields) {
            stats_.total.*field=SaturatingAdd(stats_.total.*field,sample.timings.*field);
            stats_.max.*field=std::max(stats_.max.*field,sample.timings.*field);
        }
        stats_.last=sample;
    }
};
[[noreturn]] void Fail(const char* text) { throw std::runtime_error(text); }
#ifdef _WIN32
[[noreturn]] void FailWindows(const char* operation, DWORD error=GetLastError()) {
    throw std::runtime_error(std::string(operation)+" (Windows "+std::to_string(error)+")");
}
#endif
void U64(std::string& s, std::uint64_t v) {
    char bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i]=static_cast<char>((v >> (i * 8)) & 255);
    s.append(bytes,sizeof bytes);
}
void Text(std::string& s, const std::string& text) { U64(s, text.size()); s += text; }
void Ids(std::string& s, const std::vector<Id>& ids) {
    U64(s, ids.size()); for (Id id : ids) U64(s, id);
}
struct Reader {
    const std::string& s;
    std::size_t p = 0;
    std::uint64_t Number() {
        if (p > s.size() || s.size() - p < 8) Fail("truncated journal payload");
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(static_cast<unsigned char>(s[p++])) << (i * 8);
        return value;
    }
    std::size_t Count(std::size_t maximum) {
        const auto n = Number(); if (n > maximum) Fail("journal field limit exceeded");
        return static_cast<std::size_t>(n);
    }
    std::int64_t Time() {
        const auto value = Number();
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            Fail("invalid journal timestamp");
        return static_cast<std::int64_t>(value);
    }
    std::string String(std::size_t maximum = 1024 * 1024) {
        auto n = Count(maximum);
        if (n > s.size() - p) Fail("truncated journal string");
        const auto text = s.substr(p, n); p += n; return text;
    }
    std::vector<Id> IdList() {
        const auto n = Count(100000);
        std::vector<Id> result; result.reserve(n);
        for (std::size_t i = 0; i < n; ++i) result.push_back(Number());
        return result;
    }
    template<typename T> T Enum(unsigned maximum) { return static_cast<T>(Count(maximum)); }
};
std::uint32_t Crc(const std::string& s) {
    static const auto table=[] {
        std::array<std::array<std::uint32_t,256>,8> values{};
        for (std::uint32_t n=0;n<values[0].size();++n) {
            auto value=n;
            for (unsigned bit=0;bit<8;++bit) value=(value>>1)^((value&1)?0xEDB88320U:0U);
            values[0][n]=value;
        }
        for (std::size_t slice=1;slice<values.size();++slice)
            for (std::size_t n=0;n<values[slice].size();++n) {
                const auto value=values[slice-1][n];
                values[slice][n]=(value>>8)^values[0][value&255];
            }
        return values;
    }();
    // The same IEEE CRC32 used by every existing frame. Eight independent
    // lookups shorten the byte-at-a-time dependency chain on large snapshots.
    // Explicit byte assembly is alignment- and endian-independent (no casts
    // to wider pointers, platform intrinsics or different CRC polynomial).
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto* bytes=reinterpret_cast<const unsigned char*>(s.data());
    std::size_t p=0;
    for (;s.size()-p>=8;p+=8) {
        crc^=std::uint32_t(bytes[p]) | (std::uint32_t(bytes[p+1])<<8) |
            (std::uint32_t(bytes[p+2])<<16) | (std::uint32_t(bytes[p+3])<<24);
        crc=table[7][crc&255]^table[6][(crc>>8)&255]^table[5][(crc>>16)&255]^table[4][crc>>24]^
            table[3][bytes[p+4]]^table[2][bytes[p+5]]^table[1][bytes[p+6]]^table[0][bytes[p+7]];
    }
    for (;p<s.size();++p) crc=(crc>>8)^table[0][(crc^bytes[p])&255];
    return crc ^ 0xFFFFFFFFU;
}
void EncodeDeal(std::string& s, const Deal& d) {
    U64(s,d.id); U64(s,d.login); U64(s,d.order); U64(s,d.position); U64(s,d.units);
    U64(s,static_cast<std::uint64_t>(d.time_ms)); Text(s,d.symbol);
    U64(s,static_cast<unsigned>(d.side)); U64(s,d.opening); U64(s,d.copier_owned);
}
Deal DecodeDeal(Reader& r) {
    Deal d;
    d.id=r.Number(); d.login=r.Number(); d.order=r.Number(); d.position=r.Number(); d.units=r.Number();
    d.time_ms=r.Time(); d.symbol=r.String(255); d.side=r.Enum<Side>(1);
    d.opening=r.Count(1)!=0; d.copier_owned=r.Count(1)!=0; return d;
}
void EncodeBinding(std::string& s, const Link& link) {
    U64(s,link.opening_rule.has_value());
    if (!link.opening_rule) return;
    const auto& rule=*link.opening_rule;
    U64(s,rule.id); U64(s,rule.source_login); U64(s,rule.follower_login);
    U64(s,rule.enabled); U64(s,static_cast<unsigned>(rule.mode)); Text(s,rule.parameter);
}
bool SameBinding(const Link& a, const Link& b) {
    if (a.opening_rule.has_value()!=b.opening_rule.has_value()) return false;
    if (!a.opening_rule) return true;
    const auto& x=*a.opening_rule; const auto& y=*b.opening_rule;
    return x.id==y.id && x.source_login==y.source_login && x.follower_login==y.follower_login &&
        x.enabled==y.enabled && x.mode==y.mode && x.parameter==y.parameter &&
        x.symbol_mask==y.symbol_mask && x.auto_symbol==y.auto_symbol && x.source_suffix==y.source_suffix &&
        x.direction==y.direction;
}
}

std::string EncodeState(const State& state) {
    for (const auto& link:state.links) if (link.opening_rule && !ValidDirection(link.opening_rule->direction))
        Fail("invalid journal copy direction");
    std::string s;
    // Most live records have these fixed-size fields. Reserve once without
    // changing the wire format or weakening the final hard size check.
    s.reserve(std::min(kMaxSnapshot,std::size_t{4096}+state.links.size()*320+
        state.intents.size()*256+state.closed_sources.size()*32));
    U64(s,kCurrentSchema); Text(s,state.identity); Text(s,state.config_json); U64(s,state.revision);
    U64(s,static_cast<std::uint64_t>(state.activation_ms)); U64(s,state.activation_sequence);
    Text(s,state.halt_reason); U64(s,state.baseline.size());
    for (const auto& p : state.baseline) { U64(s,p.first); U64(s,p.second); }
    U64(s,state.links.size());
    for (const auto& link : state.links) {
        U64(s,link.rule); EncodeDeal(s,link.source); U64(s,static_cast<unsigned>(link.phase));
        U64(s,link.source_initial); U64(s,link.source_remaining); U64(s,link.follower_remaining);
        U64(s,link.follower_position); Ids(s,link.processed_closes); Text(s,link.reason);
    }
    U64(s,state.intents.size());
    for (const auto& i : state.intents) {
        U64(s,i.id); U64(s,i.link); U64(s,i.close); U64(s,i.source_event);
        U64(s,i.follower_login); U64(s,i.follower_position); U64(s,static_cast<unsigned>(i.side));
        Text(s,i.symbol); U64(s,i.units); U64(s,i.follower_before); U64(s,static_cast<std::uint64_t>(i.created_ms));
        U64(s,i.equity.source); U64(s,i.equity.follower); U64(s,i.equity.common_digits);
        U64(s,static_cast<std::uint64_t>(i.equity.source_query_ms));
        U64(s,static_cast<std::uint64_t>(i.equity.follower_query_ms));
        U64(s,static_cast<unsigned>(i.state)); U64(s,i.request_id); U64(s,i.retcode);
        U64(s,i.result_position); U64(s,i.filled); Ids(s,i.deals);
    }
    U64(s,state.openings_paused);
    U64(s,state.links.size());
    for (const auto& link:state.links) EncodeBinding(s,link);
    U64(s,state.next_intent_id); U64(s,static_cast<std::uint64_t>(state.source_floor_ms));
    U64(s,state.closed_sources.size());
    for (const auto& c:state.closed_sources) {
        U64(s,c.rule); U64(s,c.login); U64(s,c.position); U64(s,static_cast<std::uint64_t>(c.opening_ms));
    }
    Text(s,state.capacity_pause);
    U64(s,state.links.size());
    for (const auto& link:state.links) {
        Text(s,link.follower_symbol);
        if (link.opening_rule) {
            Text(s,link.opening_rule->symbol_mask); U64(s,link.opening_rule->auto_symbol);
            Text(s,link.opening_rule->source_suffix);
        }
    }
    U64(s,state.links.size());
    for (const auto& link:state.links) U64(s,link.exit_requested);
    U64(s,state.subscriptions.size());
    for (const auto& b:state.subscriptions) { U64(s,b.rule); U64(s,static_cast<std::uint64_t>(b.time_ms)); U64(s,b.sequence); }
    if (kCurrentSchema>=7) {
        U64(s,state.links.size());
        for (const auto& link:state.links) if (link.opening_rule)
            U64(s,static_cast<unsigned>(link.opening_rule->direction));
    }
    if (s.size() > kMaxSnapshot) Fail("journal snapshot limit reached");
    return s;
}

State DecodeState(const std::string& s) {
    if (s.size() > kMaxSnapshot) Fail("journal snapshot too large");
    Reader r{s};
    const auto version=r.Number();
    if (version<1 || version>kCurrentSchema) Fail("unsupported journal schema");
    State state;
    state.identity=r.String(); state.config_json=r.String(); state.revision=r.Number();
    state.activation_ms=r.Time(); state.activation_sequence=r.Number(); state.halt_reason=r.String();
    const auto baselines=r.Count(100000);
    for (std::size_t i=0; i<baselines; ++i) {
        const auto login=r.Number(); const auto position=r.Number();
        state.baseline.emplace_back(login,position);
    }
    const auto links=r.Count(Engine::kOpeningLinkLimit);
    for (std::size_t i=0; i<links; ++i) {
        Link link;
        link.rule=r.Number(); link.source=DecodeDeal(r); link.phase=r.Enum<Phase>(version==1?6:7);
        link.source_initial=r.Number(); link.source_remaining=r.Number(); link.follower_remaining=r.Number();
        link.follower_position=r.Number(); link.processed_closes=r.IdList(); link.reason=r.String();
        if (link.source_remaining > link.source_initial) Fail("invalid source remainder");
        state.links.push_back(std::move(link));
    }
    const auto intents=r.Count(100000);
    for (std::size_t n=0; n<intents; ++n) {
        Intent i;
        i.id=r.Number(); i.link=r.Count(Engine::kOpeningLinkLimit); i.close=r.Count(1)!=0; i.source_event=r.Number();
        i.follower_login=r.Number(); i.follower_position=r.Number(); i.side=r.Enum<Side>(1);
        i.symbol=r.String(255); i.units=r.Number(); i.follower_before=r.Number(); i.created_ms=r.Time();
        i.equity.source=r.Number(); i.equity.follower=r.Number();
        i.equity.common_digits=static_cast<unsigned>(r.Count(8));
        i.equity.source_query_ms=r.Time(); i.equity.follower_query_ms=r.Time();
        i.state=r.Enum<IntentState>(4);
        i.request_id=static_cast<std::uint32_t>(r.Count(UINT32_MAX));
        i.retcode=static_cast<std::uint32_t>(r.Count(UINT32_MAX));
        i.result_position=r.Number(); i.filled=r.Number(); i.deals=r.IdList();
        if (!i.id || (version<4 && i.id!=n+1) || (n && i.id<=state.intents.back().id) || i.link>=state.links.size() || !i.units ||
            (i.close && (i.units>i.follower_before || !i.follower_position)))
            Fail("invalid journal intent");
        state.intents.push_back(std::move(i));
    }
    if (version>=2) state.openings_paused=r.Count(1)!=0;
    if (version>=3) {
        if (r.Count(Engine::kOpeningLinkLimit)!=state.links.size()) Fail("invalid opening rule count");
        for (auto& link:state.links) if (r.Count(1)) {
            RuleConfig rule;
            rule.id=r.Number(); rule.source_login=r.Number(); rule.follower_login=r.Number();
            rule.enabled=r.Count(1)!=0; rule.mode=r.Enum<SizingMode>(2); rule.parameter=r.String(256);
            link.opening_rule=std::move(rule);
        }
    } else {
        // Legacy live mappings were protected against rule changes. Recover
        // their rules before any new config is applied. Closed history may
        // already refer to a removed/changed rule: do not invent its policy.
        CopierConfig config; std::string error;
        if (!ParseConfigJson(state.config_json,config,error)) Fail("invalid legacy configuration");
        std::unordered_map<Id,const RuleConfig*> rules;
        for (const auto& rule:config.relationships) if (rule.enabled) rules.emplace(rule.id,&rule);
        for (auto& link:state.links) {
            if (link.phase==Phase::Closed || link.phase==Phase::ManualClosed || link.phase==Phase::Skipped) continue;
            const auto rule=rules.find(link.rule);
            if (rule==rules.end()) Fail("legacy mapping has no enabled rule");
            link.opening_rule=*rule->second;
            // No historical journal before schema 7 supported reverse copying.
            link.opening_rule->direction=CopyDirection::Forward;
        }
    }
    if (version>=4) {
        state.next_intent_id=r.Number(); state.source_floor_ms=r.Time();
        const auto count=r.Count(100000);
        std::set<std::tuple<Id,Id,Id>> keys;
        for (std::size_t n=0;n<count;++n) {
            ClosedSource c{r.Number(),r.Number(),r.Number(),r.Time()};
            if (!c.rule || !c.login || !c.position || c.opening_ms<=state.source_floor_ms ||
                !keys.emplace(c.rule,c.login,c.position).second) Fail("invalid closed-source deduplication key");
            state.closed_sources.push_back(c);
        }
        state.capacity_pause=r.String(4096);
    } else {
        const auto largest=state.intents.empty()?0:state.intents.back().id;
        if (largest==std::numeric_limits<Id>::max()) Fail("intent id exhausted");
        state.next_intent_id=largest+1;
    }
    if (version>=5) {
        if (r.Count(Engine::kOpeningLinkLimit)!=state.links.size()) Fail("invalid symbol binding count");
        for (auto& link:state.links) {
            link.follower_symbol=r.String(255);
            if (link.opening_rule) {
                link.opening_rule->symbol_mask=r.String(4096);
                link.opening_rule->auto_symbol=r.Count(1)!=0;
                link.opening_rule->source_suffix=r.String(64);
            }
        }
    } else for (auto& link:state.links) link.follower_symbol=link.source.symbol;
    if (version>=6) {
        if (r.Count(Engine::kOpeningLinkLimit)!=state.links.size()) Fail("invalid exit flag count");
        for (auto& link:state.links) link.exit_requested=r.Count(1)!=0;
        const auto count=r.Count(1000); std::set<Id> ids;
        for (std::size_t n=0;n<count;++n) {
            SubscriptionBarrier b{r.Number(),r.Time(),r.Number()};
            if (!b.rule || !b.time_ms || !ids.insert(b.rule).second) Fail("invalid subscription barrier");
            state.subscriptions.push_back(b);
        }
    }
    if (version>=7) {
        if (r.Count(Engine::kOpeningLinkLimit)!=state.links.size()) Fail("invalid direction binding count");
        for (auto& link:state.links) if (link.opening_rule) {
            link.opening_rule->direction=r.Enum<CopyDirection>(1);
            if (!ValidDirection(link.opening_rule->direction)) Fail("invalid journal copy direction");
        }
    }
    if (!state.next_intent_id || (!state.intents.empty() && state.next_intent_id<=state.intents.back().id))
        Fail("invalid next intent id");
    if (r.p!=s.size() || !state.revision) Fail("invalid journal record framing");
    ValidateRuleBindings(state);
    return state;
}

namespace {
// Validate the in-memory schema directly. Serializing and immediately parsing
// the same entire snapshot allocated thousands of objects on every durable write.
void ValidateForWrite(const State& s,const State* previous) {
    const auto text=[](const std::string& value,std::size_t limit=1024*1024) {
        if (value.size()>limit) Fail("journal field limit exceeded");
    };
    const auto time=[](std::int64_t value) { if (value<0) Fail("invalid journal timestamp"); };
    if (!s.revision || !s.next_intent_id || s.baseline.size()>100000 || s.links.size()>Engine::kOpeningLinkLimit ||
        s.intents.size()>100000 || s.closed_sources.size()>100000) Fail("invalid journal state limits");
    text(s.identity); text(s.config_json); text(s.halt_reason); text(s.capacity_pause,4096);
    time(s.activation_ms); time(s.source_floor_ms);
    for (const auto& l:s.links) {
        text(l.source.symbol,255); text(l.follower_symbol,255); text(l.reason); time(l.source.time_ms);
        if (static_cast<unsigned>(l.source.side)>1 || static_cast<unsigned>(l.phase)>7 ||
            l.source_remaining>l.source_initial || l.processed_closes.size()>100000) Fail("invalid journal mapping");
        if (l.opening_rule) { const auto& r=*l.opening_rule;
            text(r.parameter,256); text(r.symbol_mask,4096); text(r.source_suffix,64);
            if (static_cast<unsigned>(r.mode)>2) Fail("invalid journal sizing mode");
            if (!ValidDirection(r.direction)) Fail("invalid journal copy direction");
        }
    }
    Id last=0;
    for (const auto& i:s.intents) {
        if (!i.id || i.id<=last || i.id>=s.next_intent_id || i.link>=s.links.size() || !i.units ||
            (i.close && (i.units>i.follower_before || !i.follower_position)) ||
            static_cast<unsigned>(i.side)>1 || static_cast<unsigned>(i.state)>4 ||
            i.equity.common_digits>8 || i.deals.size()>100000) Fail("invalid journal intent");
        last=i.id; text(i.symbol,255); time(i.created_ms); time(i.equity.source_query_ms); time(i.equity.follower_query_ms);
    }
    const bool same_keys=previous && previous->closed_sources.size()==s.closed_sources.size() &&
        std::equal(s.closed_sources.begin(),s.closed_sources.end(),previous->closed_sources.begin(),
            [](const ClosedSource& a,const ClosedSource& b){return a.rule==b.rule && a.login==b.login && a.position==b.position && a.opening_ms==b.opening_ms;});
    std::set<std::tuple<Id,Id,Id>> keys;
    for (const auto& c:s.closed_sources) {
        if (!c.rule || !c.login || !c.position || c.opening_ms<=s.source_floor_ms ||
            (!same_keys && !keys.emplace(c.rule,c.login,c.position).second)) Fail("invalid closed-source deduplication key");
    }
    ValidateRuleBindings(s);
    if (s.subscriptions.size()>1000) Fail("too many subscription barriers");
    std::set<Id> barriers;
    for (const auto& b:s.subscriptions) if (!b.rule || b.time_ms<=0 || !barriers.insert(b.rule).second) Fail("invalid subscription barrier");
}
void ValidateTransition(const State& latest,const State& state,Id latest_schema,Id schema,
                        const std::string* encoded_state=nullptr) {
    if (latest.identity!=state.identity) Fail("journal identity changed");
    if (schema<latest_schema) Fail("journal schema downgrade");
    if ((state.next_intent_id<latest.next_intent_id || state.source_floor_ms<latest.source_floor_ms))
        Fail("journal identity counter/admission floor decreased");
    if (schema>=3) {
        if (state.links.size()<latest.links.size()) Fail("journal lost mapping history");
        for (std::size_t n=0;n<latest.links.size();++n) {
            const auto& before=latest.links[n]; const auto& after=state.links[n];
            if (before.exit_requested && !after.exit_requested) Fail("relationship exit was cancelled");
            if (before.exit_requested!=after.exit_requested && latest.config_json==state.config_json) Fail("exit changed without subscription update");
            if (!SameBinding(before,after)) Fail("mapping opening rule changed");
            if (before.source.symbol!=after.source.symbol) Fail("mapping source symbol changed");
            if (before.follower_symbol!=after.follower_symbol &&
                (!before.follower_symbol.empty() || before.phase!=Phase::Candidate))
                Fail("mapping frozen target symbol changed");
        }
    }
    if (latest.config_json==state.config_json && (latest.subscriptions.size()!=state.subscriptions.size() ||
        !std::equal(latest.subscriptions.begin(),latest.subscriptions.end(),state.subscriptions.begin(),
            [](const SubscriptionBarrier& a,const SubscriptionBarrier& b) {return a.rule==b.rule && a.time_ms==b.time_ms && a.sequence==b.sequence;}))) {
        // Restart creates a new callback queue. Only its sequence component
        // may be rebased, uniformly to the new global startup watermark. Join
        // times and rule identities must survive exactly; ordinary writes may
        // not silently remove or relax subscription barriers.
        const bool restart=state.activation_ms>0 &&
            (state.activation_ms!=latest.activation_ms || state.activation_sequence!=latest.activation_sequence) &&
            latest.subscriptions.size()==state.subscriptions.size() &&
            std::equal(latest.subscriptions.begin(),latest.subscriptions.end(),state.subscriptions.begin(),
                [&](const SubscriptionBarrier& a,const SubscriptionBarrier& b) {
                    return a.rule==b.rule && a.time_ms==b.time_ms && b.sequence==state.activation_sequence;
                });
        if (!restart) Fail("subscription barrier changed without configuration commit or startup rebase");
    }
    if (latest.config_json!=state.config_json) {
        if (schema>=6 && state.activation_ms==latest.activation_ms && state.activation_sequence==latest.activation_sequence) {
            CopierConfig before,after; std::string error;
            if (!ParseConfigJson(latest.config_json,before,error) || !ParseConfigJson(state.config_json,after,error)) Fail("invalid subscription config");
            BootSnapshot boot{std::max<std::int64_t>(1,latest.activation_ms),latest.activation_sequence,{}};
            for (const auto& b:state.subscriptions) if (b.time_ms>boot.time_ms || b.sequence>boot.sequence) { boot.time_ms=b.time_ms; boot.sequence=b.sequence; }
            auto expected=PlanSubscriptionUpdate(latest,before,after,boot); expected.revision=state.revision;
            const auto encoded_expected=EncodeState(expected);
            if (encoded_state?encoded_expected!=*encoded_state:encoded_expected!=EncodeState(state))
                Fail("subscription update changed unrelated execution state");
            return;
        }
        const auto settled=[&](const Link& l) { return l.phase==Phase::Active || l.phase==Phase::Closed ||
            l.phase==Phase::ManualClosed || l.phase==Phase::Skipped ||
            UnsubmittedSymbolBlock(latest,static_cast<std::size_t>(&l-latest.links.data())); };
        if (!latest.openings_paused || !state.openings_paused || !latest.halt_reason.empty() ||
            !state.halt_reason.empty() || latest.links.size()!=state.links.size() ||
            latest.intents.size()!=state.intents.size() ||
            !std::all_of(latest.links.begin(),latest.links.end(),settled) ||
            !std::all_of(latest.intents.begin(),latest.intents.end(),[](const Intent& i) {
                return i.state==IntentState::Confirmed;
            })) Fail("invalid managed configuration transition");
        CopierConfig before, after; std::string error;
        if (!ParseConfigJson(latest.config_json,before,error) || !ParseConfigJson(state.config_json,after,error) ||
            !before.enabled || !after.enabled || state.activation_ms<=0 ||
            std::none_of(after.relationships.begin(),after.relationships.end(),[](const RuleConfig& rule) { return rule.enabled; }) ||
            (schema<3 && !ActiveRuleChangeReason(latest,before,after).empty()))
            Fail("incompatible managed configuration transition");
        // A config commit may only change config, revision and the new
        // admission barrier. Never accept altered positions, requests,
        // fills or close deduplication merely because sizes match.
        auto unchanged=state;
        unchanged.config_json=latest.config_json; unchanged.revision=latest.revision;
        unchanged.activation_ms=latest.activation_ms; unchanged.activation_sequence=latest.activation_sequence;
        unchanged.baseline=latest.baseline;
        // Legacy bindings are reconstructed, so equivalent decimal
        // spellings in a compatible config commit may differ.
        if (schema<3) for (std::size_t n=0;n<unchanged.links.size();++n)
            unchanged.links[n].opening_rule=latest.links[n].opening_rule;
        if (EncodeState(unchanged)!=EncodeState(latest)) Fail("configuration transition changed execution history");
    }
}
void ValidateCompaction(const State& before,const State& after,const std::string* encoded_after=nullptr) {
    if (after.source_floor_ms<before.source_floor_ms) Fail("compaction lowered source admission floor");
    auto expected=before;
    expected.revision=after.revision; expected.source_floor_ms=after.source_floor_ms;
    expected.closed_sources.erase(std::remove_if(expected.closed_sources.begin(),expected.closed_sources.end(),
        [&](const ClosedSource& c) { return c.opening_ms<=after.source_floor_ms; }),expected.closed_sources.end());
    expected.links.clear(); expected.intents.clear();
    const auto absent=std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> remap(before.links.size(),absent);
    std::vector<bool> blocked(before.links.size(),false);
    std::vector<unsigned> intent_counts(before.links.size(),0);
    for (const auto& i:before.intents) ++intent_counts.at(i.link);
    std::vector<Id> latest_close(before.links.size(),0);
    for (const auto& i:before.intents)
        if (i.state!=IntentState::Confirmed &&
            !(intent_counts[i.link]==1 && (SettledNoMoneyOpening(before.links.at(i.link),i) ||
              SettledUnsubmittedNoMoneyOpening(before.links.at(i.link),i)))) blocked.at(i.link)=true;
        else if (i.close) latest_close.at(i.link)=i.id;
    for (std::size_t n=0;n<before.links.size();++n) {
        const auto& link=before.links[n];
        const auto k=expected.links.size();
        if (k<after.links.size() && after.links[k].rule==link.rule && after.links[k].source.login==link.source.login &&
            after.links[k].source.position==link.source.position) {
            remap[n]=k; expected.links.push_back(link);
        } else {
            if (blocked[n] || link.follower_remaining ||
                (link.phase!=Phase::Closed && link.phase!=Phase::ManualClosed && link.phase!=Phase::Skipped))
                Fail("compaction removed an unsettled mapping");
            if (link.source.time_ms>after.source_floor_ms)
                expected.closed_sources.push_back({link.rule,link.source.login,link.source.position,link.source.time_ms});
        }
    }
    for (auto i:before.intents) if (remap.at(i.link)!=absent) {
        if (i.close && i.state==IntentState::Confirmed && latest_close[i.link]!=i.id) continue;
        i.link=remap[i.link]; expected.intents.push_back(std::move(i));
    }
    const auto encoded_expected=EncodeState(expected);
    if (encoded_after?encoded_expected!=*encoded_after:encoded_expected!=EncodeState(after))
        Fail("compaction changed retained execution state");
}
struct DurableFile {
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
#else
    int file=-1;
#endif
    enum class Mode { Open, Existing, Create };
    explicit DurableFile(const std::filesystem::path& path, Mode mode=Mode::Open, bool lease=false) {
#ifdef _WIN32
        const auto disposition=mode==Mode::Open?OPEN_ALWAYS:(mode==Mode::Existing?OPEN_EXISTING:CREATE_NEW);
        file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|(lease?0:FILE_SHARE_DELETE),nullptr,disposition,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if (file==INVALID_HANDLE_VALUE) FailWindows(lease?"journal lease open failed":
            (mode==Mode::Existing?"journal existing-file open failed":
             (mode==Mode::Create?"journal checkpoint create failed":"journal open failed")));
#else
        (void)lease;
        const auto flags=mode==Mode::Open?O_CREAT:(mode==Mode::Create?O_CREAT|O_EXCL:0);
        file=::open(path.c_str(),O_RDWR|flags|O_CLOEXEC,0600);
        if (file<0) Fail("cannot open journal");
        if (flock(file,LOCK_EX|LOCK_NB)!=0) { ::close(file); file=-1; Fail("journal already in use"); }
#endif
    }
    ~DurableFile() {
#ifdef _WIN32
        if (file!=INVALID_HANDLE_VALUE) CloseHandle(file);
#else
        if (file>=0) ::close(file);
#endif
    }
    DurableFile(const DurableFile&)=delete;
    DurableFile& operator=(const DurableFile&)=delete;
#ifdef _WIN32
    void Close(const char* operation) {
        if (!CloseHandle(file)) FailWindows(operation);
        file=INVALID_HANDLE_VALUE;
    }
    BY_HANDLE_FILE_INFORMATION Identity() {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file,&info)) FailWindows("journal file identity query failed");
        return info;
    }
    void VerifyCheckpoint(const BY_HANDLE_FILE_INFORMATION& expected,const std::string& content) {
        const auto actual=Identity();
        if (actual.dwVolumeSerialNumber!=expected.dwVolumeSerialNumber ||
            actual.nFileIndexHigh!=expected.nFileIndexHigh || actual.nFileIndexLow!=expected.nFileIndexLow)
            Fail("journal published checkpoint identity mismatch");
        Seek(false);
        std::array<char,64*1024> buffer{};
        for (std::size_t offset=0;offset<content.size();) {
            const auto amount=std::min(buffer.size(),content.size()-offset);
            Exact(buffer.data(),amount);
            if (std::memcmp(buffer.data(),content.data()+offset,amount)!=0)
                Fail("journal published checkpoint content mismatch");
            offset+=amount;
        }
        if (Read(buffer.data(),1)) Fail("journal published checkpoint has unexpected tail");
        Seek(true);
    }
#endif
    void Seek(bool end) {
#ifdef _WIN32
        LARGE_INTEGER offset{};
        if (!SetFilePointerEx(file,offset,nullptr,end?FILE_END:FILE_BEGIN)) FailWindows("journal seek failed");
#else
        if (lseek(file,0,end?SEEK_END:SEEK_SET)<0) Fail("journal seek failed");
#endif
    }
    std::size_t Read(char* data,std::size_t amount) {
#ifdef _WIN32
        DWORD got=0;
        if (!ReadFile(file,data,static_cast<DWORD>(amount),&got,nullptr)) FailWindows("journal read failed");
        return got;
#else
        ssize_t got;
        do { got=::read(file,data,amount); } while(got<0 && errno==EINTR);
        if (got<0) Fail("journal read failed");
        return static_cast<std::size_t>(got);
#endif
    }
    void Exact(char* data,std::size_t amount) {
        std::size_t offset=0;
        while (offset<amount) {
            const auto got=Read(data+offset,amount-offset);
            if (!got) Fail("torn journal tail; explicit recovery required");
            offset+=got;
        }
    }
    void Write(const std::string& data, bool flush=true,JournalCommitSample* sample=nullptr) {
        if (sample) sample->write_calls=SaturatingAdd(sample->write_calls,1);
        {
            PhaseTimer timer(sample?&sample->timings.write_ns:nullptr);
            std::size_t offset=0;
            while (offset<data.size()) {
#ifdef _WIN32
                DWORD written=0;
                if (!WriteFile(file,data.data()+offset,static_cast<DWORD>(data.size()-offset),&written,nullptr))
                    FailWindows("journal write failed");
                if (!written) FailWindows("journal write made no progress",ERROR_WRITE_FAULT);
#else
                ssize_t written;
                do { written=::write(file,data.data()+offset,data.size()-offset); } while(written<0 && errno==EINTR);
                if (written<=0) Fail("journal write failed");
#endif
                offset+=static_cast<std::size_t>(written);
            }
        }
        if (flush) Flush(sample);
    }
    void Flush(JournalCommitSample* sample=nullptr) {
        if (sample) sample->durable_flush_calls=SaturatingAdd(sample->durable_flush_calls,1);
        PhaseTimer timer(sample?&sample->timings.durable_flush_ns:nullptr);
#ifdef _WIN32
        if (!FlushFileBuffers(file)) FailWindows("journal durable flush failed");
#elif defined(__APPLE__)
        if (fcntl(file,F_FULLFSYNC)<0) Fail("journal durable flush failed");
#else
        if (fsync(file)<0) Fail("journal durable flush failed");
#endif
    }
};

void SyncDirectory(const std::filesystem::path& path) {
#ifndef _WIN32
    const int fd=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (fd<0) Fail("journal directory open failed");
    const auto result=fsync(fd); ::close(fd);
    if (result!=0) Fail("journal directory flush failed");
#else
    (void)path; // Publishing uses MoveFileExW with MOVEFILE_WRITE_THROUGH.
#endif
}
std::string Frame(const std::string& payload, Id revision, bool checkpoint=false, bool prune=false) {
    std::string record;
    record.reserve(32+payload.size());
    record.append(checkpoint?kCheckpointMagic:(prune?kPruneMagic:kMagic),8);
    U64(record,payload.size()); U64(record,revision); U64(record,Crc(payload));
    record+=payload; return record;
}
// Delta offsets address the previous complete snapshot, never its compressed
// bytes. Both the delta frame and reconstructed result have independent CRCs.
std::string Delta(const std::string& before,const std::string& after,Id base,bool prune) {
    // Retain the exact version-4 patch boundaries: changed spans are joined
    // until 32 unchanged bytes separate them. Bounded block comparisons skip
    // long unchanged spans and dense changed spans without scanning each byte.
    const auto common=std::min(before.size(),after.size());
    std::string delta(40,'\0');
    std::size_t patches=0,p=0;
    while (p<after.size()) {
        while (p<common && common-p>=64 &&
               std::memcmp(before.data()+p,after.data()+p,64)==0) p+=64;
        while (p<common && before[p]==after[p]) ++p;
        if (p==after.size()) break;
        const auto start=p;
        auto end=++p;
        bool separated=false;
        while (p<common) {
            const auto count=std::min(std::size_t{32},common-p);
            auto last=p+count;
            if (std::memcmp(before.data()+p,after.data()+p,count)==0) {
                p+=count;
                if (count==32) { separated=true; break; }
                if (p==after.size()) break;
            } else {
                // p starts immediately after the last changed byte. A block
                // of at most 32 bytes containing a change cannot hide a full
                // 32-byte unchanged run, even across its leading boundary.
                while (before[last-1]==after[last-1]) --last;
                end=p=last;
            }
        }
        if (!separated && p>=common && p<after.size()) end=p=after.size();
        U64(delta,start); U64(delta,end-start); delta.append(after,start,end-start); ++patches;
        if (delta.size()-40>=after.size()) return {};
    }
    if (delta.size()>=after.size()) return {};
    std::string header; header.reserve(40);
    U64(header,prune); U64(header,base); U64(header,after.size());
    U64(header,Crc(after)); U64(header,patches);
    delta.replace(0,header.size(),header);
    return delta;
}
std::string ApplyDelta(const std::string& before,const std::string& delta,Id base,bool& prune) {
    Reader r{delta}; prune=r.Count(1)!=0;
    if (r.Number()!=base) Fail("journal delta base revision mismatch");
    const auto size=r.Count(kMaxSnapshot); const auto crc=r.Number();
    const auto count=r.Count(kMaxSnapshot/16);
    auto result=before; result.resize(size,'\0');
    std::size_t end=0;
    for (std::size_t n=0;n<count;++n) {
        const auto offset=r.Count(size), length=r.Count(size);
        if (!length || offset<end || length>size-offset || length>delta.size()-r.p)
            Fail("invalid journal delta range");
        result.replace(offset,length,delta,r.p,length); r.p+=length; end=offset+length;
    }
    if (r.p!=delta.size() || Crc(result)!=crc) Fail("journal delta result checksum mismatch");
    return result;
}

}

struct FileJournal::Impl {
    std::filesystem::path path;
    std::unique_ptr<DurableFile> lease, file;
    std::uint64_t size=0, revision=0, rotate_bytes=0, rotations=0, written=0, logical=0;
    // Keep the complete decoded payload. Prefixing it with a fake frame and
    // slicing that prefix off for every delta needlessly copied the snapshot.
    std::string latest_payload;
    std::size_t appended_records=0;
    State latest_state;
    Id latest_schema=0;
    JournalPerformance performance;
    bool loaded=false, failed=false, initialized=false;
    Impl(const std::filesystem::path& value, std::uint64_t threshold)
        :path(std::filesystem::absolute(value)),rotate_bytes(threshold) {
        if (threshold<1024 || threshold>kMaxJournal) Fail("invalid journal rotation threshold");
        auto lock_path=path; lock_path+=".lock";
        lease=std::make_unique<DurableFile>(lock_path,DurableFile::Mode::Open,true);
        std::string marker(8,'\0');
        if (lease->Read(marker.data(),1)) {
            lease->Exact(marker.data()+1,7);
            if (marker!="TCXL0001") Fail("invalid journal lease marker");
            initialized=true;
        }
        auto archive=path; archive+=".archive";
        if ((initialized || (std::filesystem::exists(archive) && !std::filesystem::is_empty(archive))) &&
            (!std::filesystem::exists(path) || std::filesystem::file_size(path)==0))
            Fail("initialized journal missing/empty; explicit recovery required");
        file=std::make_unique<DurableFile>(path);
    }
    void MarkInitialized(JournalCommitSample* sample=nullptr) {
        if (initialized) return;
        lease->Seek(false); lease->Write("TCXL0001",true,sample); SyncDirectory(path.parent_path());
        initialized=true;
    }
    void Checkpoint(const std::string& checkpoint,JournalCommitSample* sample=nullptr) {
        auto temporary=path; temporary+=".checkpoint.tmp";
        // The lease owns this path; stale temporary content is never loaded as state.
        // Refuse unexpected leftovers instead of overwriting recovery evidence.
        auto replacement=std::make_unique<DurableFile>(temporary,DurableFile::Mode::Create);
        replacement->Write(checkpoint,true,sample);
        SyncDirectory(path.parent_path());
        // Retain the stable lease throughout publication. Any failure stops
        // Save before a new trade can be sent. Never fall back
        // to an archive or a .tmp on Load: that could omit a submitted intent.
#ifdef _WIN32
        // Windows replacement must not depend on renaming over our open target.
        // The independent .lock still excludes every cooperating writer while
        // the data handles are closed. Never delete the old main file first.
        const auto identity=replacement->Identity();
        replacement->Close("journal checkpoint close failed");
        file->Close("journal main close before checkpoint failed");
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            FailWindows("journal checkpoint publish failed");
        // OPEN_EXISTING cannot silently recreate a lost main file. A failed
        // reopen/verification poisons this store even if publication succeeded.
        replacement=std::make_unique<DurableFile>(path,DurableFile::Mode::Existing);
        replacement->VerifyCheckpoint(identity,checkpoint);
#else
        if (::rename(temporary.c_str(),path.c_str())!=0) Fail("journal checkpoint publish failed");
        SyncDirectory(path.parent_path());
#endif
        file=std::move(replacement);
        size=checkpoint.size(); appended_records=0; ++rotations;
    }
};

FileJournal::FileJournal(const std::filesystem::path& path, std::uint64_t threshold)
    : impl_(std::make_unique<Impl>(path,threshold)) {}
FileJournal::~FileJournal() = default;
std::uint64_t FileJournal::rotations() const { return impl_->rotations; }
std::uint64_t FileJournal::bytes() const { return impl_->size; }
std::uint64_t FileJournal::written_bytes() const { return impl_->written; }
std::uint64_t FileJournal::logical_bytes() const { return impl_->logical; }
JournalPerformance FileJournal::performance() const noexcept { return impl_->performance; }
std::string FileJournal::OpeningBlockReason(const State& state,std::size_t additions) {
    // Keep half the snapshot budget plus most of the live-record budget for
    // submitted results and existing closes. This check never blocks a close.
    const auto used=EncodeState(state).size();
    if (used>kOpeningSnapshotBytes || additions>(kOpeningSnapshotBytes-used)/kOpeningMappingReserveBytes)
        return "运行快照接近容量阈值，已预留已有仓位平仓空间";
    std::error_code error;
    const auto space=std::filesystem::space(impl_->path.parent_path(),error);
    if (error) return "无法检查账本磁盘剩余空间";
    if (space.available<kOpeningDiskReserveBytes) return "账本磁盘可用空间低于 256 MiB，已暂停新仓";
    return {};
}
bool FileJournal::Load(State& output) {
    auto& f=*impl_;
    if (f.loaded || f.failed) Fail("journal load called twice");
    try {
        State latest;
        bool found=false;
        std::uint64_t latest_schema=0;
        for (;;) {
            std::string header(32,'\0');
            const auto first=f.file->Read(header.data(),1);
            if (!first) break;
            f.file->Exact(header.data()+1,31);
            const bool checkpoint=std::memcmp(header.data(),kCheckpointMagic,8)==0;
            bool prune=std::memcmp(header.data(),kPruneMagic,8)==0;
            const bool delta=std::memcmp(header.data(),kDeltaMagic,8)==0;
            if ((!checkpoint && !prune && !delta && std::memcmp(header.data(),kMagic,8)!=0) || (checkpoint && found) || ((prune || delta) && !found))
                Fail("invalid journal magic/checkpoint position");
            Reader r{header,8};
            const auto length=r.Count(kMaxSnapshot); const auto revision=r.Number(); const auto crc=r.Number();
            if ((!checkpoint && revision!=f.revision+1) || !revision || f.size>kMaxJournal-32 || length>kMaxJournal-f.size-32)
                Fail("invalid journal sequence/size");
            std::string payload(length,'\0'); f.file->Exact(payload.data(),length);
            if (crc!=Crc(payload)) Fail("journal checksum mismatch");
            if (delta) payload=ApplyDelta(f.latest_payload,payload,f.revision,prune);
            auto state=DecodeState(payload);
            const auto schema=Reader{payload}.Number();
            if (state.revision!=revision) Fail("journal revision mismatch");
            if (found) { if (schema<latest_schema) Fail("journal schema downgrade");
                // A legacy payload may use an older schema than EncodeState;
                // only reuse bytes when they are the current canonical schema.
                const auto* canonical=schema==kCurrentSchema?&payload:nullptr;
                if (prune) ValidateCompaction(latest,state,canonical);
                else ValidateTransition(latest,state,latest_schema,schema,canonical); }
            latest=std::move(state); latest_schema=schema; found=true; f.revision=revision; f.size+=32+length;
            f.latest_payload=std::move(payload);
            if (checkpoint) f.appended_records=0; else ++f.appended_records;
        }
        f.loaded=true;
        if (found) f.MarkInitialized();
        if (found) { f.latest_state=latest; f.latest_schema=latest_schema; output=std::move(latest); }
        return found;
    } catch (...) { f.failed=true; throw; }
}
void FileJournal::Save(const State& state) {
    Commit(state,false);
}
void FileJournal::Compact(const State& state) {
    Commit(state,true,true);
}
void FileJournal::Prune(const State& state) { Commit(state,true); }
void FileJournal::Commit(const State& state,bool compact,bool force_checkpoint) {
    auto& f=*impl_;
    CommitTimer timing(f.performance);
    auto& sample=timing.sample;
    if (!f.loaded || f.failed || state.revision!=f.revision+1) Fail("journal is not writable");
    try {
        std::string payload;
        {
            PhaseTimer phase(sample.timings.encode_ns);
            payload=EncodeState(state);
        }
        sample.payload_bytes=payload.size();
        const auto schema=Reader{payload}.Number();
        {
            PhaseTimer phase(sample.timings.validation_ns);
            ValidateForWrite(state,f.latest_payload.empty()?nullptr:&f.latest_state);
            if (schema<f.latest_schema) Fail("journal schema downgrade");
            if (compact) {
                if (f.latest_payload.empty()) Fail("cannot compact an empty journal");
                ValidateCompaction(f.latest_state,state,&payload);
            } else if (!f.latest_payload.empty()) {
                // A replacement checkpoint must enforce the same transition rules
                // as append/replay before discarding the previous frame.
                ValidateTransition(f.latest_state,state,f.latest_schema,schema,&payload);
            }
        }
        f.MarkInitialized(&sample);
        // A forced/count-based checkpoint never uses a delta. Avoid scanning
        // both complete payloads and computing a discarded delta checksum.
        bool checkpoint=force_checkpoint || f.appended_records>=kReplayRecordLimit ||
            (compact && state.links.empty() && state.intents.empty() && f.size>8*1024*1024);
        std::string record;
        {
            PhaseTimer phase(sample.timings.delta_frame_ns);
            if (checkpoint) record=Frame(payload,state.revision,true);
            else {
                const auto delta=f.latest_payload.empty()?std::string{}:Delta(f.latest_payload,payload,f.revision,compact);
                record=delta.empty()?Frame(payload,state.revision,false,compact):Frame(delta,state.revision);
                if (!delta.empty()) record.replace(0,8,kDeltaMagic,8);
                if (f.size && f.size+record.size()>f.rotate_bytes) {
                    checkpoint=true;
                    if (delta.empty()) record.replace(0,8,kCheckpointMagic,8);
                    else record=Frame(payload,state.revision,true);
                }
            }
        }
        sample.checkpoint=checkpoint; sample.record_bytes=record.size();
        if (checkpoint) {
            PhaseTimer phase(sample.timings.checkpoint_ns);
            f.Checkpoint(record,&sample);
        } else {
            if (f.size+32+payload.size()>kMaxJournal) Fail("journal size limit reached; archive/recovery required");
            f.file->Write(record,true,&sample);
            f.size+=record.size(); ++f.appended_records;
        }
        {
            PhaseTimer phase(sample.timings.state_update_ns);
            f.revision=state.revision;
            f.written+=record.size(); f.logical+=32+payload.size();
            f.latest_payload=std::move(payload); f.latest_state=state; f.latest_schema=schema;
        }
        sample.succeeded=true;
    } catch (...) { f.failed=true; throw; }
}

} // namespace tradecopier::execution

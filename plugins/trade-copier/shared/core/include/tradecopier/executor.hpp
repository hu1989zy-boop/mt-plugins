#pragma once

#include "tradecopier/config.hpp"
#include "tradecopier/symbols.hpp"
#include <string>
#include <optional>
#include <map>
#include <tuple>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <utility>
#include <vector>

namespace tradecopier::execution {

// Volumes use shared 1e-8 lot units; adapters convert native units explicitly.
enum class Side : unsigned { Buy, Sell };
enum class Phase : unsigned { Candidate, Opening, Active, Closing, Closed, ManualClosed, Review, Skipped };
enum class IntentState : unsigned { Prepared, Submitted, Confirmed, Rejected, Review };
enum class ReadState { Ready, Waiting, Invalid };

struct Deal {
    Id id = 0, login = 0, order = 0, position = 0;
    Units units = 0;
    std::int64_t time_ms = 0;
    std::string symbol;
    Side side = Side::Buy;
    bool opening = false, copier_owned = false;
};
struct SourceClose { Id deal = 0; Units units = 0; std::int64_t time_ms = 0; };
struct SourceView {
    ReadState state = ReadState::Invalid;
    Units opening_units = 0, remaining = 0;
    std::int64_t first_fill_ms = 0;
    // Complete, deduplicated history for this position, sorted by time and deal.
    // The adapter rejects reversals, Close By, transfers and multiple entry orders.
    std::vector<SourceClose> closes;
    std::string reason;
};
struct EquitySnapshot {
    std::uint64_t source = 0, follower = 0;
    unsigned common_digits = 0;
    std::int64_t source_query_ms = 0, follower_query_ms = 0;
};
struct Link {
    Id rule = 0;
    Deal source;
    Phase phase = Phase::Candidate;
    Units source_initial = 0, source_remaining = 0, follower_remaining = 0;
    Id follower_position = 0;
    std::vector<Id> processed_closes;
    std::string reason;
    // Frozen at admission. Never resolve an existing mapping through live config.
    // Only terminal mappings imported from schema 1/2 may lack this metadata.
    std::optional<RuleConfig> opening_rule;
    std::string follower_symbol; // Frozen before first submission, independent of source symbol.
    bool exit_requested = false; // Close only this subscription's frozen target position.
};
// Actual target position side derived from the frozen opening rule.
Side FollowerSide(const Link&);
struct Intent {
    Id id = 0;
    std::size_t link = 0;
    bool close = false;
    Id source_event = 0, follower_login = 0, follower_position = 0;
    Side side = Side::Buy;
    std::string symbol;
    Units units = 0, follower_before = 0;
    std::int64_t created_ms = 0;
    EquitySnapshot equity;
    IntentState state = IntentState::Prepared;
    std::uint32_t request_id = 0, retcode = 0;
    Id result_position = 0;
    Units filled = 0;
    std::vector<Id> deals;
};
struct ClosedSource {
    Id rule=0, login=0, position=0;
    std::int64_t opening_ms=0;
};
struct SubscriptionBarrier {
    Id rule=0;
    std::int64_t time_ms=0;
    Id sequence=0; // Queue-local watermark; rebased to the startup watermark on restart.
};
struct State {
    std::string identity, config_json;
    std::uint64_t revision = 0;
    std::int64_t activation_ms = 0;
    Id activation_sequence = 0;
    std::vector<std::pair<Id, Id>> baseline; // login, position
    std::vector<Link> links;
    std::vector<Intent> intents;
    std::string halt_reason; // Legacy schema field; cleared on startup, never a runtime gate.
    bool openings_paused = false;
    Id next_intent_id = 1;
    std::int64_t source_floor_ms = 0;
    // Short-lived deduplication keys only, never closed trade details.
    std::vector<ClosedSource> closed_sources;
    std::string capacity_pause;
    std::vector<SubscriptionBarrier> subscriptions;
};
struct BootSnapshot {
    std::int64_t time_ms = 0;
    Id sequence = 0;
    std::vector<std::pair<Id, Id>> positions;
};
State PlanSubscriptionUpdate(const State&, const CopierConfig&, const CopierConfig&, const BootSnapshot&);
// Legacy schema 1/2 replay guard. Those records did not persist opening rules.
std::string ActiveRuleChangeReason(const State&, const CopierConfig& current, const CopierConfig& next);
// Validate every bound rule and its account references, including retired rules.
void ValidateRuleBindings(const State&);
bool UnsubmittedSymbolBlock(const State&,std::size_t link);
// A correlated platform no-money opening refusal with durable zero-fill evidence may be
// retired. Never applies to rejected closes, ambiguous results or live positions.
bool SettledNoMoneyOpening(const Link&, const Intent&);
// MT4-only synchronous refusal: Submit explicitly reported NotSubmitted with
// code 134 and no request ID. This is distinct from broker callback evidence.
bool SettledUnsubmittedNoMoneyOpening(const Link&, const Intent&);
// Accounts with an unresolved execution must not receive further submissions.
// This is derived from durable intents, never from the bounded UI rows.
std::vector<Id> ExecutionReviewAccounts(const State&);
struct Submission {
    enum class Status { Queued, NotSubmitted, Unknown } status = Status::Unknown;
    std::uint32_t request_id = 0, retcode = 0;
};
struct Result {
    enum class Status { Waiting, Filled, Rejected, Uncertain } status = Status::Waiting;
    Id position = 0;
    Units units = 0;
    std::uint32_t retcode = 0;
    std::vector<Id> deals;
    std::string reason; // Read-only evidence diagnostic; not a claim of rejection.
    // Optional, verified target deal timestamps for diagnostics only. They are
    // not persisted and never influence execution/recovery decisions.
    std::int64_t first_deal_ms = 0, last_deal_ms = 0;
};

struct ExecutionPerformance {
    std::uint64_t confirmation_passes = 0, confirmation_reuses = 0;
    std::uint64_t confirmation_us = 0, confirmation_max_us = 0;
    std::uint64_t confirmation_saves = 0, confirmation_save_us = 0, confirmation_save_max_us = 0;
    std::uint64_t account_limit_visits = 0, global_limit_hits = 0;
    // Successful durable prune calls only; failures/in-memory cleanup do not
    // count as commits. Removed keys count expiry, excluding newly added keys.
    std::uint64_t prune_commits = 0, prune_expiry_only_commits = 0, prune_closed_sources_removed = 0;
    // Calls that deferred expired keys, not source trades or elapsed windows.
    std::uint64_t prune_expiry_deferred_passes = 0;
};

class Store {
public:
    virtual ~Store() = default;
    virtual bool Load(State& state) = 0; // false only for a new, empty journal
    virtual void Save(const State& state) = 0; // return only after durable flush
    virtual void Compact(const State& state) { Save(state); }
    virtual void Prune(const State& state) { Compact(state); }
    virtual std::string OpeningBlockReason(const State&, std::size_t) { return {}; }
};

// All methods run on ONE worker, never in a server callback. A read failure must
// throw or return Invalid; it must never be converted into a zero position.
class Port {
public:
    virtual ~Port() = default;
    virtual std::int64_t NowMs() = 0;
    virtual SourceView ReadSource(const Deal& opening) = 0;
    virtual void BeginCycle() {}
    virtual SymbolResolution ResolveSymbol(const RuleConfig&,const std::string& source,Side) { return {source,{}}; }
    virtual VolumeLimits Limits(const RuleConfig&, const std::string& source_symbol,const std::string& target_symbol,
                                Side side, bool close) = 0;
    virtual EquitySnapshot Equities(const RuleConfig&) = 0;
    virtual Units FollowerRemaining(Id login, Id position, const std::string& symbol,
                                    Side position_side) = 0;
    virtual Submission Submit(const Intent&) = 0;
    virtual Result Reconcile(const Intent&) = 0;
    // Called only after the resolution is durable. Implementations may then
    // release bounded per-request callback evidence.
    virtual void ForgetRequest(Id) noexcept {}
    virtual void Log(const std::string&) = 0;
};

// One SDK worker pipelines independent positions with per-account and global
// bounds. Each mapping has at most one unresolved request. Ambiguous reconciliation
// isolates only the affected mapping. Runtime faults never globally halt dispatch.
// Lost storage forbids new openings; verifiable old positions continue in memory.
// A timeout/restart never retries.
class Engine {
public:
    Engine(const CopierConfig&, std::string identity, Port&, Store&, bool managed_config = false);
    Engine(const std::function<CopierConfig()>& bootstrap, std::string identity, Port&, Store&, bool managed_config);
    void Start(const BootSnapshot&);
    void Admit(const Deal&, Id callback_sequence);
    // persist=false is worker-only: Tick/ReportFault must follow before management reads.
    // Candidates have no side effects; prepared intents are always durable before Submit.
    void AdmitBatch(const std::vector<std::pair<Deal,Id>>&, bool persist = true);
    void ConfirmPending();
    // Callback hint only. Execution still requires fresh source history and
    // exact target evidence; absent/stale/duplicate hints cannot create trades.
    void NotifySourceClose(Id login,Id position);
    static constexpr std::size_t kMaxInFlight = 512;
    static constexpr std::size_t kAccountInFlight = 8;
    static constexpr std::size_t kAdmissionBatch = 32;
    static constexpr std::size_t kPlanningVisits = 1024;
    static constexpr std::size_t kOpeningLinkLimit = 10000;
    // MT5 worker may reuse its immediately preceding ConfirmPending pass only
    // when no new callback arrived. Reuse is single-use; other callers retain
    // fresh reconciliation by default. MT4 always reconciles as before.
    void Tick(bool reuse_confirmation = false);
    const ExecutionPerformance& performance() const noexcept { return performance_; }
    bool HasPending() const { return !pending_.empty(); }
    void ReportFault(const std::string& reason);
    // Management runs on the worker. Subscription commits compare configuration,
    // not the rapidly changing trading revision. Existing mappings retain frozen rules.
    // ApplyConfig is the legacy paused migration API; UI uses UpdateSubscriptions.
    void PauseOpenings(Id expected_revision);
    void ResumeOpenings(Id expected_revision, const BootSnapshot&);
    void ApplyConfig(Id expected_revision, const CopierConfig&, const BootSnapshot&);
    void UpdateSubscriptions(const std::string& expected_config, const CopierConfig&, const BootSnapshot&);
    const std::string& warning() const { return warning_; }
    bool started() const { return started_; }
    bool storage_degraded() const { return storage_failed_; }
    std::string ApplyBlockReason() const;
    // Full-state validation, independent of the bounded rows shown in the UI.
    std::string ConfigChangeBlockReason(const CopierConfig&) const;
    std::string InspectIntent(Id id); // queries only; never resets or resubmits
    const State& state() const { return state_; }

private:
    const RuleConfig& Rule(const Link&) const;
    void Save();
    void SaveProgress();
    void Log(const std::string&) noexcept;
    void Review(Link&, const std::string&);
    void AdmitOne(const Deal&, Id);
    void ResolvePending();
    bool ResolveOne(Intent&, const Result* evidence=nullptr);
    void RefreshExitingReviews();
    std::int64_t last_exit_review_ms_=0;
    std::size_t next_exit_review_=0;
    void IsolateExecution(Intent&, const Result&, const std::string& reason);
    void Prepare(std::size_t, Intent, std::int64_t source_event_ms);
    void SubmitBatch(std::size_t first, std::int64_t planning_ms);
    void AbandonPrepared(std::size_t first, const std::string& reason);
    const SourceView& ReadSource(const Deal&);
    void Process(std::size_t);
    void PrioritizeSourceCloses(std::size_t, const SourceView&);
    void TickWork(bool reuse_confirmation);
    void PruneSettled();
    std::string OpeningCapacity(std::size_t);
    void PauseForCapacity(const std::string&);
    void ManagementCheck(Id expected_revision) const;
    CopierConfig config_;
    std::unordered_map<Id,SymbolMask> symbol_masks_;
    Port& port_;
    Store& store_;
    State state_;
    std::string warning_;
    bool restored_ = false, started_ = false, storage_failed_ = false;
    bool dirty_ = false;
    bool confirmation_ready_ = false;
    ExecutionPerformance performance_;
    std::size_t next_link_ = 0, next_candidate_ = 0;
    // Compaction rebuilds these indices only after the new state is durable.
    std::vector<std::size_t> pending_, work_links_;
    // Volatile hints only: Process always revalidates source/target evidence.
    std::map<std::pair<Id,Id>,std::vector<std::size_t>> active_sources_;
    std::deque<std::size_t> ready_closes_;
    std::unordered_set<std::size_t> queued_closes_;
    std::map<std::pair<Id,Id>,bool> close_hints_;
    using SourceKey = std::tuple<Id,Id,Id,Id,Units,std::int64_t,std::string,Side,bool,bool>;
    std::map<SourceKey,SourceView> source_cache_;
    std::unordered_map<std::size_t, std::int64_t> source_wait_since_;
    std::unordered_map<Id,std::int64_t> batch_source_times_;
};

} // namespace tradecopier::execution

#pragma once
#include "history_index.hpp"
#include "tradecopier/journal.hpp"
#include <string>
#include <vector>

namespace tradecopier::execution {
// Worker-only, at most three aggregate records per five seconds of activity.
// Delta values cover interval_ms; fields ending _max_us_total are lifetime
// per-operation maxima. Diagnostic failures must not change trading state.
class PerformanceLog {
public:
    void Reset(std::int64_t now_ms) noexcept {
        last_ms_=now_ms; engine_={}; journal_={}; history_={};
        committed_frame_bytes_=0; logical_snapshot_bytes_=0;
    }
    std::vector<std::string> Poll(std::int64_t now_ms,const ExecutionPerformance& e,
            const JournalPerformance& j,const HistoryMetrics& h,std::size_t links,
            std::size_t intents,std::uint64_t journal_bytes,
            std::uint64_t committed_frame_bytes=0,std::uint64_t logical_snapshot_bytes=0) {
        if (now_ms<last_ms_) { Reset(now_ms); return {}; }
        if (now_ms-last_ms_<5000) return {};
        const auto interval=now_ms-last_ms_;
        const bool active=j.attempts!=journal_.attempts || h.deal_queries!=history_.deal_queries ||
            h.order_queries!=history_.order_queries || h.reconcile_calls!=history_.reconcile_calls;
        std::vector<std::string> lines;
        if (active) {
            const auto field=[](const char* name,std::uint64_t value) {
                return std::string(" ")+name+"="+std::to_string(value);
            };
            const auto delta=[](std::uint64_t value,std::uint64_t old) { return value>=old?value-old:value; };
            lines.push_back("PERF interval_ms="+std::to_string(interval)+
                field("links",links)+field("intents",intents)+field("journal_bytes",journal_bytes)+
                field("confirm_passes",delta(e.confirmation_passes,engine_.confirmation_passes))+
                field("confirm_reuses",delta(e.confirmation_reuses,engine_.confirmation_reuses))+
                field("confirm_us",delta(e.confirmation_us,engine_.confirmation_us))+
                field("confirm_max_us_total",e.confirmation_max_us)+
                field("confirm_saves",delta(e.confirmation_saves,engine_.confirmation_saves))+
                field("confirm_save_us",delta(e.confirmation_save_us,engine_.confirmation_save_us))+
                field("confirm_save_max_us_total",e.confirmation_save_max_us)+
                field("account_limit_visits",delta(e.account_limit_visits,engine_.account_limit_visits))+
                field("global_limit_hits",delta(e.global_limit_hits,engine_.global_limit_hits))+
                field("prune_commits",delta(e.prune_commits,engine_.prune_commits))+
                field("prune_expiry_only_commits",delta(e.prune_expiry_only_commits,engine_.prune_expiry_only_commits))+
                field("prune_closed_sources_removed",delta(e.prune_closed_sources_removed,engine_.prune_closed_sources_removed))+
                field("prune_expiry_deferred_passes",delta(e.prune_expiry_deferred_passes,engine_.prune_expiry_deferred_passes)));
            std::string journal="PERF_JOURNAL interval_ms="+std::to_string(interval)+
                field("attempts",delta(j.attempts,journal_.attempts))+
                field("failed",delta(j.failed,journal_.failed))+
                field("checkpoints",delta(j.checkpoints,journal_.checkpoints))+
                field("write_calls",delta(j.write_calls,journal_.write_calls))+
                field("flush_calls",delta(j.durable_flush_calls,journal_.durable_flush_calls))+
                // Successful frame bytes include replacement checkpoints, not
                // lease markers, failed partial writes or filesystem overhead.
                field("committed_frame_bytes",delta(committed_frame_bytes,committed_frame_bytes_))+
                field("logical_snapshot_bytes",delta(logical_snapshot_bytes,logical_snapshot_bytes_));
            const auto phase=[&](const char* name,std::uint64_t now,std::uint64_t old,std::uint64_t peak) {
                journal+=field((std::string(name)+"_us").c_str(),delta(now,old)/1000)+
                    field((std::string(name)+"_max_us_total").c_str(),peak/1000);
            };
            phase("total",j.total.total_ns,journal_.total.total_ns,j.max.total_ns);
            phase("encode",j.total.encode_ns,journal_.total.encode_ns,j.max.encode_ns);
            phase("validate",j.total.validation_ns,journal_.total.validation_ns,j.max.validation_ns);
            phase("delta_frame",j.total.delta_frame_ns,journal_.total.delta_frame_ns,j.max.delta_frame_ns);
            phase("write",j.total.write_ns,journal_.total.write_ns,j.max.write_ns);
            phase("flush",j.total.durable_flush_ns,journal_.total.durable_flush_ns,j.max.durable_flush_ns);
            phase("checkpoint",j.total.checkpoint_ns,journal_.total.checkpoint_ns,j.max.checkpoint_ns);
            phase("state_update",j.total.state_update_ns,journal_.total.state_update_ns,j.max.state_update_ns);
            lines.push_back(std::move(journal));
            lines.push_back("PERF_HISTORY interval_ms="+std::to_string(interval)+
                field("cycles",delta(h.cycle_count,history_.cycle_count))+
                field("deal_queries",delta(h.deal_queries,history_.deal_queries))+
                field("order_queries",delta(h.order_queries,history_.order_queries))+
                field("cache_hits",delta(h.cached_windows,history_.cached_windows))+
                field("indexed_deal_rows",delta(h.indexed_deal_rows,history_.indexed_deal_rows))+
                field("indexed_order_rows",delta(h.indexed_order_rows,history_.indexed_order_rows))+
                field("index_lookups",delta(h.index_lookups,history_.index_lookups))+
                field("index_hits",delta(h.index_hits,history_.index_hits))+
                field("candidate_rows",delta(h.candidate_rows,history_.candidate_rows))+
                field("reconcile_calls",delta(h.reconcile_calls,history_.reconcile_calls))+
                field("reconcile_us",delta(h.reconcile_us,history_.reconcile_us)));
        }
        last_ms_=now_ms; engine_=e; journal_=j; history_=h;
        committed_frame_bytes_=committed_frame_bytes; logical_snapshot_bytes_=logical_snapshot_bytes;
        return lines;
    }
private:
    std::int64_t last_ms_=0;
    ExecutionPerformance engine_;
    JournalPerformance journal_;
    HistoryMetrics history_;
    std::uint64_t committed_frame_bytes_=0, logical_snapshot_bytes_=0;
};
} // namespace tradecopier::execution

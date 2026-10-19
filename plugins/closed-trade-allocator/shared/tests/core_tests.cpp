#include "closedalloc/core.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace closedalloc;
int checks = 0;

void check(bool condition, const std::string& description) {
  ++checks;
  if (!condition) throw std::runtime_error(description);
}

template<class F> void throws(F&& operation, const std::string& description) {
  bool rejected = false;
  try { operation(); } catch (const std::exception&) { rejected = true; }
  check(rejected, description);
}

struct Directory {
  std::filesystem::path path;
  Directory() {
    static unsigned sequence = 0;
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("closedalloc-test-" + std::to_string(tick) + "-" + std::to_string(++sequence));
    std::filesystem::create_directory(path);
  }
  ~Directory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  std::string journal() const { return (path / "allocator.journal").string(); }
};

struct Random {
  std::size_t calls = 0;
  std::uint64_t next = 0;
  Engine::Random callback() {
    return [this] { ++calls; return calls == 1 ? 0x123456789abcdef0ULL : next; };
  }
};

Config config() {
  Config value;
  value.platform = "mt4";
  value.server = "test-server";
  value.source = 1001;
  value.target = 2002;
  value.enabled_since = 100;
  value.armed = true;
  return value;
}

Trade trade(const std::string& id, double profit = 10, std::int64_t closed_at = 200) {
  Trade value;
  value.id = id;
  value.closed_at = closed_at;
  value.profit = profit;
  value.payload = "adapter-v1:immutable-payload";
  return value;
}

const Job& job(const Engine& engine, const std::string& id) {
  for (const auto& entry : engine.jobs())
    if (entry.second.trade.id == id) return entry.second;
  throw std::runtime_error("job not found: " + id);
}

struct FakePort : Port {
  unsigned submissions = 0, reconciliations = 0;
  std::vector<Job> submitted, reconciled;
  std::function<Result(const Job&, const SaveProgress&)> on_submit;
  std::function<Result(const Job&)> on_reconcile;
  Result submit(const Job& value, const SaveProgress& save) override {
    ++submissions;
    submitted.push_back(value);
    if (on_submit) return on_submit(value, save);
    Progress progress{{7000 + submissions}, "verified-history-and-balance"};
    save(progress);
    return {Outcome::completed, progress, "verified"};
  }
  Result reconcile(const Job& value) override {
    ++reconciliations;
    reconciled.push_back(value);
    if (on_reconcile) return on_reconcile(value);
    return {Outcome::review, value.progress, "insufficient evidence"};
  }
};

void probability_boundaries() {
  Directory directory;
  Random random;
  auto cfg = config();
  cfg.loss_probability = 0;
  cfg.profit_probability = 1;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  random.next = 0;
  engine.observe(trade("loss-zero", -10), 201);
  random.next = std::numeric_limits<std::uint64_t>::max();
  engine.observe(trade("profit-one", 10), 201);
  check(job(engine, "loss-zero").state == State::skipped, "zero percent rejects even zero draw");
  check(job(engine, "profit-one").state == State::selected, "100 percent accepts maximum draw");
  check(job(engine, "profit-one").draw < 1, "uniform draw is always below one");
  cfg.profit_probability = 0.5;
  engine.configure(cfg, 202);
  random.next = (std::uint64_t{1} << 63);
  engine.observe(trade("equal-threshold"), 203);
  random.next = (std::uint64_t{1} << 63) - 2048;
  engine.observe(trade("below-threshold"), 203);
  check(job(engine, "equal-threshold").draw == 0.5, "fixed RNG can reproduce exact threshold");
  check(job(engine, "equal-threshold").state == State::skipped, "u equal to probability is excluded");
  check(job(engine, "below-threshold").state == State::selected, "u below probability is selected");
  const auto stats = engine.stats();
  check(stats.loss_candidates == 1 && stats.loss_selected == 0, "loss statistics count decisions separately");
  check(stats.profit_candidates == 3 && stats.profit_selected == 2, "profit statistics count decisions separately");
  check(random.calls == 5, "one seed plus exactly one draw per eligible nonflat source trade");
}

void net_currency_rounding_and_exclusions() {
  Directory directory;
  Random random;
  auto cfg = config();
  cfg.loss_probability = 1;
  cfg.profit_probability = 0;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  auto after_fees = trade("fees-make-loss", 2);
  after_fees.commission = -1;
  after_fees.swap = -0.75;
  after_fees.fee = -0.5;
  check(net_profit(after_fees) == -0.25, "signed profit, commission, swap and fee are summed exactly once");
  engine.observe(after_fees, 201);
  check(job(engine, after_fees.id).probability == 1 && job(engine, after_fees.id).state == State::selected,
        "positive gross profit with negative net uses loss probability");
  engine.observe(trade("flat-after-rounding", 0.004), 201);
  engine.observe(trade("negative-flat-after-rounding", -0.004), 201);
  engine.observe(trade("one-cent-loss", -0.006), 201);
  auto zero_digits = trade("zero-digit-flat", 0.49);
  zero_digits.currency_digits = 0;
  engine.observe(zero_digits, 201);
  auto three_digits = trade("three-digit-profit", 0.001);
  three_digits.currency_digits = 3;
  engine.observe(three_digits, 201);
  auto partial = trade("partial-excluded", -100);
  partial.exclusion = "partial close unsupported";
  engine.observe(partial, 201);
  check(job(engine, "flat-after-rounding").state == State::skipped &&
        job(engine, "negative-flat-after-rounding").state == State::skipped &&
        job(engine, "zero-digit-flat").state == State::skipped, "currency-normalized zero trades skip sampling");
  check(job(engine, "one-cent-loss").state == State::selected, "currency-normalized negative trade is eligible");
  check(job(engine, "three-digit-profit").probability == 0, "currency precision controls flat threshold");
  check(job(engine, "partial-excluded").state == State::skipped, "adapter partial-close exclusion is honored");
  check(random.calls == 4, "flat and explicitly excluded trades do not consume random draws");
  const auto stats = engine.stats();
  check(stats.profit_candidates == 1 && stats.loss_candidates == 2, "excluded and flat records are not candidates");
}

void durable_decisions_and_payload() {
  Directory directory;
  Random random;
  auto cfg = config();
  cfg.profit_probability = 1;
  cfg.loss_probability = 0;
  Encoder snapshot;
  snapshot.text("mt4-closed-history-v1");
  snapshot.real(1.08765); // SL
  snapshot.real(1.125);   // TP
  snapshot.text("");     // target Comment intentionally empty
  snapshot.text(std::string("opaque\0bytes\n", 13));
  auto source = trade("source-with-sltp");
  source.payload = snapshot.data();
  std::string saved_key;
  std::uint64_t saved_tag = 0;
  double saved_draw = 0;
  {
    Engine engine(directory.journal(), cfg, 150, random.callback());
    engine.observe(source, 201);
    engine.observe(trade("permanent-skip", -10), 201);
    const auto& chosen = job(engine, source.id);
    saved_key = chosen.key;
    saved_tag = chosen.tag;
    saved_draw = chosen.draw;
    const auto draws = random.calls;
    cfg.profit_probability = 0;
    cfg.loss_probability = 1;
    engine.configure(cfg, 202);
    for (int repeat = 0; repeat < 5; ++repeat) {
      engine.observe(source, 203 + repeat);
      engine.observe(trade("permanent-skip", -10), 203 + repeat);
    }
    check(random.calls == draws && engine.jobs().size() == 2, "repeated events and probability edits never reroll or duplicate");
    check(job(engine, source.id).probability == 1 && job(engine, "permanent-skip").probability == 0,
          "decisions retain their original probabilities");
  }
  unsigned replay_random_calls = 0;
  Engine recovered(directory.journal(), cfg, 300, [&] { ++replay_random_calls; return 0; });
  recovered.observe(source, 301);
  recovered.observe(trade("permanent-skip", -10), 301);
  const auto& restored = job(recovered, source.id);
  check(replay_random_calls == 0, "restarting an existing ledger does not consume RNG or resample");
  check(restored.key == saved_key && restored.tag == saved_tag && restored.draw == saved_draw,
        "durable identity, API tag and draw survive restart");
  check(restored.trade.payload == source.payload, "opaque source snapshot including SL, TP and empty comment survives restart");
  Decoder decoder(restored.trade.payload);
  check(decoder.text() == "mt4-closed-history-v1" && decoder.real() == 1.08765 &&
        decoder.real() == 1.125 && decoder.text().empty(), "SL and TP are preserved without a target comment");
  check(decoder.text() == std::string("opaque\0bytes\n", 13), "binary payload bytes round-trip intact");
  decoder.finish();
  FakePort port;
  recovered.pump(port);
  recovered.pump(port);
  check(port.submissions == 1 && port.submitted.front().trade.payload == source.payload,
        "only selected history reaches adapter once with unchanged snapshot");
  check(job(recovered, source.id).state == State::completed && recovered.stats().completed == 1,
        "adapter-verified result becomes completed");
}

void uncertain_write_recovery() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Random random;
  Progress durable{{99123}, "before-next-side-effect"};
  {
    Engine engine(directory.journal(), cfg, 150, random.callback());
    engine.observe(trade("lost-receipt"), 201);
    FakePort port;
    port.on_submit = [&](const Job& value, const SaveProgress& save) -> Result {
      check(value.state == State::writing, "adapter is called only after durable Writing state");
      save(durable);
      throw std::runtime_error("simulated disconnect after target accepted history");
    };
    try { engine.pump(port); } catch (const std::exception&) {}
    check(port.submissions == 1, "uncertain first write has only one submit attempt");
  }
  cfg.enabled = false;
  cfg.armed = false;
  Engine recovered(directory.journal(), cfg, 300);
  FakePort port;
  port.on_reconcile = [&](const Job& value) -> Result {
    check(value.progress.target_ids == durable.target_ids && value.progress.data == durable.data,
          "reconciliation receives saved external IDs and per-step evidence after restart");
    return {Outcome::completed, value.progress, "target history and balance verified"};
  };
  recovered.pump(port);
  recovered.pump(port);
  check(port.reconciliations == 1 && port.submissions == 0,
        "uncertain writes reconcile while paused/disarmed and never blindly resubmit");
  check(job(recovered, "lost-receipt").state == State::completed, "positive recovery evidence closes the original job");
}

void unknown_outcomes_never_resubmit() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Engine engine(directory.journal(), cfg, 150);
  engine.observe(trade("uncertain"), 201);
  FakePort port;
  port.on_submit = [](const Job&, const SaveProgress& save) {
    Progress progress{{808}, "partial-target-history"};
    save(progress);
    return Result{Outcome::review, progress, "target balance is unresolved"};
  };
  engine.pump(port);
  engine.pump(port);
  engine.pump(port);
  check(port.submissions == 1 && port.reconciliations >= 1, "review loops query evidence without resubmitting");
  check(job(engine, "uncertain").state == State::review, "unresolved target evidence remains visible for review");
}

void positively_unwritten_can_retry() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Random random;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  engine.observe(trade("refused-before-write"), 201);
  FakePort port;
  port.on_submit = [&](const Job& value, const SaveProgress&) {
    if (port.submissions == 1) return Result{Outcome::not_written, {}, "confirmed preflight rejection"};
    return Result{Outcome::completed, {{900}, "verified"}, "verified"};
  };
  engine.pump(port);
  check(port.submissions == 1, "not-written result cannot create a retry loop in one pump");
  engine.pump(port);
  check(port.submissions == 2 && job(engine, "refused-before-write").state == State::completed,
        "positive proof of no external writes permits a later retry");
  check(random.calls == 2 && port.submitted[0].tag == port.submitted[1].tag,
        "safe retry uses the original draw and durable API tag");
}

void unresolved_route_serializes_balance_effects() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Engine engine(directory.journal(), cfg, 150);
  engine.observe(trade("a"), 201);
  engine.observe(trade("b"), 201);
  FakePort port;
  port.on_submit = [](const Job& value, const SaveProgress& save) {
    if (value.trade.id == "a") {
      Progress evidence{{10001}, "history-written-balance-unresolved"};
      save(evidence);
      return Result{Outcome::review, evidence, "awaiting reliable balance evidence"};
    }
    return Result{Outcome::completed, {{10002}, "complete"}, "verified"};
  };
  engine.pump(port);
  check(port.submissions == 1 && job(engine, "a").state == State::review &&
        job(engine, "b").state == State::selected,
        "one uncertain result blocks other selected writes during the same pump");
  engine.pump(port);
  check(port.reconciliations == 1 && port.submissions == 1,
        "pre-existing review is reconciled before admitting another target balance change");
  std::vector<std::string> sequence;
  port.on_reconcile = [&](const Job& value) {
    sequence.push_back("reconcile");
    return Result{Outcome::completed, value.progress, "target fully verified"};
  };
  port.on_submit = [&](const Job&, const SaveProgress&) {
    sequence.push_back("submit");
    return Result{Outcome::completed, {{10002}, "complete"}, "verified"};
  };
  engine.pump(port);
  check(sequence == std::vector<std::string>{"reconcile", "submit"},
        "successful reconciliation precedes the next selected allocation");
  check(engine.stats().completed == 2 && port.submissions == 2, "verified route resumes without duplicating earlier writes");
}

void not_written_cannot_discard_target_evidence() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Engine engine(directory.journal(), cfg, 150);
  engine.observe(trade("a"), 201);
  engine.observe(trade("b"), 201);
  FakePort port;
  port.on_submit = [](const Job&, const SaveProgress& save) {
    save({{50001}, "history-was-created"});
    return Result{Outcome::not_written, {}, "contradictory adapter result"};
  };
  engine.pump(port);
  check(port.submissions == 1 && job(engine, "a").state == State::review,
        "not-written result with persisted target IDs is rejected and blocks further submissions");
  check(job(engine, "a").progress.target_ids == std::vector<std::uint64_t>{50001},
        "contradictory result cannot erase durable target record evidence");
  port.on_reconcile = [](const Job& value) {
    return Result{Outcome::not_written, value.progress, "still contradictory"};
  };
  engine.pump(port);
  check(port.submissions == 1 && job(engine, "a").state == State::review,
        "reconciliation cannot authorize retry while target IDs exist");
}

void later_progress_cannot_erase_receipts() {
  for (const bool via_checkpoint : {false, true}) {
    Directory directory;
    auto cfg = config();
    cfg.profit_probability = 1;
    Engine engine(directory.journal(), cfg, 150);
    engine.observe(trade("existing-receipt"), 201);
    FakePort port;
    port.on_submit = [&](const Job&, const SaveProgress& save) {
      save({{51001, 51002}, "two-target-records-already-created"});
      if (via_checkpoint) save({{51002}, "later-checkpoint-omits-first-receipt"});
      return Result{Outcome::not_written, {{}, "nonempty-adapter-diagnostic"}, "inconsistent adapter response"};
    };
    engine.pump(port);
    const auto& value = job(engine, "existing-receipt");
    check(value.state == State::review && value.progress.target_ids == std::vector<std::uint64_t>{51001, 51002},
          via_checkpoint ? "later checkpoint cannot remove previously persisted receipts" :
                           "nonempty result metadata cannot erase previously persisted target IDs");
    engine.pump(port);
    check(port.submissions == 1, "erased receipt claims cannot trigger duplicate external writes");
  }
}

void cross_entity_receipts_preserve_order_and_duplicates() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  const std::vector<std::uint64_t> receipts{50001, 50002, 50001, 50002};
  {
    Engine engine(directory.journal(), cfg, 150);
    engine.observe(trade("mt5-overlapping-ticket-spaces"), 201);
    FakePort port;
    port.on_submit = [&](const Job&, const SaveProgress& save) {
      save({{50001, 50002}, "history-orders"});
      save({receipts, "history-orders-and-deals"});
      save({{50001, 50002}, "shorter-later-checkpoint"});
      return Result{Outcome::review, {{50001}, "shorter-result"}, "balance pending"};
    };
    engine.pump(port);
    check(job(engine, "mt5-overlapping-ticket-spaces").progress.target_ids == receipts,
          "order and deal namespaces may have equal tickets; positional receipts retain duplicates");
  }
  Engine recovered(directory.journal(), cfg, 300);
  FakePort port;
  port.on_reconcile = [&](const Job& value) {
    check(value.progress.target_ids == receipts, "full ordered receipt sequence survives restart and shorter progress updates");
    return Result{Outcome::completed, value.progress, "verified"};
  };
  recovered.pump(port);
  check(port.submissions == 0 && port.reconciliations == 1,
        "duplicate numeric receipt IDs across entities do not cause a new submission");
}

void proven_preflight_failure_allows_fresh_evidence() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Random random;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  engine.observe(trade("preflight-only"), 201);
  FakePort port;
  port.on_submit = [&](const Job& value, const SaveProgress& save) {
    if (port.submissions == 1) {
      save({{}, "preflight-phase-zero-no-external-call"});
      return Result{Outcome::not_written, {}, "positive proof that no write was attempted"};
    }
    check(value.progress.target_ids.empty() && value.progress.data.empty(),
          "proven preflight-only failure permits the next attempt to gather fresh balance evidence");
    return Result{Outcome::completed, {{60001}, "verified-new-preflight"}, "complete"};
  };
  engine.pump(port);
  check(job(engine, "preflight-only").state == State::selected &&
        job(engine, "preflight-only").progress.data.empty(),
        "active preflight evidence is cleared only after no-write proof and absence of receipts");
  engine.pump(port);
  check(port.submissions == 2 && random.calls == 2 &&
        port.submitted[0].tag == port.submitted[1].tag && port.submitted[0].draw == port.submitted[1].draw,
        "fresh preflight does not change the original selection, tag or draw");
}

void pump_submit_budget_counts_failures_and_rotates() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Random random;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  for (const auto* id : {"a", "b", "c", "d", "e"}) engine.observe(trade(id), 201);
  FakePort port;
  port.on_submit = [](const Job& value, const SaveProgress&) {
    if (value.trade.id == "a") return Result{Outcome::not_written, {}, "persistent preflight refusal"};
    return Result{Outcome::completed, {}, "verified"};
  };
  engine.pump(port, {2, 1});
  check(port.submissions == 2 && port.submitted[0].trade.id == "a" && port.submitted[1].trade.id == "b",
        "submit budget counts failed and successful attempts equally");
  engine.pump(port, {2, 1});
  check(port.submissions == 4 && port.submitted[2].trade.id == "c" && port.submitted[3].trade.id == "d",
        "persistent head refusal cannot starve later selected trades");
  engine.pump(port, {2, 1});
  check(port.submissions == 6 && port.submitted[4].trade.id == "e" && port.submitted[5].trade.id == "a",
        "finite submit budget wraps fairly after every pending job gets a turn");
  check(engine.stats().completed == 4 && job(engine, "a").state == State::selected && random.calls == 6,
        "bounded retries preserve original sampling and allow unrelated completed results");
  check(port.submitted.front().tag == port.submitted.back().tag,
        "round-robin scheduling never changes durable retry identity");
}

void pump_zero_budget_and_unlimited_default() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Engine engine(directory.journal(), cfg, 150);
  for (const auto* id : {"a", "b", "c"}) engine.observe(trade(id), 201);
  FakePort port;
  engine.pump(port, {0, 0});
  check(port.submissions == 0 && port.reconciliations == 0 && engine.stats().selected == 3,
        "zero budgets cause no adapter calls or writing transitions");
  engine.pump(port);
  check(port.submissions == 3 && engine.stats().completed == 3,
        "omitted budgets retain unlimited default processing for existing callers");
}

void pump_budget_preserves_review_isolation_and_next_turn_retry() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Engine engine(directory.journal(), cfg, 150);
  engine.observe(trade("a"), 201);
  engine.observe(trade("b"), 201);
  FakePort port;
  port.on_submit = [](const Job&, const SaveProgress&) {
    return Result{Outcome::review, {}, "write outcome unknown"};
  };
  engine.pump(port, {1, 1});
  check(port.submissions == 1 && job(engine, "b").state == State::selected,
        "new review result halts the route within the bounded pump");
  engine.pump(port, {1, 0});
  check(port.submissions == 1 && port.reconciliations == 0,
        "zero reconcile budget never bypasses an unresolved chosen write");
  port.on_reconcile = [](const Job& value) {
    return Result{Outcome::not_written, value.progress, "positive proof that external write never occurred"};
  };
  port.on_submit = [](const Job&, const SaveProgress&) {
    return Result{Outcome::completed, {}, "verified"};
  };
  engine.pump(port, {1, 1});
  check(port.reconciliations == 1 && port.submissions == 2 && port.submitted.back().trade.id == "b" &&
        job(engine, "a").state == State::selected,
        "freshly reconciled not-written job waits for the next pump while earlier selected work may run");
  engine.pump(port, {1, 0});
  check(port.submissions == 3 && port.submitted.back().trade.id == "a" && engine.stats().completed == 2,
        "next pump may safely retry the original reconciled job under its submit budget");
}

// Recovery fixture for ledgers produced before whole-route serialization: an
// earlier version could leave several jobs uncertain. Use the existing framed
// schema rather than mutating live engine state, so this also checks compatibility.
void append_legacy_review(const std::string& path, const Job& job) {
  Encoder e;
  e.text("job-1"); e.text(job.key); e.u64(job.tag);
  const auto& t = job.trade;
  e.text(t.id); e.i64(t.closed_at); e.real(t.profit); e.real(t.commission);
  e.real(t.swap); e.real(t.fee); e.u64(t.currency_digits); e.text(t.payload); e.text(t.exclusion);
  e.real(job.net); e.real(job.probability); e.real(job.draw);
  e.u64(job.admitted); e.u64(job.chosen); e.u64(job.source_changed);
  e.u64(static_cast<unsigned>(State::review)); e.i64(job.decided_at);
  e.text("legacy unresolved write"); e.u64(0); e.text("");
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : e.data()) { hash ^= byte; hash *= 1099511628211ULL; }
  std::ofstream output(path, std::ios::binary | std::ios::app);
  output << "CLA1 " << e.data().size() << ' ' << hash << '\n' << e.data() << '\n';
  output.close();
  check(static_cast<bool>(output), "legacy recovery fixture appended successfully");
}

void pump_reconcile_budget_is_fair_across_recovered_jobs() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  std::vector<Job> previous;
  {
    Engine engine(directory.journal(), cfg, 150);
    for (const auto* id : {"a", "b", "c", "d"}) engine.observe(trade(id), 201);
    for (const auto* id : {"a", "b", "c"}) previous.push_back(job(engine, id));
  }
  for (const auto& value : previous) append_legacy_review(directory.journal(), value);
  Engine engine(directory.journal(), cfg, 300);
  FakePort port;
  bool finish_a = false;
  port.on_reconcile = [&](const Job& value) -> Result {
    if (value.trade.id == "a" && !finish_a) throw std::runtime_error("temporary target read failure");
    if (value.trade.id == "c") return {Outcome::not_written, {}, "preflight only"};
    return {Outcome::completed, value.progress, "verified"};
  };
  engine.pump(port, {1, 1});
  check(port.reconciliations == 1 && port.submissions == 0 && port.reconciled.back().trade.id == "a",
        "failed reconciliation consumes its budget and keeps all new writes blocked");
  engine.pump(port, {1, 1});
  check(port.reconciliations == 2 && port.submissions == 0 && port.reconciled.back().trade.id == "b",
        "a repeatedly failing review cannot starve the next recovered uncertain job");
  engine.pump(port, {1, 1});
  check(port.reconciliations == 3 && port.submissions == 0 && port.reconciled.back().trade.id == "c",
        "unvisited or still unresolved route jobs block new writes after budget exhaustion");
  finish_a = true;
  engine.pump(port, {1, 1});
  check(port.reconciliations == 4 && port.reconciled.back().trade.id == "a" && port.submissions == 1,
        "reconciliation cursor wraps and submissions resume only after every uncertain job is resolved");
  engine.pump(port, {1, 0});
  check(port.submissions == 2 && engine.stats().completed == 4,
        "bounded processing completes legacy uncertain jobs and untouched selected work exactly once");
}

void paused_and_armed_gates() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  cfg.armed = false;
  Random random;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  engine.observe(trade("waiting-for-arm", 10, 190), 201);
  FakePort port;
  engine.pump(port);
  check(port.submissions == 0 && job(engine, "waiting-for-arm").state == State::selected,
        "disarmed mode records decisions without submitting");
  cfg.enabled = false;
  cfg.armed = true;
  engine.configure(cfg, 210);
  engine.observe(trade("during-pause", 10, 211), 212);
  engine.pump(port);
  check(port.submissions == 0, "disabled gate prevents even previously selected writes");
  cfg.enabled = true;
  engine.configure(cfg, 220);
  engine.observe(trade("pause-start", 10, 210), 221);
  engine.observe(trade("pause-end", 10, 220), 221);
  engine.observe(trade("missed-in-pause", 10, 215), 221);
  engine.observe(trade("after-pause", 10, 221), 222);
  for (const auto* id : {"during-pause", "pause-start", "pause-end", "missed-in-pause"})
    check(job(engine, id).state == State::skipped, std::string("pause interval skipped permanently: ") + id);
  engine.pump(port);
  check(port.submissions == 2, "resuming submits preserved earlier choice and new post-pause trade");
  check(random.calls == 3, "pause exclusions do not draw random values");
}

void startup_and_pause_boundaries_survive_restart() {
  Directory directory;
  auto cfg = config();
  cfg.enabled_since = 0;
  cfg.profit_probability = 1;
  {
    Engine engine(directory.journal(), cfg, 1000);
    check(engine.scan_from() == 1000, "zero enable timestamp freezes first startup boundary");
    engine.observe(trade("at-start", 10, 1000), 1001);
    engine.observe(trade("pre-start", 10, 999), 1001);
    check(job(engine, "at-start").state == State::skipped && job(engine, "pre-start").state == State::skipped,
          "startup second and earlier history are excluded conservatively");
    cfg.enabled = false;
    engine.configure(cfg, 1010);
  }
  {
    Engine engine(directory.journal(), cfg, 1020);
    check(engine.scan_from() == 1000, "restarts preserve original startup boundary");
    cfg.enabled = true;
    engine.configure(cfg, 1030);
  }
  Engine engine(directory.journal(), cfg, 1040);
  engine.observe(trade("pause-across-restart", 10, 1025), 1041);
  engine.observe(trade("normal-downtime", 10, 1035), 1041);
  check(job(engine, "pause-across-restart").state == State::skipped, "paused downtime stays excluded after reopening");
  check(job(engine, "normal-downtime").state == State::selected, "normal enabled downtime is eligible for backfill");
}

void history_revision_isolated() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Random random;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  auto source = trade("revised-history");
  engine.observe(source, 201);
  source.payload += ":changed-sl";
  source.profit = -100;
  engine.observe(source, 202);
  FakePort port;
  engine.pump(port);
  const auto& value = job(engine, source.id);
  check(port.submissions == 0 && value.state == State::review, "changed source history blocks an unsubmitted allocation for review");
  check(value.trade.profit == 10 && value.trade.payload == "adapter-v1:immutable-payload",
        "source revision cannot overwrite frozen original snapshot");
  check(random.calls == 2, "source revision never gets a second draw");
}

void source_lookup_and_selected_invalidation() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  Random random;
  Engine engine(directory.journal(), cfg, 150, random.callback());
  const auto empty_size = std::filesystem::file_size(directory.journal());
  engine.invalidate_source("unknown", "deleted before discovery");
  check(engine.find_trade("unknown") == nullptr && engine.find_trade("") == nullptr && engine.jobs().empty() &&
        std::filesystem::file_size(directory.journal()) == empty_size && random.calls == 1,
        "unknown source invalidation creates no job, journal event or random draw");
  auto original = trade("selected-source");
  original.commission = -1.25;
  original.swap = -0.5;
  engine.observe(original, 201);
  const Job* pointer = engine.find_trade(original.id);
  check(pointer != nullptr && pointer->state == State::selected, "find_trade returns the exact route-local selected job");
  const Job frozen = *pointer;
  engine.observe(trade("later-source"), 202);
  check(engine.find_trade(original.id) == pointer, "source lookup pointer stays valid when other jobs are inserted");
  engine.invalidate_source(original.id, "close timestamp changed outside the valid observation interval");
  check(engine.find_trade(original.id) == pointer && pointer->source_changed && pointer->state == State::review,
        "invalidation preserves object identity and isolates a selected source without a valid new timestamp");
  check(pointer->trade.payload == frozen.trade.payload && pointer->trade.closed_at == frozen.trade.closed_at &&
        pointer->trade.profit == frozen.trade.profit && pointer->trade.commission == frozen.trade.commission &&
        pointer->net == frozen.net && pointer->probability == frozen.probability && pointer->draw == frozen.draw &&
        pointer->tag == frozen.tag && pointer->decided_at == frozen.decided_at,
        "selected invalidation retains frozen source data, decision, probability, random value and API tag");
  const auto invalidated_size = std::filesystem::file_size(directory.journal());
  const auto first_reason = pointer->reason;
  engine.invalidate_source(original.id, "later duplicate notification");
  check(pointer->reason == first_reason && first_reason ==
        "source history changed: close timestamp changed outside the valid observation interval" &&
        std::filesystem::file_size(directory.journal()) == invalidated_size,
        "repeated invalidation retains the original reason and does not append duplicate journal records");
  FakePort port;
  engine.pump(port, {1, 1});
  check(port.submissions == 0 && port.reconciliations == 0 && job(engine, "later-source").state == State::selected,
        "invalidated selected source blocks all subsequent submissions without adapter resubmission");
}

void completed_source_invalidation_preserves_receipts_after_restart() {
  Directory directory;
  auto cfg = config();
  cfg.profit_probability = 1;
  const Progress evidence{{70001, 70002, 70001, 70002}, "complete-history-and-balance-evidence"};
  Job frozen;
  {
    Engine engine(directory.journal(), cfg, 150);
    engine.observe(trade("completed-source"), 201);
    const Job* pointer = engine.find_trade("completed-source");
    FakePort port;
    port.on_submit = [&](const Job&, const SaveProgress& save) {
      save(evidence);
      return Result{Outcome::completed, evidence, "fully verified"};
    };
    engine.pump(port);
    check(pointer == engine.find_trade("completed-source") && pointer->state == State::completed,
          "lookup pointer remains valid after the selected job completes");
    frozen = *pointer;
    engine.observe(trade("following-source"), 202);
    engine.invalidate_source("completed-source", "source deal deleted");
    check(pointer->state == State::review && pointer->source_changed &&
          pointer->progress.target_ids == evidence.target_ids && pointer->progress.data == evidence.data,
          "completed source invalidation preserves every ordered target receipt and balance checkpoint");
  }
  unsigned draws = 0;
  Engine recovered(directory.journal(), cfg, 300, [&] { ++draws; return 0; });
  const auto* restored = recovered.find_trade("completed-source");
  check(restored && restored->source_changed && restored->state == State::review &&
        restored->reason == "source history changed: source deal deleted",
        "explicit source invalidation and its reason survive ledger recovery");
  check(restored->trade.payload == frozen.trade.payload && restored->trade.closed_at == frozen.trade.closed_at &&
        restored->tag == frozen.tag && restored->draw == frozen.draw && restored->probability == frozen.probability &&
        restored->net == frozen.net && restored->chosen == frozen.chosen &&
        restored->progress.target_ids == evidence.target_ids && restored->progress.data == evidence.data && draws == 0,
        "recovery retains original source, draw and target evidence without resampling");
  FakePort port;
  recovered.pump(port, {1, 1});
  check(port.submissions == 0 && port.reconciliations == 0 &&
        recovered.find_trade("following-source")->state == State::selected,
        "recovered invalidation continues whole-route isolation and never replays completed side effects");
}

void route_identity_and_exclusive_lock() {
  Directory directory;
  auto cfg = config();
  {
    Engine engine(directory.journal(), cfg, 150);
    throws([&] { Engine duplicate(directory.journal(), cfg, 151); }, "concurrent engines cannot own the same ledger");
    auto other = cfg;
    other.target = 2003;
    throws([&] { engine.configure(other, 152); }, "live configuration cannot change the durable route identity");
  }
  auto other = cfg;
  other.source = 1002;
  throws([&] { Engine wrong_route(directory.journal(), other, 200); }, "ledger cannot be repurposed for another source");
  other = cfg;
  other.target = 2003;
  throws([&] { Engine wrong_route(directory.journal(), other, 200); }, "ledger cannot be repurposed for another target");
  other = cfg;
  other.server = "another-server";
  throws([&] { Engine wrong_route(directory.journal(), other, 200); }, "ledger cannot be repurposed for another server");
  Engine valid(directory.journal(), cfg, 200);
  check(valid.jobs().empty(), "rejected identity opens do not damage the original ledger");
}

void invalid_configuration_and_codec() {
  auto cfg = config();
  for (const double value : {-0.01, 1.01, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
    auto bad = cfg;
    bad.profit_probability = value;
    throws([&] { validate(bad); }, "invalid profit probability rejected");
    bad = cfg;
    bad.loss_probability = value;
    throws([&] { validate(bad); }, "invalid loss probability rejected");
  }
  auto bad = cfg;
  bad.target = bad.source;
  throws([&] { validate(bad); }, "self allocation is rejected");
  bad = cfg;
  bad.source = 0;
  throws([&] { validate(bad); }, "zero source account is rejected");
  bad = cfg;
  bad.server.clear();
  throws([&] { validate(bad); }, "missing server identity is rejected");
  Encoder encoder;
  encoder.u64(std::numeric_limits<std::uint64_t>::max());
  encoder.i64(std::numeric_limits<std::int64_t>::min());
  encoder.real(1.2345678901234567);
  Decoder decoder(encoder.data());
  check(decoder.u64() == std::numeric_limits<std::uint64_t>::max() &&
        decoder.i64() == std::numeric_limits<std::int64_t>::min() &&
        decoder.real() == 1.2345678901234567, "strict codec preserves integer and double boundaries");
  decoder.finish();
  std::string incomplete = "999:short";
  throws([&] { Decoder malformed(incomplete); (void)malformed.text(); }, "truncated length-prefixed payload rejected");
  const auto trailing = encoder.data() + "junk";
  throws([&] { Decoder malformed(trailing); malformed.u64(); malformed.i64(); malformed.real(); malformed.finish(); },
         "codec rejects trailing bytes");
}

void ledger_corruption_is_rejected() {
  Directory directory;
  auto cfg = config();
  {
    Engine engine(directory.journal(), cfg, 150);
    engine.observe(trade("persisted"), 201);
  }
  std::ifstream original(directory.journal(), std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>());
  check(bytes.size() > 16, "test journal contains persisted records");
  auto copy = [&](const std::string& filename, std::string data) {
    const auto path = (directory.path / filename).string();
    std::ofstream output(path, std::ios::binary);
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.close();
    return path;
  };
  auto truncated = copy("truncated.journal", bytes.substr(0, bytes.size() - 1));
  throws([&] { Engine broken(truncated, cfg, 300); }, "truncated durable journal must fail closed");
  auto changed = bytes;
  changed[changed.size() / 2] ^= 1;
  auto corrupted = copy("corrupted.journal", changed);
  throws([&] { Engine broken(corrupted, cfg, 300); }, "corrupt durable journal must fail closed");
  auto appended = copy("garbage.journal", bytes + "unexpected trailing bytes");
  throws([&] { Engine broken(appended, cfg, 300); }, "garbage appended to durable journal must fail closed");
  Engine intact(directory.journal(), cfg, 300);
  check(intact.jobs().size() == 1, "valid original ledger remains readable");
}

} // namespace

int main() {
  const std::vector<std::pair<const char*, void(*)()>> tests{
    {"probability_boundaries", probability_boundaries},
    {"net_currency_rounding_and_exclusions", net_currency_rounding_and_exclusions},
    {"durable_decisions_and_payload", durable_decisions_and_payload},
    {"uncertain_write_recovery", uncertain_write_recovery},
    {"unknown_outcomes_never_resubmit", unknown_outcomes_never_resubmit},
    {"positively_unwritten_can_retry", positively_unwritten_can_retry},
    {"unresolved_route_serializes_balance_effects", unresolved_route_serializes_balance_effects},
    {"not_written_cannot_discard_target_evidence", not_written_cannot_discard_target_evidence},
    {"later_progress_cannot_erase_receipts", later_progress_cannot_erase_receipts},
    {"cross_entity_receipts_preserve_order_and_duplicates", cross_entity_receipts_preserve_order_and_duplicates},
    {"proven_preflight_failure_allows_fresh_evidence", proven_preflight_failure_allows_fresh_evidence},
    {"pump_submit_budget_counts_failures_and_rotates", pump_submit_budget_counts_failures_and_rotates},
    {"pump_zero_budget_and_unlimited_default", pump_zero_budget_and_unlimited_default},
    {"pump_budget_preserves_review_isolation_and_next_turn_retry", pump_budget_preserves_review_isolation_and_next_turn_retry},
    {"pump_reconcile_budget_is_fair_across_recovered_jobs", pump_reconcile_budget_is_fair_across_recovered_jobs},
    {"paused_and_armed_gates", paused_and_armed_gates},
    {"startup_and_pause_boundaries_survive_restart", startup_and_pause_boundaries_survive_restart},
    {"history_revision_isolated", history_revision_isolated},
    {"source_lookup_and_selected_invalidation", source_lookup_and_selected_invalidation},
    {"completed_source_invalidation_preserves_receipts_after_restart", completed_source_invalidation_preserves_receipts_after_restart},
    {"route_identity_and_exclusive_lock", route_identity_and_exclusive_lock},
    {"invalid_configuration_and_codec", invalid_configuration_and_codec},
    {"ledger_corruption_is_rejected", ledger_corruption_is_rejected},
  };
  unsigned failed = 0;
  for (const auto& test : tests) {
    try {
      test.second();
      std::cout << "PASS " << test.first << '\n';
    } catch (const std::exception& error) {
      ++failed;
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() << " cases, " << checks << " checks, " << failed << " failures\n";
  return failed ? 1 : 0;
}

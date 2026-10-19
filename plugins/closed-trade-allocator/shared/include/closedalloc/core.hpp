#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace closedalloc {

// All timestamps are Unix seconds. The adapter's opaque payload is an immutable
// versioned snapshot, including original SL/TP, with no target comment text.
struct Trade {
  std::string id;
  std::int64_t closed_at = 0;
  double profit = 0, commission = 0, swap = 0, fee = 0;
  unsigned currency_digits = 2;
  std::string payload;
  std::string exclusion; // Nonempty for partial closes / unsupported records.
};

struct Config {
  std::string platform; // mt4 or mt5
  std::string server;
  std::uint64_t source = 0, target = 0;
  double loss_probability = 0.30, profit_probability = 0.70;
  std::int64_t enabled_since = 0; // zero: freeze first startup time in journal
  bool enabled = true;
  bool armed = false;
};

enum class State { skipped, selected, writing, completed, review };
struct Progress {
  std::vector<std::uint64_t> target_ids;
  std::string data; // Versioned adapter evidence / per-step checkpoints.
};
struct Job {
  std::string key;
  std::uint64_t tag = 0; // Persistent random API tag, never placed in Comment.
  Trade trade;
  double net = 0, probability = 0, draw = 0;
  bool admitted = false, chosen = false, source_changed = false;
  State state = State::selected;
  std::int64_t decided_at = 0;
  std::string reason;
  Progress progress;
};

enum class Outcome { completed, not_written, review };
struct Result {
  Outcome outcome = Outcome::review;
  Progress progress;
  std::string message;
};
using SaveProgress = std::function<void(const Progress&)>;

class Port {
public:
  virtual ~Port() = default;
  // The core has durably recorded Writing before calling submit. Checkpoint
  // evidence BEFORE each external side effect, then returned IDs AFTER it.
  // A failed checkpoint throws: let that exception escape without more writes.
  virtual Result submit(const Job&, const SaveProgress&) = 0;
  // Read-only reconciliation. Never resend, fix balances or delete records.
  // not_written requires positive proof that NO external side effect occurred.
  virtual Result reconcile(const Job&) = 0;
};

struct Stats {
  std::uint64_t profit_candidates = 0, loss_candidates = 0;
  std::uint64_t profit_selected = 0, loss_selected = 0;
  std::uint64_t skipped = 0, selected = 0, writing = 0, completed = 0, review = 0;
};

struct PumpBudget {
  // Maximum adapter calls in one pump, including failures. Zero disables the
  // corresponding call type. Omitted budgets preserve unlimited processing.
  std::size_t submit = (std::numeric_limits<std::size_t>::max)();
  std::size_t reconcile = (std::numeric_limits<std::size_t>::max)();
};

// Strict length-prefixed binary codec for adapter payloads and journal frames.
// Integers are decimal strings, doubles round-trip at max_digits10 precision.
class Encoder {
public:
  void text(const std::string&);
  void u64(std::uint64_t);
  void i64(std::int64_t);
  void real(double);
  const std::string& data() const { return data_; }
private:
  std::string data_;
};
class Decoder {
public:
  explicit Decoder(const std::string& data) : data_(data) {}
  std::string text();
  std::uint64_t u64();
  std::int64_t i64();
  double real();
  void finish() const;
private:
  const std::string& data_;
  std::size_t offset_ = 0;
};

void validate(const Config&);
double net_profit(const Trade&);
const char* state_name(State);

class Engine {
public:
  using Random = std::function<std::uint64_t()>;
  // Exclusive OS file lock, durable append-only journal; errors throw and
  // prevent further submissions. Existing route identity must match exactly.
  Engine(const std::string& journal_path, Config config, std::int64_t now,
         Random random = {});
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  const Config& config() const;
  std::int64_t scan_from() const;
  // Config updates freeze past pause intervals and never reroll existing jobs.
  void configure(Config config, std::int64_t now);
  // Idempotent ingest. The adapter must provide fully settled immutable data.
  void observe(const Trade&, std::int64_t now);
  // Route-local lookup; the pointer remains valid for this Engine's lifetime,
  // although subsequent engine operations may update the pointed-to state.
  const Job* find_trade(const std::string& trade_id) const;
  // Isolate an existing source whose history is missing, reopened or otherwise
  // invalid. Preserve all frozen evidence; unknown IDs are ignored. No current
  // timestamp is required because the source may have had its times revised.
  void invalidate_source(const std::string& trade_id, const std::string& reason);
  // Recover uncertain writes even while disarmed/paused, but submit only when
  // enabled AND armed. At most one submit attempt per job per invocation.
  // Finite budgets rotate fairly between calls. Any unresolved chosen write
  // still blocks new submissions, including jobs outside the reconcile budget.
  void pump(Port&, PumpBudget budget = {});
  const std::map<std::string, Job>& jobs() const;
  Stats stats() const;
  std::string report_tsv() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace closedalloc

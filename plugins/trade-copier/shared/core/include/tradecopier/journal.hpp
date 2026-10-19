#pragma once

#include "tradecopier/executor.hpp"
#include <filesystem>
#include <memory>

namespace tradecopier::execution {

// Durations use steady-clock nanoseconds. Fields are inclusive phase totals per
// Commit: checkpoint_ns includes its write/flush/publication, so it must not be
// added to write_ns or durable_flush_ns when calculating the total. All other
// phases are disjoint; total_ns also includes bookkeeping and initialization.
struct JournalTimings {
    std::uint64_t total_ns=0;
    std::uint64_t encode_ns=0;
    std::uint64_t validation_ns=0;
    std::uint64_t delta_frame_ns=0;
    std::uint64_t write_ns=0;
    std::uint64_t durable_flush_ns=0;
    std::uint64_t checkpoint_ns=0;
    std::uint64_t state_update_ns=0;
};
struct JournalCommitSample {
    JournalTimings timings;
    std::uint64_t payload_bytes=0, record_bytes=0;
    std::uint64_t write_calls=0, durable_flush_calls=0;
    bool checkpoint=false, succeeded=false;
};
// Since this FileJournal instance was constructed. Load/replay and capacity
// checks are excluded. Failed commits (including partial stages) are included;
// last is the last completed attempt. max is the per-field maximum of a whole
// commit's phase totals, not the slowest individual WriteFile/flush system call.
// write_calls counts logical writes (one may retry/loop OS writes); flush_calls
// counts durable flush attempts. Both include lease initialization/checkpoint
// writes made by Commit. Counters saturate at UINT64_MAX. Worker-thread only.
struct JournalPerformance {
    std::uint64_t attempts=0, succeeded=0, failed=0, checkpoints=0;
    std::uint64_t write_calls=0, durable_flush_calls=0;
    JournalTimings total, max;
    JournalCommitSample last;
};

// Checksummed snapshot/delta records and a replaceable live-state checkpoint; no archives.
// The stable sidecar lease protects the writer across file replacement. A damaged/truncated
// journal refuses to load; silently dropping a final intent could duplicate a
// trade. The file is local runtime data, not a portable configuration document.
class FileJournal final : public Store {
public:
    static constexpr std::uint64_t kRotateBytes = 64ULL * 1024 * 1024;
    static constexpr std::size_t kReplayRecordLimit = 256;
    static constexpr std::size_t kMaxSnapshotBytes = 32ULL * 1024 * 1024;
    static constexpr std::size_t kOpeningSnapshotBytes = kMaxSnapshotBytes / 2;
    // Includes a frozen 4 KiB symbol mask, names/suffix, rule and request evidence.
    static constexpr std::size_t kOpeningMappingReserveBytes = 8192;
    static constexpr std::uint64_t kOpeningDiskReserveBytes = 256ULL * 1024 * 1024;
    explicit FileJournal(const std::filesystem::path&, std::uint64_t rotate_bytes = kRotateBytes);
    ~FileJournal() override;
    FileJournal(const FileJournal&) = delete;
    FileJournal& operator=(const FileJournal&) = delete;
    bool Load(State&) override;
    void Save(const State&) override;
    void Compact(const State&) override;
    void Prune(const State&) override;
    std::string OpeningBlockReason(const State&, std::size_t) override;
    std::uint64_t rotations() const;
    std::uint64_t bytes() const;
    std::uint64_t written_bytes() const;
    std::uint64_t logical_bytes() const;
    JournalPerformance performance() const noexcept;
private:
    void Commit(const State&, bool compact, bool force_checkpoint = false);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Used by the read-only journal inspection command and corruption tests.
std::string EncodeState(const State&);
State DecodeState(const std::string&);

} // namespace tradecopier::execution

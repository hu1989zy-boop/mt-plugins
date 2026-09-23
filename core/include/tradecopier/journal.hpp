#pragma once

#include "tradecopier/executor.hpp"
#include <filesystem>
#include <memory>

namespace tradecopier::execution {

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
private:
    void Commit(const State&, bool compact, bool force_checkpoint = false);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Used by the read-only journal inspection command and corruption tests.
std::string EncodeState(const State&);
State DecodeState(const std::string&);

} // namespace tradecopier::execution

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// SDK-independent policy prototype. This library neither places trades nor
// provides persistence, concurrent event processing, reconnect reconciliation,
// a server snapshot barrier, or MT5 ABI entry points. An adapter must supply
// confirmed fills and serialize events per link before this can control trades.
namespace tradecopier {

using Units = std::uint64_t;
using Id = std::uint64_t;
constexpr Units kUnitsPerLot = 100000000;
// These are business units, NOT MT5 SDK volume units. The future adapter must
// convert explicitly and validate the precision supported by its actual SDK.

struct Rational {
    std::uint64_t numerator = 1;
    std::uint64_t denominator = 1;
};

// Plain, positive decimal only; no exponent/sign/whitespace. Conversion is exact.
// The output is unchanged on failure, including arithmetic overflow.
bool ParsePositiveDecimal(const std::string& text, Rational& output);

enum class SizingMode { LotRatio, EquityRatio, FixedLots };

struct VolumePolicy {
    SizingMode mode = SizingMode::LotRatio;
    Rational factor{}; // multiplier, or equity coefficient
    Units fixed_units = 0;
};

struct VolumeLimits {
    Units minimum = 0;
    Units maximum = 0;
    Units step = 0;
};

struct OpeningInput {
    Units source_units = 0;
    // USD cents are a prototype input precision. The adapter must determine
    // actual server equity precision. EquityRatio requires both snapshots to
    // be positive: query the current source and follower equity after receiving
    // the source opening fill, before follower submission, then freeze them.
    // The adapter records the source fill time and both query times separately;
    // this is not an atomic snapshot at source execution. The core never refreshes
    // these values later. LotRatio and FixedLots do not depend on these fields.
    std::uint64_t source_equity_usd_cents = 0;
    std::uint64_t follower_equity_usd_cents = 0;
};

enum class Code {
    Ready,
    SkipBelowMinimum,
    ReviewResidualBelowMinimum,
    ReviewCloseBelowMinimum,
    ReviewUnalignedVolume,
    ExceedsMaximum,
    InvalidInput,
    ArithmeticOverflow,
    DuplicateEvent,
    NoActionTerminal,
    AwaitingExecution,
    NeedsReview
};

const char* CodeName(Code code);

struct VolumeDecision {
    Code code = Code::InvalidInput;
    Units units = 0; // Nonzero only for Ready. Never silently cap to maximum.
};

bool ValidLimits(const VolumeLimits& limits);
bool ValidPolicy(const VolumePolicy& policy);
VolumeDecision PlanOpening(const VolumePolicy& policy,
                           const OpeningInput& input,
                           const VolumeLimits& limits);

// Partial close ratio = source_closed / source_remaining_before.
// Apply that ratio to the ACTUAL follower remainder, then floor to step.
// Full source close requests the entire mapped follower remainder, subject to
// explicit volume constraints. A min/step conflict is sent for review, never
// resolved by rounding up or closing unrelated positions.
VolumeDecision PlanClosing(Units source_remaining_before,
                           Units source_closed,
                           Units actual_follower_remaining,
                           const VolumeLimits& limits);

struct Relation {
    Id id = 0;
    Id source_login = 0;
    Id follower_login = 0;
    VolumePolicy policy{};
};

enum class RelationError {
    InvalidId,
    DuplicateId,
    SelfFollow,
    DuplicatePair,
    InvalidPolicy,
    DirectedCycle
};

// Validate all configured relations, including any currently disabled ones,
// before accepting configuration. Multiple sources and followers are allowed;
// a source/follower pair must have exactly one sizing rule.
std::vector<RelationError> ValidateRelations(const std::vector<Relation>& relations);

enum class Admission {
    Accepted,
    BaselinePosition,
    BeforeActivation,
    Duplicate,
    InvalidInput
};

// One gate per relation activation epoch. The watermark is an adapter-provided
// monotonic opening-event sequence, not wall-clock time or an SDK ticket. A
// consistent baseline plus ordered event barrier is a precondition. Rebuilding
// a gate after reconnect would lose deduplication and is not a recovery design.
class ActivationGate {
public:
    ActivationGate(Id activation_watermark, const std::vector<Id>& existing_positions);
    Admission AdmitOpening(Id source_position_id, Id opening_event_sequence);

private:
    Id watermark_;
    std::unordered_set<Id> baseline_;
    std::unordered_set<Id> admitted_;
};

enum class LinkState { Active, AwaitingExecution, ManuallyClosed, Closed, NeedsReview };

// A small serialized state model for one source-position / follower-position
// mapping, initialized from confirmed fills. One source opening is one input
// here: deal aggregation and partial opening fills are adapter responsibilities.
// This is in-memory only; event IDs and mappings require durable storage in a
// production engine. Close decisions are proposals, not proof of execution.
class PositionLink {
public:
    PositionLink(Id source_position_id, Id follower_position_id,
                 Units source_remaining, Units follower_remaining);

    // Source close is already confirmed. A valid event advances its remainder.
    // Ready creates exactly one outstanding execution proposal; later events
    // are held until confirmation. Duplicate IDs never create another proposal.
    // Invalid close data/metadata or overflow freezes the link for external
    // reconciliation instead of letting a later event use stale source volume.
    VolumeDecision OnSourceClose(Id event_id, Units source_closed,
                                 const VolumeLimits& limits);

    // Called with broker-confirmed closed volume. A failure (zero) or partial
    // execution halts for reconciliation; there is no blind automatic retry.
    Code ConfirmFollowerClose(Id event_id, Units actual_closed);

    // A manual full close is terminal; future source events produce no actions.
    // A manual partial close updates the actual remainder. If an execution is
    // already in flight, or the reported volume unexpectedly increases, halt
    // for reconciliation. The adapter must separately
    // reconcile/cancel outstanding broker work; changing local state cannot do it.
    Code ObserveManualFollowerRemaining(Units actual_remaining);

    LinkState state() const { return state_; }
    Units source_remaining() const { return source_remaining_; }
    Units follower_remaining() const { return follower_remaining_; }
    Id source_position_id() const { return source_position_id_; }
    Id follower_position_id() const { return follower_position_id_; }

private:
    Id source_position_id_;
    Id follower_position_id_;
    Units source_remaining_;
    Units follower_remaining_;
    LinkState state_ = LinkState::Active;
    std::unordered_set<Id> source_close_events_;
    Id pending_event_ = 0;
    Units pending_units_ = 0;
};

} // namespace tradecopier

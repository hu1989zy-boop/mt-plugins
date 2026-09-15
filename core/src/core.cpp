#include "tradecopier/core.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <utility>

namespace tradecopier {
namespace {

bool Multiply(std::uint64_t a, std::uint64_t b, std::uint64_t& result) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) return false;
    result = a * b;
    return true;
}

// Cross-cancel before checked multiplication. If the reduced intermediate
// products still cannot fit in uint64_t, fail closed even if the mathematical
// quotient might fit. No floating point, compiler-specific integer extensions,
// or unchecked overflow are used; this is compatible with MSVC C++17.
bool FloorRatio(std::vector<std::uint64_t> numerators,
                std::vector<std::uint64_t> denominators,
                std::uint64_t& result) {
    for (auto denominator : denominators) if (denominator == 0) return false;
    for (auto& numerator : numerators) {
        for (auto& denominator : denominators) {
            const auto divisor = std::gcd(numerator, denominator);
            numerator /= divisor;
            denominator /= divisor;
        }
    }
    std::uint64_t numerator = 1;
    std::uint64_t denominator = 1;
    for (const auto factor : numerators) {
        if (!Multiply(numerator, factor, numerator)) return false;
    }
    for (const auto factor : denominators) {
        if (!Multiply(denominator, factor, denominator)) return false;
    }
    result = numerator / denominator;
    return true;
}

VolumeDecision Quantize(std::vector<std::uint64_t> numerators,
                        std::vector<std::uint64_t> denominators,
                        const VolumeLimits& limits) {
    denominators.push_back(limits.step);
    std::uint64_t steps = 0;
    if (!FloorRatio(std::move(numerators), std::move(denominators), steps))
        return {Code::ArithmeticOverflow, 0};
    Units units = 0;
    if (!Multiply(steps, limits.step, units)) return {Code::ArithmeticOverflow, 0};
    if (units > limits.maximum) return {Code::ExceedsMaximum, 0};
    if (units < limits.minimum) return {Code::SkipBelowMinimum, 0};
    return {Code::Ready, units};
}

bool Terminal(LinkState state) {
    return state == LinkState::ManuallyClosed || state == LinkState::Closed;
}

} // namespace

bool ParsePositiveDecimal(const std::string& text, Rational& output) {
    if (text.empty()) return false;
    bool dot = false;
    bool before_dot = false;
    bool after_dot = false;
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 1;
    for (const char character : text) {
        if (character == '.') {
            if (dot || !before_dot) return false;
            dot = true;
            continue;
        }
        if (character < '0' || character > '9') return false;
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (!Multiply(numerator, 10, numerator) ||
            numerator > std::numeric_limits<std::uint64_t>::max() - digit) return false;
        numerator += digit;
        if (dot) {
            after_dot = true;
            if (!Multiply(denominator, 10, denominator)) return false;
        } else {
            before_dot = true;
        }
    }
    if ((dot && !after_dot) || numerator == 0) return false;
    const auto divisor = std::gcd(numerator, denominator);
    output = {numerator / divisor, denominator / divisor};
    return true;
}

const char* CodeName(Code code) {
    switch (code) {
    case Code::Ready: return "ready";
    case Code::SkipBelowMinimum: return "skip_below_minimum";
    case Code::ReviewResidualBelowMinimum: return "review_residual_below_minimum";
    case Code::ReviewCloseBelowMinimum: return "review_close_below_minimum";
    case Code::ReviewUnalignedVolume: return "review_unaligned_volume";
    case Code::ExceedsMaximum: return "exceeds_maximum";
    case Code::InvalidInput: return "invalid_input";
    case Code::ArithmeticOverflow: return "arithmetic_overflow";
    case Code::DuplicateEvent: return "duplicate_event";
    case Code::NoActionTerminal: return "no_action_terminal";
    case Code::AwaitingExecution: return "awaiting_execution";
    case Code::NeedsReview: return "needs_review";
    }
    return "invalid_input";
}

bool ValidLimits(const VolumeLimits& limits) {
    return limits.step > 0 && limits.minimum > 0 &&
           limits.minimum <= limits.maximum &&
           limits.minimum % limits.step == 0 && limits.maximum % limits.step == 0;
}

bool ValidPolicy(const VolumePolicy& policy) {
    switch (policy.mode) {
    case SizingMode::LotRatio:
    case SizingMode::EquityRatio:
        return policy.factor.numerator > 0 && policy.factor.denominator > 0;
    case SizingMode::FixedLots:
        return policy.fixed_units > 0;
    }
    return false;
}

VolumeDecision PlanOpening(const VolumePolicy& policy,
                           const OpeningInput& input,
                           const VolumeLimits& limits) {
    if (!ValidLimits(limits) || !ValidPolicy(policy) || input.source_units == 0)
        return {Code::InvalidInput, 0};
    switch (policy.mode) {
    case SizingMode::LotRatio:
        return Quantize({input.source_units, policy.factor.numerator},
                        {policy.factor.denominator}, limits);
    case SizingMode::EquityRatio:
        if (input.source_equity_usd_cents == 0 || input.follower_equity_usd_cents == 0)
            return {Code::InvalidInput, 0};
        return Quantize({input.source_units, input.follower_equity_usd_cents,
                         policy.factor.numerator},
                        {input.source_equity_usd_cents, policy.factor.denominator}, limits);
    case SizingMode::FixedLots:
        return Quantize({policy.fixed_units}, {}, limits);
    }
    return {Code::InvalidInput, 0};
}

VolumeDecision PlanClosing(Units source_remaining_before,
                           Units source_closed,
                           Units actual_follower_remaining,
                           const VolumeLimits& limits) {
    if (!ValidLimits(limits) || source_remaining_before == 0 || source_closed == 0 ||
        source_closed > source_remaining_before || actual_follower_remaining == 0)
        return {Code::InvalidInput, 0};
    if (actual_follower_remaining % limits.step != 0)
        return {Code::ReviewUnalignedVolume, 0};
    if (source_closed == source_remaining_before) {
        if (actual_follower_remaining > limits.maximum) return {Code::ExceedsMaximum, 0};
        if (actual_follower_remaining < limits.minimum)
            return {Code::ReviewCloseBelowMinimum, 0};
        return {Code::Ready, actual_follower_remaining};
    }
    const auto decision = Quantize({actual_follower_remaining, source_closed},
                                   {source_remaining_before}, limits);
    if (decision.code != Code::Ready) return decision;
    const Units remainder = actual_follower_remaining - decision.units;
    if (remainder > 0 && remainder < limits.minimum)
        return {Code::ReviewResidualBelowMinimum, 0};
    return decision;
}

std::vector<RelationError> ValidateRelations(const std::vector<Relation>& relations) {
    std::vector<RelationError> errors;
    std::set<Id> ids;
    std::set<std::pair<Id, Id>> pairs;
    std::map<Id, std::vector<Id>> edges;
    std::map<Id, std::size_t> incoming;
    for (const auto& relation : relations) {
        if (relation.id == 0 || relation.source_login == 0 || relation.follower_login == 0)
            errors.push_back(RelationError::InvalidId);
        if (!ids.insert(relation.id).second) errors.push_back(RelationError::DuplicateId);
        if (relation.source_login == relation.follower_login)
            errors.push_back(RelationError::SelfFollow);
        if (!ValidPolicy(relation.policy)) errors.push_back(RelationError::InvalidPolicy);
        if (!pairs.insert({relation.source_login, relation.follower_login}).second) {
            errors.push_back(RelationError::DuplicatePair);
            continue;
        }
        edges[relation.source_login].push_back(relation.follower_login);
        incoming.try_emplace(relation.source_login, 0);
        ++incoming[relation.follower_login];
    }
    std::queue<Id> ready;
    for (const auto& entry : incoming) if (entry.second == 0) ready.push(entry.first);
    std::size_t visited = 0;
    while (!ready.empty()) {
        const Id source = ready.front();
        ready.pop();
        ++visited;
        const auto found = edges.find(source);
        if (found == edges.end()) continue;
        for (const Id follower : found->second) {
            if (--incoming[follower] == 0) ready.push(follower);
        }
    }
    if (visited != incoming.size()) errors.push_back(RelationError::DirectedCycle);
    return errors;
}

ActivationGate::ActivationGate(Id activation_watermark,
                               const std::vector<Id>& existing_positions)
    : watermark_(activation_watermark), baseline_(existing_positions.begin(), existing_positions.end()) {}

Admission ActivationGate::AdmitOpening(Id source_position_id, Id opening_event_sequence) {
    if (source_position_id == 0 || opening_event_sequence == 0) return Admission::InvalidInput;
    if (baseline_.count(source_position_id) != 0) return Admission::BaselinePosition;
    if (opening_event_sequence <= watermark_) {
        baseline_.insert(source_position_id);
        return Admission::BeforeActivation;
    }
    if (!admitted_.insert(source_position_id).second) return Admission::Duplicate;
    return Admission::Accepted;
}

PositionLink::PositionLink(Id source_position_id, Id follower_position_id,
                           Units source_remaining, Units follower_remaining)
    : source_position_id_(source_position_id), follower_position_id_(follower_position_id),
      source_remaining_(source_remaining), follower_remaining_(follower_remaining) {
    if (source_position_id == 0 || follower_position_id == 0 ||
        source_position_id == follower_position_id ||
        source_remaining == 0 || follower_remaining == 0) state_ = LinkState::NeedsReview;
}

VolumeDecision PositionLink::OnSourceClose(Id event_id, Units source_closed,
                                          const VolumeLimits& limits) {
    if (Terminal(state_)) return {Code::NoActionTerminal, 0};
    if (source_close_events_.count(event_id) != 0) return {Code::DuplicateEvent, 0};
    if (state_ == LinkState::AwaitingExecution) return {Code::AwaitingExecution, 0};
    if (state_ == LinkState::NeedsReview) return {Code::NeedsReview, 0};
    if (event_id == 0) {
        state_ = LinkState::NeedsReview;
        return {Code::InvalidInput, 0};
    }
    const auto decision = PlanClosing(source_remaining_, source_closed, follower_remaining_, limits);
    if (decision.code == Code::InvalidInput || decision.code == Code::ArithmeticOverflow) {
        state_ = LinkState::NeedsReview;
        return decision;
    }
    source_close_events_.insert(event_id);
    source_remaining_ -= source_closed;
    if (decision.code == Code::Ready) {
        pending_event_ = event_id;
        pending_units_ = decision.units;
        state_ = LinkState::AwaitingExecution;
    } else if (decision.code != Code::SkipBelowMinimum) {
        state_ = LinkState::NeedsReview;
    }
    return decision;
}

Code PositionLink::ConfirmFollowerClose(Id event_id, Units actual_closed) {
    if (Terminal(state_)) return Code::NoActionTerminal;
    if (state_ != LinkState::AwaitingExecution || event_id != pending_event_ ||
        actual_closed > pending_units_ || actual_closed > follower_remaining_)
        return Code::InvalidInput;
    follower_remaining_ -= actual_closed;
    if (actual_closed != pending_units_) {
        state_ = LinkState::NeedsReview;
    } else if (source_remaining_ == 0 && follower_remaining_ == 0) {
        state_ = LinkState::Closed;
    } else {
        state_ = LinkState::Active;
    }
    pending_event_ = 0;
    pending_units_ = 0;
    return state_ == LinkState::NeedsReview ? Code::NeedsReview : Code::Ready;
}

Code PositionLink::ObserveManualFollowerRemaining(Units actual_remaining) {
    if (Terminal(state_)) return Code::NoActionTerminal;
    if (actual_remaining > follower_remaining_) {
        state_ = LinkState::NeedsReview;
        return Code::InvalidInput;
    }
    const bool was_pending = state_ == LinkState::AwaitingExecution;
    follower_remaining_ = actual_remaining;
    if (actual_remaining == 0) {
        state_ = LinkState::ManuallyClosed;
        pending_event_ = 0;
        pending_units_ = 0;
    } else if (was_pending) {
        state_ = LinkState::NeedsReview;
    }
    return was_pending || state_ == LinkState::NeedsReview ? Code::NeedsReview : Code::Ready;
}

} // namespace tradecopier

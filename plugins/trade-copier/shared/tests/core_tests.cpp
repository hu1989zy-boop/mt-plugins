#include "tradecopier/core.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using namespace tradecopier;

namespace {
int checks = 0;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

void Decision(const VolumeDecision& actual, Code code, Units units, const char* description) {
    ++checks;
    if (actual.code != code || actual.units != units) {
        std::cerr << "FAIL: " << description << " (got " << CodeName(actual.code)
                  << ", " << actual.units << "; expected " << CodeName(code)
                  << ", " << units << ")\n";
        std::exit(1);
    }
}

bool Has(const std::vector<RelationError>& errors, RelationError error) {
    return std::find(errors.begin(), errors.end(), error) != errors.end();
}

constexpr Units lot = kUnitsPerLot;
constexpr Units centilot = lot / 100;
const VolumeLimits normal{centilot, 100 * lot, centilot};
const OpeningInput one_lot{lot, 1000000, 500000}; // source $10k, follower $5k

void DecimalTests() {
    Rational value;
    Check(ParsePositiveDecimal("1.500", value) && value.numerator == 3 &&
          value.denominator == 2, "decimal 1.500 exactly reduces to 3/2");
    Check(ParsePositiveDecimal("0.00000001", value) && value.numerator == 1 &&
          value.denominator == 100000000, "small decimal stays exact");
    const Rational prior = value;
    for (const auto* text : {"", "0", "0.000", "-1", "+1", " 1", "1e3", ".5", "1.",
                             "1.2.3", "18446744073709551616", "0.00000000000000000001"}) {
        Check(!ParsePositiveDecimal(text, value), "invalid/overflow decimal rejected");
        Check(value.numerator == prior.numerator && value.denominator == prior.denominator,
              "failed decimal parse leaves output unchanged");
    }
}

void OpeningTests() {
    Decision(PlanOpening({SizingMode::LotRatio, {3, 2}, 0}, one_lot, normal),
             Code::Ready, 150 * centilot, "1 lot times 1.5 = 1.50 lots");
    Decision(PlanOpening({SizingMode::EquityRatio, {6, 5}, 0}, one_lot, normal),
             Code::Ready, 60 * centilot, "1 * $5k/$10k * 1.2 = 0.60 lots");
    Decision(PlanOpening({SizingMode::FixedLots, {}, 20 * centilot},
                         {3 * lot, 1000000, 500000}, normal),
             Code::Ready, 20 * centilot, "fixed 0.20 lots independent of 3-lot source");
    Decision(PlanOpening({SizingMode::LotRatio, {1, 3}, 0}, one_lot, normal),
             Code::Ready, 33 * centilot, "floor 1/3 lot to 0.33, never round up");
    Decision(PlanOpening({SizingMode::LotRatio, {1, 1000}, 0}, one_lot, normal),
             Code::SkipBelowMinimum, 0, "tiny sizing is skipped, never forced to minimum");
    Decision(PlanOpening({SizingMode::FixedLots, {}, 101 * lot}, one_lot, normal),
             Code::ExceedsMaximum, 0, "over maximum rejects rather than caps");
    Decision(PlanOpening({SizingMode::FixedLots, {}, lot + centilot / 2}, one_lot, normal),
             Code::Ready, lot, "fixed amount also floors to symbol step");
    Decision(PlanOpening({SizingMode::LotRatio, {0, 1}, 0}, one_lot, normal),
             Code::InvalidInput, 0, "zero multiplier rejected");
    Decision(PlanOpening({SizingMode::LotRatio, {1, 0}, 0}, one_lot, normal),
             Code::InvalidInput, 0, "zero denominator rejected");
    Decision(PlanOpening({SizingMode::EquityRatio, {}, 0}, {lot, 0, 500000}, normal),
             Code::InvalidInput, 0, "zero source equity rejected");
    Decision(PlanOpening({SizingMode::FixedLots, {}, lot}, {lot, 100, 0}, normal),
             Code::Ready, lot, "fixed sizing does not require unused equity fields");
    Decision(PlanOpening({SizingMode::LotRatio, {3, 2}, 0}, {lot, 0, 0}, normal),
             Code::Ready, 150 * centilot, "lot ratio does not require unused equity fields");
    Decision(PlanOpening({SizingMode::EquityRatio, {}, 0}, {lot, 100, 0}, normal),
             Code::InvalidInput, 0, "equity ratio rejects zero follower equity");
    Decision(PlanOpening({}, one_lot, {1, 100, 0}), Code::InvalidInput, 0, "zero step rejected");
    Decision(PlanOpening({}, one_lot, {3, 100, 2}), Code::InvalidInput, 0,
             "unaligned metadata rejected for adapter review");
    Decision(PlanOpening({}, {0, 100, 100}, normal), Code::InvalidInput, 0,
             "zero source volume rejected");
}

void OverflowTests() {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    const VolumeLimits all{1, max, 1};
    Decision(PlanOpening({SizingMode::LotRatio, {max, max}, 0}, {max, 1, 1}, all),
             Code::Ready, max, "cross cancellation avoids a needless overflow");
    Decision(PlanOpening({SizingMode::LotRatio, {2, 1}, 0}, {max, 1, 1}, all),
             Code::ArithmeticOverflow, 0, "multiplication overflow fails closed");
    Decision(PlanOpening({SizingMode::EquityRatio, {1, max - 1}, 0},
                         {1, max, 1}, all), Code::ArithmeticOverflow, 0,
             "irreducible denominator overflow fails closed");
    Decision(PlanClosing(max, max, max, all), Code::Ready, max,
             "full close avoids an unnecessary huge ratio product");
    Decision(PlanClosing(max - 1, max - 2, max, all), Code::ArithmeticOverflow, 0,
             "partial close product overflow fails closed");
}

void ClosingTests() {
    Decision(PlanClosing(2 * lot, 50 * centilot, 20 * centilot, normal),
             Code::Ready, 5 * centilot, "fixed 0.20 copy closes 0.05 when source closes 25%");
    Decision(PlanClosing(lot, 50 * centilot, 60 * centilot, normal),
             Code::Ready, 30 * centilot, "partial close uses actual current follower remainder");
    Decision(PlanClosing(lot, centilot, centilot, normal),
             Code::SkipBelowMinimum, 0, "tiny partial close does not round up");
    Decision(PlanClosing(lot, lot, 3 * centilot, normal),
             Code::Ready, 3 * centilot, "full source close closes all mapped remainder");
    Decision(PlanClosing(lot, lot + 1, lot, normal), Code::InvalidInput, 0,
             "source over-close rejected");
    Decision(PlanClosing(lot, lot, centilot / 2, normal), Code::ReviewUnalignedVolume, 0,
             "unaligned full remainder needs review");
    const VolumeLimits min_two{2 * centilot, 100 * lot, centilot};
    Decision(PlanClosing(lot, 75 * centilot, 3 * centilot, min_two),
             Code::ReviewResidualBelowMinimum, 0,
             "partial close leaving less than minimum does not close extra");
    Decision(PlanClosing(lot, lot, centilot, min_two), Code::ReviewCloseBelowMinimum, 0,
             "full remainder below permitted close minimum needs review");
    Decision(PlanClosing(lot, lot, 101 * lot, normal), Code::ExceedsMaximum, 0,
             "full close above single-request maximum requires adapter decision");
}

void ClosingBoundarySweep() {
    // Small integer exhaustive oracle exercises both min-close and min-remainder
    // boundaries independently of ratio reduction. Products fit without overflow.
    for (Units minimum = 1; minimum <= 3; ++minimum) {
        const VolumeLimits limits{minimum, 20, 1};
        for (Units source = 1; source <= 12; ++source) {
            for (Units closed = 1; closed <= source; ++closed) {
                for (Units target = 1; target <= 12; ++target) {
                    Code expected = Code::Ready;
                    Units units = target;
                    if (closed == source) {
                        if (target < minimum) expected = Code::ReviewCloseBelowMinimum;
                    } else {
                        units = target * closed / source;
                        if (units < minimum) expected = Code::SkipBelowMinimum;
                        else if (target - units < minimum) expected = Code::ReviewResidualBelowMinimum;
                    }
                    if (expected != Code::Ready) units = 0;
                    Decision(PlanClosing(source, closed, target, limits), expected, units,
                             "small-volume exhaustive min-close/min-remainder boundary oracle");
                }
            }
        }
    }
}

void RelationTests() {
    const VolumePolicy ratio{SizingMode::LotRatio, {3, 2}, 0};
    const Id high = 6000000000ULL;
    std::vector<Relation> relations{
        {1, high, high + 10, ratio}, {2, high, high + 11, ratio},
        {3, high + 1, high + 10, {SizingMode::FixedLots, {}, centilot}},
        {4, high + 1, high + 11, {SizingMode::EquityRatio, {1, 1}, 0}}};
    Check(ValidateRelations(relations).empty(), "many-to-many with independent modes and 64-bit IDs");
    relations.push_back({5, high, high + 10, ratio});
    Check(Has(ValidateRelations(relations), RelationError::DuplicatePair), "duplicate pair rejected");
    relations.pop_back();
    relations.push_back({5, high + 10, high, ratio});
    Check(Has(ValidateRelations(relations), RelationError::DirectedCycle), "two-account cycle rejected");
    Check(Has(ValidateRelations({{1, 1, 2, ratio}, {2, 2, 3, ratio}, {3, 3, 1, ratio}}),
              RelationError::DirectedCycle), "three-account cycle rejected");
    Check(Has(ValidateRelations({{1, 1, 1, ratio}}), RelationError::SelfFollow), "self-follow rejected");
    Check(Has(ValidateRelations({{1, 1, 2, ratio}, {1, 1, 3, ratio}}), RelationError::DuplicateId),
          "duplicate relation IDs rejected");
    Check(Has(ValidateRelations({{0, 0, 1, ratio}}), RelationError::InvalidId), "zero IDs rejected");
    Check(Has(ValidateRelations({{1, 1, 2, {SizingMode::FixedLots, {}, 0}}}),
              RelationError::InvalidPolicy), "invalid sizing configuration rejected");
}

void GateTests() {
    ActivationGate gate(1000, {41, 42});
    Check(gate.AdmitOpening(41, 1001) == Admission::BaselinePosition,
          "baseline old position excluded even if observed after activation");
    Check(gate.AdmitOpening(43, 999) == Admission::BeforeActivation,
          "late pre-activation open absent from snapshot still excluded");
    Check(gate.AdmitOpening(45, 1000) == Admission::BeforeActivation, "activation boundary excluded");
    Check(gate.AdmitOpening(43, 1005) == Admission::BaselinePosition,
          "late old position remains excluded when a later update has a new sequence");
    Check(gate.AdmitOpening(44, 1001) == Admission::Accepted, "new source position admitted");
    Check(gate.AdmitOpening(44, 1001) == Admission::Duplicate, "duplicate opening ignored");
    Check(gate.AdmitOpening(44, 1002) == Admission::Duplicate,
          "existing position updates do not create another opening");
    Check(gate.AdmitOpening(0, 1003) == Admission::InvalidInput, "invalid opening ID rejected");
    ActivationGate later_follower(1005, {44});
    Check(later_follower.AdmitOpening(44, 1006) == Admission::BaselinePosition,
          "new relation gets its own baseline and does not copy existing source");
}

void LinkTests() {
    PositionLink fixed(101, 201, 2 * lot, 20 * centilot);
    Decision(fixed.OnSourceClose(1, 50 * centilot, normal), Code::Ready, 5 * centilot,
             "fixed-lot mapping plans proportional partial close");
    Check(fixed.source_remaining() == 150 * centilot && fixed.follower_remaining() == 20 * centilot,
          "source confirmed immediately, follower unchanged until execution confirmation");
    Decision(fixed.OnSourceClose(1, 50 * centilot, normal), Code::DuplicateEvent, 0,
             "duplicate close callback does not send another request");
    Decision(fixed.OnSourceClose(2, 50 * centilot, normal), Code::AwaitingExecution, 0,
             "next event waits for execution, avoiding stale target volume");
    Check(fixed.ConfirmFollowerClose(99, 5 * centilot) == Code::InvalidInput,
          "wrong confirmation ID rejected");
    Check(fixed.ConfirmFollowerClose(1, 5 * centilot) == Code::Ready,
          "confirmed close updates mapping");
    Decision(fixed.OnSourceClose(2, 50 * centilot, normal), Code::Ready, 5 * centilot,
             "second close uses 0.5/1.5 times actual 0.15");
    Check(fixed.ConfirmFollowerClose(2, 5 * centilot) == Code::Ready, "second close confirmed");
    Decision(fixed.OnSourceClose(3, lot, normal), Code::Ready, 10 * centilot,
             "source final close clears follower remainder");
    Check(fixed.ConfirmFollowerClose(3, 10 * centilot) == Code::Ready &&
          fixed.state() == LinkState::Closed, "completed link is terminal");
    Decision(fixed.OnSourceClose(4, lot, normal), Code::NoActionTerminal, 0,
             "completed link never creates more actions");

    PositionLink tail(102, 202, lot, 3 * centilot);
    Decision(tail.OnSourceClose(1, 33300000, normal), Code::SkipBelowMinimum, 0,
             "first third of 0.03 rounds below 0.01 and skips");
    Decision(tail.OnSourceClose(2, 33300000, normal), Code::Ready, centilot,
             "next third uses remaining source and actual target");
    Check(tail.ConfirmFollowerClose(2, centilot) == Code::Ready, "rounded partial close confirmed");
    Decision(tail.OnSourceClose(3, 33400000, normal), Code::Ready, 2 * centilot,
             "final source close clears accumulated rounding tail");
    Check(tail.ConfirmFollowerClose(3, 2 * centilot) == Code::Ready &&
          tail.follower_remaining() == 0, "no tail left after full close");

    PositionLink manual(103, 203, lot, lot);
    Check(manual.ObserveManualFollowerRemaining(60 * centilot) == Code::Ready,
          "manual partial close updates actual follower remainder");
    Decision(manual.OnSourceClose(1, 50 * centilot, normal), Code::Ready, 30 * centilot,
             "subsequent proportional close respects manual reduction");
    Check(manual.ConfirmFollowerClose(1, 30 * centilot) == Code::Ready, "manual reduced close confirms");
    Check(manual.ObserveManualFollowerRemaining(0) == Code::Ready &&
          manual.state() == LinkState::ManuallyClosed, "manual full close terminates mapping");
    Decision(manual.OnSourceClose(2, 50 * centilot, normal), Code::NoActionTerminal, 0,
             "manually closed link never closes or reopens");
    Check(manual.ObserveManualFollowerRemaining(lot) == Code::NoActionTerminal,
          "terminal mapping cannot be revived by later snapshot");

    PositionLink failed(104, 204, lot, lot);
    Decision(failed.OnSourceClose(1, lot, normal), Code::Ready, lot, "full-close proposal before failure");
    Check(failed.ConfirmFollowerClose(1, 0) == Code::NeedsReview,
          "failed execution halts instead of blindly retrying");
    Decision(failed.OnSourceClose(2, lot, normal), Code::NeedsReview, 0,
             "unreconciled mapping cannot produce further orders");

    PositionLink flight(105, 205, lot, lot);
    Decision(flight.OnSourceClose(1, 50 * centilot, normal), Code::Ready, 50 * centilot,
             "in-flight close created");
    Check(flight.ObserveManualFollowerRemaining(0) == Code::NeedsReview &&
          flight.state() == LinkState::ManuallyClosed,
          "manual full close while request in flight terminates and flags reconciliation");
    Decision(flight.OnSourceClose(2, 50 * centilot, normal), Code::NoActionTerminal, 0,
             "no further request after in-flight manual close");

    PositionLink partial(106, 206, lot, lot);
    Decision(partial.OnSourceClose(1, 50 * centilot, normal), Code::Ready, 50 * centilot,
             "partial execution setup");
    Check(partial.ConfirmFollowerClose(1, 20 * centilot) == Code::NeedsReview &&
          partial.follower_remaining() == 80 * centilot,
          "partial execution records actual volume and requires reconciliation");

    const auto max = std::numeric_limits<std::uint64_t>::max();
    PositionLink overflow(107, 207, max - 1, max);
    Decision(overflow.OnSourceClose(1, max - 2, {1, max, 1}), Code::ArithmeticOverflow, 0,
             "overflow in an already confirmed source close requires reconciliation");
    Decision(overflow.OnSourceClose(2, max - 1, {1, max, 1}), Code::NeedsReview, 0,
             "later event cannot plan with stale source remainder after overflow");
    PositionLink metadata(108, 208, lot, lot);
    Decision(metadata.OnSourceClose(1, 50 * centilot, {1, lot, 0}), Code::InvalidInput, 0,
             "invalid symbol metadata freezes confirmed source close");
    Decision(metadata.OnSourceClose(2, 50 * centilot, normal), Code::NeedsReview, 0,
             "later event cannot plan with stale source remainder after invalid metadata");
    PositionLink same_position(109, 109, lot, lot);
    Check(same_position.state() == LinkState::NeedsReview,
          "source and follower cannot identify the same hedging position");
    PositionLink increased(110, 210, lot, lot);
    Check(increased.ObserveManualFollowerRemaining(2 * lot) == Code::InvalidInput &&
          increased.state() == LinkState::NeedsReview,
          "unexpected follower increase freezes stale local volume for reconciliation");
    Decision(increased.OnSourceClose(1, 50 * centilot, normal), Code::NeedsReview, 0,
             "later source close cannot use stale target remainder after invalid snapshot");
}
} // namespace

int main() {
    DecimalTests();
    OpeningTests();
    OverflowTests();
    ClosingTests();
    ClosingBoundarySweep();
    RelationTests();
    GateTests();
    LinkTests();
    std::cout << "PASS: " << checks << " policy, admission, graph and link checks\n";
}

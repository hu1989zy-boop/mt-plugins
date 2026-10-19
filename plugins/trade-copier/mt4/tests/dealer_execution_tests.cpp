#include "dealer_execution.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using mt4copier::DealerFailurePhase;
using mt4copier::ExecuteAcceptedDealerRequest;
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
struct Invocation { std::string operation; int id; int dealer; };
struct Calls {
    int lock_result = 1;
    int confirm_result = 0;
    int release_result = 1;
    bool lock_throws = false;
    bool confirm_throws = false;
    bool release_throws = false;
    std::vector<Invocation> history;

    mt4copier::DealerExecutionOutcome Run(bool accepted = true, std::int64_t id = 123,
                                        std::int64_t dealer = 2016) {
        return ExecuteAcceptedDealerRequest(accepted, id, dealer,
            [&](int request, int login) {
                history.push_back({"lock", request, login});
                if (lock_throws) throw std::runtime_error("lock outcome unknown");
                return lock_result;
            },
            [&](int request, int login) {
                history.push_back({"confirm", request, login});
                if (confirm_throws) throw 42; // All exception kinds retain admission.
                return confirm_result;
            },
            [&](int request, int login) {
                history.push_back({"release", request, login});
                if (release_throws) throw std::runtime_error("release outcome unknown");
                return release_result;
            });
    }
    void Sequence(const std::vector<std::string>& expected, int id = 123, int dealer = 2016) const {
        Check(history.size() == expected.size(), "exact number of dealer API calls");
        for (std::size_t n = 0; n < history.size(); ++n) {
            Check(history[n].operation == expected[n], "dealer API call ordering");
            Check(history[n].id == id, "only this plugin's exact request ID is touched");
            Check(history[n].dealer == dealer, "configured dealer identity is preserved");
        }
    }
};

void RejectedAndInvalidAdmission() {
    Calls calls;
    const auto rejected = calls.Run(false);
    calls.Sequence({});
    Check(!rejected.accepted && rejected.failure_phase == DealerFailurePhase::Admission,
          "unaccepted admission never reaches dealer APIs");
    Check(!rejected.lock.attempted && !rejected.confirm.attempted && !rejected.release.attempted,
          "unaccepted admission does not imply lock or execution");

    constexpr auto too_large = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1;
    for (const auto invalid : {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1},
                              std::int64_t{0}, too_large, std::numeric_limits<std::int64_t>::max()}) {
        for (bool bad_request : {false, true}) {
            Calls item;
            const auto id = bad_request ? invalid : 123;
            const auto dealer = bad_request ? 2016 : invalid;
            const auto result = item.Run(true, id, dealer);
            item.Sequence({});
            Check(result.accepted, "invalid post-admission identity cannot erase admission");
            Check(result.request_id == id && result.dealer_login == dealer,
                  "out of range identity remains available for diagnostics without truncation");
            Check(result.failure_phase == DealerFailurePhase::Identity,
                  "signed 32-bit ID range is enforced before dealer APIs");
        }
    }
}

void NativeReturnConventions() {
    for (int success : {1, 2, -1}) {
        Calls calls;
        calls.lock_result = success;
        const auto result = calls.Run();
        calls.Sequence({"lock", "confirm"});
        Check(result.accepted && result.failure_phase == DealerFailurePhase::None,
              "nonzero BOOL lock followed by RET_OK confirmation retains accepted request");
        Check(result.lock.attempted && result.lock.returned && result.lock.native_result == success && !result.lock.threw,
              "raw BOOL lock return is recorded");
        Check(result.confirm.attempted && result.confirm.returned && result.confirm.native_result == 0 && !result.confirm.threw,
              "RET_OK zero confirmation is successful, not false");
        Check(!result.release.attempted, "successful confirmation does not release or retry");
    }
    Calls lock_failed;
    lock_failed.lock_result = 0;
    const auto failed = lock_failed.Run();
    lock_failed.Sequence({"lock"});
    Check(failed.accepted && failed.failure_phase == DealerFailurePhase::Lock,
          "FALSE zero lock cannot be mistaken for RET_OK");
    Check(failed.lock.returned && !failed.lock.threw && failed.lock.native_result == 0,
          "failed lock preserves native result");
    Check(!failed.confirm.attempted && !failed.release.attempted,
          "a lock not acquired is neither confirmed nor released");

    for (int failure : {-1, 1, 128, 134}) {
        for (int release_result : {-1, 0, 1}) {
            Calls calls;
            calls.confirm_result = failure;
            calls.release_result = release_result;
            const auto result = calls.Run();
            calls.Sequence({"lock", "confirm", "release"});
            Check(result.accepted && result.failure_phase == DealerFailurePhase::Confirm,
                  "nonzero confirmation preserves admission and original failure phase");
            Check(result.confirm.returned && result.confirm.native_result == failure && !result.confirm.threw,
                  "confirmation return is not rewritten as submission status");
            Check(result.release.attempted && result.release.returned &&
                  result.release.native_result == release_result && !result.release.threw,
                  "release native result recorded without inventing return semantics");
        }
    }
}

void ExceptionsAfterAdmission() {
    Calls locked_unknown;
    locked_unknown.lock_throws = true;
    const auto lock_result = locked_unknown.Run();
    locked_unknown.Sequence({"lock"});
    Check(lock_result.accepted && lock_result.failure_phase == DealerFailurePhase::Lock,
          "lock exception cannot turn queued admission into not submitted");
    Check(lock_result.lock.attempted && !lock_result.lock.returned && lock_result.lock.threw,
          "lock exception is distinguishable from native FALSE return");
    Check(!lock_result.confirm.attempted && !lock_result.release.attempted,
          "unknown lock ownership never releases another owner or confirms");

    for (bool confirm_throws : {false, true}) {
        for (bool release_throws : {false, true}) {
            Calls calls;
            calls.confirm_result = 134;
            calls.confirm_throws = confirm_throws;
            calls.release_throws = release_throws;
            const auto result = calls.Run();
            calls.Sequence({"lock", "confirm", "release"});
            Check(result.accepted && result.failure_phase == DealerFailurePhase::Confirm,
                  "confirmation or cleanup exception retains queued admission");
            Check(result.confirm.attempted && result.confirm.returned == !confirm_throws &&
                  result.confirm.threw == confirm_throws,
                  "confirmation exception has a distinct diagnostic");
            Check(result.release.attempted && result.release.returned == !release_throws &&
                  result.release.threw == release_throws,
                  "cleanup executes once and never throws after acceptance");
        }
    }
}

void IdentityBoundariesAndIsolation() {
    constexpr int limit = std::numeric_limits<std::int32_t>::max();
    for (const auto& identities : {std::pair<int,int>{1,1}, {limit,limit}, {47,62}}) {
        Calls calls;
        const auto result = calls.Run(true, identities.first, identities.second);
        calls.Sequence({"lock", "confirm"}, identities.first, identities.second);
        Check(result.request_id == identities.first && result.dealer_login == identities.second &&
              result.failure_phase == DealerFailurePhase::None,
              "valid exact request and dealer identities reach native calls unchanged");
    }
    Calls first;
    first.lock_result = 0;
    Calls second;
    const auto failed = first.Run(true, 100, 200);
    const auto succeeding = second.Run(true, 101, 200);
    first.Sequence({"lock"}, 100, 200);
    second.Sequence({"lock", "confirm"}, 101, 200);
    Check(failed.failure_phase == DealerFailurePhase::Lock && succeeding.failure_phase == DealerFailurePhase::None,
          "failed request cannot alter another request or trigger broad queue handling");
}

void DiagnosticEvidence() {
    Calls success;
    const auto success_text = mt4copier::DealerExecutionDiagnostic(success.Run());
    Check(success_text.find("dealer_accepted=1 dealer_request=123 DealerLogin=2016 dealer_failure_phase=none") == 0,
          "diagnostic correlates request and dealer without asserting a fill");
    Check(success_text.find("dealer_lock_attempted=1 dealer_lock_native=1 dealer_lock_threw=0") != std::string::npos &&
          success_text.find("dealer_confirm_attempted=1 dealer_confirm_native=0 dealer_confirm_threw=0") != std::string::npos,
          "diagnostics preserve different native lock and confirmation conventions");
    Check(success_text.find("dealer_release_attempted=0 dealer_release_native=none dealer_release_threw=0") != std::string::npos,
          "unattempted release does not invent a native return code");
    Calls exception;
    exception.confirm_throws = exception.release_throws = true;
    const auto exception_text = mt4copier::DealerExecutionDiagnostic(exception.Run());
    Check(exception_text.find("dealer_failure_phase=confirm") != std::string::npos &&
          exception_text.find("dealer_confirm_attempted=1 dealer_confirm_native=none dealer_confirm_threw=1") != std::string::npos &&
          exception_text.find("dealer_release_attempted=1 dealer_release_native=none dealer_release_threw=1") != std::string::npos,
          "exceptions retain original failure and uncertain release diagnostics");
    Check(exception_text.find_first_of("\r\n\t") == std::string::npos,
          "scalar diagnostics are a single line");
}
} // namespace

int main() {
    try {
        RejectedAndInvalidAdmission();
        NativeReturnConventions();
        ExceptionsAfterAdmission();
        IdentityBoundariesAndIsolation();
        DiagnosticEvidence();
        std::cout << "MT4 dealer execution tests passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MT4 dealer execution test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}

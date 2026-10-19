#pragma once
#include <cstdint>
#include <limits>
#include <string>

namespace mt4copier {

enum class DealerFailurePhase { None, Admission, Identity, Lock, Confirm };

struct DealerCallOutcome {
    bool attempted = false;
    bool returned = false;
    int native_result = 0;
    bool threw = false;
};

struct DealerExecutionOutcome {
    bool accepted = false;
    std::int64_t request_id = 0;
    std::int64_t dealer_login = 0;
    DealerFailurePhase failure_phase = DealerFailurePhase::None;
    DealerCallOutcome lock;
    DealerCallOutcome confirm;
    DealerCallOutcome release;
};

// Scalar-only diagnostics do not include untrusted exception or account text.
// The caller must still guard logging against allocation/output exceptions
// after RequestsAdd, since accepted requests must keep their queued identity.
inline std::string DealerExecutionDiagnostic(const DealerExecutionOutcome& result) {
    const char* phase = "unknown";
    switch (result.failure_phase) {
        case DealerFailurePhase::None: phase = "none"; break;
        case DealerFailurePhase::Admission: phase = "admission"; break;
        case DealerFailurePhase::Identity: phase = "identity"; break;
        case DealerFailurePhase::Lock: phase = "lock"; break;
        case DealerFailurePhase::Confirm: phase = "confirm"; break;
    }
    std::string text = "dealer_accepted=" + std::to_string(result.accepted ? 1 : 0) +
        " dealer_request=" + std::to_string(result.request_id) +
        " DealerLogin=" + std::to_string(result.dealer_login) +
        " dealer_failure_phase=" + phase;
    const auto append = [&](const char* name, const DealerCallOutcome& call) {
        const std::string prefix = std::string(" dealer_") + name;
        text += prefix + "_attempted=" + std::to_string(call.attempted ? 1 : 0);
        text += prefix + "_native=" + (call.returned ? std::to_string(call.native_result) : "none");
        text += prefix + "_threw=" + std::to_string(call.threw ? 1 : 0);
    };
    append("lock", result.lock);
    append("confirm", result.confirm);
    append("release", result.release);
    return text;
}

// Operate only on the exact request ID returned by this plugin's RequestsAdd.
// An accepted request remains accepted even when dealer processing fails or
// throws: the caller must retain Queued and reconcile execution evidence.
// This helper neither retries nor reports a fill. RequestsLock uses BOOL
// semantics; RequestsConfirm uses RET_OK == 0, not BOOL semantics.
template<class Lock, class Confirm, class Release>
DealerExecutionOutcome ExecuteAcceptedDealerRequest(
        bool accepted, std::int64_t request_id, std::int64_t dealer_login,
        Lock&& lock, Confirm&& confirm, Release&& release) noexcept {
    DealerExecutionOutcome result;
    result.accepted = accepted;
    result.request_id = request_id;
    result.dealer_login = dealer_login;
    if (!accepted) {
        result.failure_phase = DealerFailurePhase::Admission;
        return result;
    }
    constexpr auto max_id = std::numeric_limits<std::int32_t>::max();
    if (request_id <= 0 || request_id > max_id || dealer_login <= 0 || dealer_login > max_id) {
        result.failure_phase = DealerFailurePhase::Identity;
        return result;
    }
    const auto id = static_cast<int>(request_id);
    const auto dealer = static_cast<int>(dealer_login);
    result.lock.attempted = true;
    try {
        result.lock.native_result = lock(id, dealer);
        result.lock.returned = true;
    } catch (...) {
        result.lock.threw = true;
        result.failure_phase = DealerFailurePhase::Lock;
        return result;
    }
    if (result.lock.native_result == 0) {
        result.failure_phase = DealerFailurePhase::Lock;
        return result;
    }
    result.confirm.attempted = true;
    try {
        result.confirm.native_result = confirm(id, dealer);
        result.confirm.returned = true;
        if (result.confirm.native_result == 0) return result;
    } catch (...) {
        result.confirm.threw = true;
    }
    result.failure_phase = DealerFailurePhase::Confirm;

    // Release only a lock that was positively acquired, after an unsuccessful
    // confirmation attempt. Preserve both native diagnostics and uncertainty;
    // even a release error must not cause a second submission or confirmation.
    result.release.attempted = true;
    try {
        result.release.native_result = release(id, dealer);
        result.release.returned = true;
    } catch (...) {
        result.release.threw = true;
    }
    return result;
}

} // namespace mt4copier

#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

namespace mt4copier {
// Uses native 32-bit atomics; a successful admission keeps callback storage
// alive until cleanup closes admission and drains all earlier users.
class CallGate {
    static constexpr std::uint32_t closed = 0x80000000u;
    std::atomic<std::uint32_t> count_{closed};
public:
    void Open() noexcept { count_.store(0, std::memory_order_release); }
    bool Enter() noexcept {
        auto n = count_.load(std::memory_order_acquire);
        while (!(n & closed) && n != closed - 1) {
            if (count_.compare_exchange_weak(n, n + 1, std::memory_order_acquire)) return true;
        }
        return false;
    }
    void Leave() noexcept { count_.fetch_sub(1, std::memory_order_release); }
    void Close() noexcept { count_.fetch_or(closed, std::memory_order_acq_rel); }
    void Drain() const noexcept {
        while (count_.load(std::memory_order_acquire) & ~closed) std::this_thread::yield();
    }
};
struct Stopping : std::exception {};
class GateUse {
    CallGate& gate_;
public:
    explicit GateUse(CallGate& gate) : gate_(gate) { if (!gate_.Enter()) throw Stopping{}; }
    ~GateUse() { gate_.Leave(); }
    GateUse(const GateUse&) = delete;
    GateUse& operator=(const GateUse&) = delete;
};

inline std::uint64_t Unsigned(const std::string& value) {
    if (value.empty() || value.size() > 20) throw std::invalid_argument("invalid unsigned parameter");
    std::uint64_t result = 0;
    for (char c : value) {
        if (c < '0' || c > '9' || result > (UINT64_MAX - static_cast<unsigned>(c - '0')) / 10)
            throw std::invalid_argument("invalid unsigned parameter");
        result = result * 10 + static_cast<unsigned>(c - '0');
    }
    return result;
}
using ParameterValues = std::map<std::string, std::string>;
inline ParameterValues DefaultParameters() {
    return {{"Armed", "0"}, {"ExpectedServerName", ""}, {"ExpectedServerBuild", "1470"},
        {"ServerKey", "1"}, {"ApiAppId", "3401"}, {"ConfigPath", ""}, {"JournalPath", ""},
        {"ManagementEnabled", "0"}, {"AllowedSymbol", "*"}, {"DealerLogin", "0"}};
}
struct Parameters {
    bool armed = false, management = false;
    std::uint64_t server_key = 1;
    std::uint32_t dealer_login = 0; // Deployment placeholder; an armed plugin requires a real manager login.
    unsigned app = 3401, build = 1470;
    std::string server_name, config, journal, symbol;
};
inline Parameters ParseParameters(const ParameterValues& values) {
    const auto defaults = DefaultParameters();
    if (values.size() != defaults.size()) throw std::invalid_argument("incomplete plugin parameters");
    for (const auto& item : values)
        if (!defaults.count(item.first) || item.second.size() >= 128 || item.second.find('\0') != std::string::npos)
            throw std::invalid_argument("unknown or oversized plugin parameter");
    Parameters p;
    const auto armed = Unsigned(values.at("Armed")), management = Unsigned(values.at("ManagementEnabled"));
    const auto build = Unsigned(values.at("ExpectedServerBuild")), app = Unsigned(values.at("ApiAppId"));
    const auto dealer = Unsigned(values.at("DealerLogin"));
    p.server_key = Unsigned(values.at("ServerKey"));
    if (armed > 1 || management > 1 || !build || build > 32767 || !app || app > 4095 || !p.server_key)
        throw std::invalid_argument("invalid plugin identity or boolean parameter");
    if (dealer > static_cast<std::uint64_t>(INT32_MAX) || (armed && !dealer))
        throw std::invalid_argument("Armed=1 needs DealerLogin in 1..2147483647");
    p.armed = armed == 1; p.management = management == 1;
    p.app = static_cast<unsigned>(app); p.build = static_cast<unsigned>(build);
    p.dealer_login = static_cast<std::uint32_t>(dealer);
    p.server_name = values.at("ExpectedServerName"); p.config = values.at("ConfigPath");
    p.journal = values.at("JournalPath"); p.symbol = values.at("AllowedSymbol");
    if (p.symbol.empty() || (p.armed && (p.server_name.empty() || p.config.empty() || p.journal.empty())))
        throw std::invalid_argument("Armed=1 needs server name, configuration and journal paths");
    return p;
}
} // namespace mt4copier

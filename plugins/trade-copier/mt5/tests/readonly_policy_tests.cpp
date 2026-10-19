#include "readonly_policy.hpp"
#include <iostream>
#include <limits>

using namespace tradecopier;
using namespace tradecopier::execution;

namespace {
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void Reject(F operation, const std::string& reason) {
    try { operation(); }
    catch (const std::runtime_error& error) {
        Check(std::string(error.what()).find(reason) != std::string::npos,
              "failure must identify the expected account or cause");
        return;
    }
    throw std::runtime_error("expected rejection: " + reason);
}

void AccountPermissions() {
    // Check all combinations of the three client restriction bits. A dealer
    // configuration only permits the follower role; it does not alter sources.
    for (unsigned bits = 0; bits != 8; ++bits) {
        const std::uint64_t blocked = ((bits & 1) ? 0x4 : 0) |
                                      ((bits & 2) ? 0x8 : 0) |
                                      ((bits & 4) ? 0x200 : 0);
        const auto rights = kUserEnabled | blocked | 0x4000 | 0x40;
        Check(ClientTradingBlocked(rights) == (bits != 0), "client restriction detection");
        Check(CopyRequestAction(rights, 999, 50002) ==
              (bits ? kDealerExecuteAction : kClientMarketAction), "route only restricted followers through dealer");
        CheckAccountAccess(rights, true, 999, 50002);
        if (bits) {
            Reject([&] { CheckAccountAccess(rights, false, 999, 50001); }, "login=50001");
            Reject([&] { CopyRequestAction(rights, 0, 50002); }, "DealerLogin");
            Reject([&] { CopyRequestAction(rights, 0, 50002); }, "login=50002");
        } else {
            CheckAccountAccess(rights, false, 0, 50001);
            Check(CopyRequestAction(rights, 0, 50002) == kClientMarketAction,
                  "existing writable accounts need no dealer configuration");
        }
        Reject([&] { CopyRequestAction(blocked, 999, 50002); }, "disabled");
        Reject([&] { CheckAccountAccess(blocked, false, 999, 50001); }, "disabled");
        Check(rights == (kUserEnabled | blocked | 0x4000 | 0x40), "account permissions remain unchanged");
    }
    Reject([] { CopyRequestAction(kUserEnabled, 999, 0); }, "login=0");
}

void TradeFlagsAndPrices() {
    for (const bool close : {false, true}) {
        Check(CopyRequestFlags(kClientMarketAction, close) == (close ? 3 : 2),
              "preserve existing client market flags");
        Check(CopyRequestFlags(kDealerExecuteAction, close) == (close ? 1 : 0),
              "dealer flags contain only position-close marker");
        for (const auto action : {kClientMarketAction, kDealerExecuteAction})
            Check((CopyRequestFlags(action, close) & 1024) == 0, "never bypass margin checks");
    }
    for (const auto action : {0u, 201u, std::numeric_limits<unsigned>::max()})
        Reject([&] { CopyRequestFlags(action, false); }, "unsupported");
    Check(DealerExecutionPrice(Side::Buy, 2000, 2001, 10000, 10000) == 2001,
          "buy opens and sell-position closes execute at ask");
    Check(DealerExecutionPrice(Side::Sell, 2000, 2001, 10000, 10000) == 2000,
          "sell opens and buy-position closes execute at bid");
    Check(DealerExecutionPrice(Side::Buy, 2000, 2000, 10000, 15000) == 2000,
          "zero spread and exactly 5000 ms old quotes are allowed");
    const auto max_time = std::numeric_limits<std::int64_t>::max();
    Check(DealerExecutionPrice(Side::Sell, 1, 2, max_time - 5000, max_time) == 1,
          "timestamp age calculation does not overflow");
    Reject([] { DealerExecutionPrice(Side::Buy, 1, 2, 10000, 15001); }, "5000 ms");
    Reject([] { DealerExecutionPrice(Side::Buy, 1, 2, 10001, 10000); }, "future");
    Reject([] { DealerExecutionPrice(Side::Buy, 1, 2, 0, 10000); }, "missing");
    Reject([] { DealerExecutionPrice(Side::Buy, 1, 2, -1, 10000); }, "missing");
    Reject([] { DealerExecutionPrice(Side::Buy, 1, 2, 1, -1); }, "future");
    Reject([&] { DealerExecutionPrice(Side::Buy, 1, 2, 1, max_time); }, "5000 ms");
    Reject([] { DealerExecutionPrice(static_cast<Side>(5), 1, 2, 1, 1); }, "side");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (const auto value : {0.0, -1.0, nan, inf, -inf}) {
        Reject([&] { DealerExecutionPrice(Side::Buy, value, 2, 1, 1); }, "quote");
        Reject([&] { DealerExecutionPrice(Side::Sell, 1, value, 1, 1); }, "quote");
    }
    Reject([] { DealerExecutionPrice(Side::Sell, 3, 2, 1, 1); }, "quote");
}

void DealerPermissions() {
    const std::vector<std::wstring> groups{L"demo\\hedge", L"real\\community"};
    const auto original = groups;
    RequireDealerGroup(999, 999, true, true, groups, L"real\\community");
    Check(groups == original, "dealer authorization is read-only");
    Reject([&] { RequireDealerGroup(0, 0, true, true, groups, L"demo\\hedge"); }, "identity");
    Reject([&] { RequireDealerGroup(999, 998, true, true, groups, L"demo\\hedge"); }, "identity");
    Reject([&] { RequireDealerGroup(999, 999, false, true, groups, L"demo\\hedge"); }, "disabled");
    Reject([&] { RequireDealerGroup(999, 999, true, false, groups, L"demo\\hedge"); }, "permission");
    Reject([&] { RequireDealerGroup(999, 999, true, true, groups, L"real\\other"); }, "group");
    Reject([] { RequireDealerGroup(999, 999, true, true, {}, L"real\\community"); }, "group");
    Reject([] { RequireDealerGroup(999, 999, true, true, {L""}, L""); }, "group");
}

void DealerGroupMasks() {
    const std::wstring target = L"real\\VU\\ECN\\BP-L500";
    const auto allow = [&](const std::vector<std::wstring>& masks,
                           std::wstring_view group = L"real\\VU\\ECN\\BP-L500") {
        const auto original = masks;
        RequireDealerGroup(2016, 2016, true, true, masks, group);
        Check(masks == original, "mask authorization does not change dealer groups");
    };
    const auto deny = [&](const std::vector<std::wstring>& masks,
                          std::wstring_view group = L"real\\VU\\ECN\\BP-L500") {
        Reject([&] { RequireDealerGroup(2016, 2016, true, true, masks, group); }, "");
    };

    // Regression: the user's dealer has '*' and account 50002 is in this group.
    allow({L"*"});
    allow({target});
    allow({L"real\\*"});
    allow({L"*\\BP-L500"});
    allow({L"real\\*\\ECN\\BP-*"});
    allow({L"r**l\\*\\B*500"});
    allow({L"real\\VU\\ECN\\BP-L500*"}); // '*' may match no characters.
    allow({L"demo\\*,real\\VU\\*"});
    allow({L" demo\\* , real\\VU\\* "});
    allow({L"real\\*, !demo\\*"});
    allow({L"!demo\\*", L"real\\*"});
    allow({L"*, !real\\VU\\ECN\\BP-L501"});
    allow({L"real\\客户\\*"}, L"real\\客户\\对冲账户");

    deny({});
    deny({L""});
    deny({L"   "});
    deny({L"*"}, L"");
    deny({L"real\\*"}, L"demo\\VU\\ECN\\BP-L500");
    deny({L"Real\\*"}); // Matching remains case-sensitive.
    deny({L"real\\VU\\ECN\\BP-L50"});
    deny({L"real\\VU\\ECN\\BP-L500x"});
    deny({L"*L501"});
    deny({L"!demo\\*"}); // Exclusions alone never grant access.
    deny({L"!*"});

    // Exclusions win regardless of entry/term order, including across rows.
    deny({L"*,!real\\VU\\*"});
    deny({L"!real\\VU\\*,*"});
    deny({L"*", L"!real\\VU\\*"});
    deny({L"!real\\VU\\*", L"*"});
    deny({target, L"!real\\*"});
    deny({L"*", L"demo\\*, !real\\VU\\ECN\\BP-L500"});
    deny({L"demo\\*, !real\\VU\\ECN\\BP-L500", L"*"});
    deny({L"*, !!real\\VU\\*"});
    deny({L"!!!real\\VU\\*", L"*"});

    // Only '*' is a wildcard; '?' and brackets stay literal.
    deny({L"real\\VU\\ECN\\BP-L50?"});
    deny({L"real\\VU\\ECN\\BP-L50[0]"});
    allow({L"real\\literal?"}, L"real\\literal?");
    allow({L"real\\literal[0]"}, L"real\\literal[0]");

    // A matching allow must not hide malformed or truncated permission terms.
    for (const auto& malformed : {
             std::wstring(L"!"), std::wstring(L"  !!!  "),
             std::wstring(L"real\\bad\nmask"), std::wstring(L"real\\bad\tmask"),
             std::wstring(L"real\\bad\rmask"), std::wstring(L"real\\bad\x7fmask"),
             std::wstring(L"real\\bad\0mask", 13),
             std::wstring(256, L'a')}) {
        deny({L"*", malformed});
        deny({malformed, L"*"});
        deny({L"*," + malformed});
        deny({malformed + L",*"});
    }
    const std::wstring maximum_mask(255, L'a');
    allow({maximum_mask}, maximum_mask);
    allow({L" " + maximum_mask + L" "}, maximum_mask);
    deny({std::wstring(256, L'a')}, std::wstring(256, L'a'));
    deny({L"*"}, std::wstring(L"real\\bad\0group", 14));
    deny({L"*"}, L"real\\bad\ngroup");
}
}

int main() {
    try {
        AccountPermissions();
        TradeFlagsAndPrices();
        DealerPermissions();
        DealerGroupMasks();
        std::cout << "readonly policy checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "readonly policy test failed: " << error.what() << '\n';
        return 1;
    }
}

#include "discovery.hpp"
#include <iostream>
#include <map>

using namespace dynamicleverage;
namespace {
void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action, const char* message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  expect(rejected, message);
}
struct User {
  dynamicleverage::Login login = 0;
  std::wstring group;
  void Release() { delete this; }
  dynamicleverage::Login Login() const { return login; }
  const wchar_t* Group() const { return group.c_str(); }
};
struct Api {
  std::vector<Login> logins{1001, 1002, 1003};
  std::map<Login, User> users{{1001, {1001, L"demo\\USD"}},
                            {1002, {1002, L"客户"}}, {1003, {1003, L"real"}}};
  std::uint32_t result = 0;
  bool missing_count = false, missing_pointer = false, throw_after_allocation = false;
  bool allocation_fails = false, cancelled = false;
  Login unreadable = 0, throw_unknown = 0, cancel_after = 0;
  unsigned freed = 0, reads = 0, listings = 0;
  std::uint32_t UserLogins(const wchar_t* mask, std::uint64_t*& out, std::uint32_t& total) {
    ++listings;
    expect(std::wstring(mask) == L"*", "native enumeration must list all groups before shared matching");
    if (!missing_count) total = static_cast<std::uint32_t>(logins.size());
    if (!missing_pointer && !logins.empty()) {
      out = new std::uint64_t[logins.size()];
      std::copy(logins.begin(), logins.end(), out);
    }
    if (throw_after_allocation) throw std::runtime_error("SDK exception after allocation");
    return result;
  }
  void Free(void* buffer) { ++freed; delete[] static_cast<std::uint64_t*>(buffer); }
  User* UserCreate() { return allocation_fails ? nullptr : new User; }
  std::uint32_t UserGet(Login login, User* user) {
    ++reads;
    if (cancel_after == login) cancelled = true;
    if (throw_unknown == login) throw 42;
    if (unreadable == login) return 7;
    *user = users.at(login);
    return 0;
  }
};
void tests() {
  std::map<Login, std::string> errors;
  auto discover = [&](Api& api) {
    errors.clear();
    return mt5::discover_accounts(api, [&] { return api.cancelled; },
                                 [&](Login login, const std::string& error) { errors[login] = error; });
  };
  Api success;
  auto accounts = discover(success);
  expect(accounts.size() == 3 && accounts[0].login == 1001 && accounts[0].group == "demo\\USD" &&
         accounts[1].group == "客户" && success.freed == 1 && errors.empty(),
         "account discovery preserves identities/groups and frees SDK storage");
  Config config;
  config.platform = "mt5";
  config.enabled = true;
  config.rules = {{"groups", "Group rule", true, Scope::group, "!real,*", {{0, 1000}}},
                  {"accounts", "Explicit rule", true, Scope::account, "1003", {{0, 500}}}};
  expect(candidate_accounts(config, accounts) == std::vector<Login>({1001, 1002, 1003}),
         "discovered groups and explicit logins both reach the rules engine");
  expect(select_rule(config, 1001, accounts[0].group)->id == "groups" &&
         select_rule(config, 1003, accounts[2].group)->id == "accounts",
         "shared group exclusions and global first-match priority govern discovery results");

  Api removed;
  removed.unreadable = 1002;
  accounts = discover(removed);
  expect(accounts.size() == 2 && errors.count(1002) && removed.reads == 3 && removed.freed == 1,
         "single unreadable account must not block unrelated accounts");
  removed.unreadable = 0;
  removed.users[1002].group = L"real\\migrated";
  accounts = discover(removed);
  expect(accounts.size() == 3 && accounts[1].group == "real\\migrated" && errors.empty(),
         "later discovery recovers account and reads its current group");
  Api mismatch;
  mismatch.users[1002].login = 9999;
  expect(discover(mismatch).size() == 2 && errors.count(1002), "identity mismatch is excluded");
  Api bad_group;
  bad_group.users[1001].group.clear();
  expect(discover(bad_group).size() == 2 && errors.count(1001), "missing group is not matched as wildcard");
  Api exception;
  exception.throw_unknown = 1002;
  expect(discover(exception).size() == 2 && errors.count(1002) && exception.freed == 1,
         "non-standard per-account exception remains isolated");
  Api allocation;
  allocation.allocation_fails = true;
  expect(discover(allocation).empty() && errors.size() == 3 && allocation.freed == 1,
         "user allocation failure never creates a candidate");

  Api duplicate;
  duplicate.logins = {1001, 1001};
  rejects([&] { discover(duplicate); }, "duplicate login list must suspend discovery");
  expect(duplicate.freed == 1, "duplicate-list storage is released");
  Api zero;
  zero.logins = {0};
  rejects([&] { discover(zero); }, "zero login is rejected");
  expect(zero.freed == 1, "invalid-login storage is released");
  Api no_count;
  no_count.missing_count = true;
  rejects([&] { discover(no_count); }, "unwritten count cannot represent success");
  expect(no_count.freed == 1, "incomplete-list storage is released");
  Api no_pointer;
  no_pointer.missing_pointer = true;
  rejects([&] { discover(no_pointer); }, "positive count with no buffer is rejected");
  Api partial;
  partial.result = 2;
  rejects([&] { discover(partial); }, "partial/error result is not a complete list");
  expect(partial.freed == 1 && partial.reads == 0, "error output allocation is freed before account reads");
  Api thrown;
  thrown.throw_after_allocation = true;
  rejects([&] { discover(thrown); }, "enumeration exceptions are propagated");
  expect(thrown.freed == 1, "exception output allocation is freed");
  Api contradictory;
  contradictory.result = 1;
  rejects([&] { discover(contradictory); }, "no-data result with accounts is rejected");
  Api empty;
  empty.logins.clear();
  empty.result = 1;
  expect(discover(empty).empty() && empty.freed == 0, "documented no-data result is accepted");
  empty.result = 0;
  expect(discover(empty).empty(), "complete empty enumeration is accepted");
  Api cancelled;
  cancelled.cancelled = true;
  expect(discover(cancelled).empty() && cancelled.listings == 0, "stop avoids a new SDK scan");
  cancelled.cancelled = false;
  cancelled.cancel_after = 1001;
  expect(discover(cancelled).size() == 1 && cancelled.reads == 1 && cancelled.freed == 1,
         "stop during a scan cancels remaining accounts and releases storage");
}
}
int main() {
  try { tests(); std::cout << "MT5 discovery tests passed\n"; return 0; }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include "sdk_port.hpp"
#include "dynamicleverage/execution.hpp"
#include <climits>
#include <memory>
#include <set>
#include <utility>

namespace mt4leverage {
static_assert(ServerApiVersion == 5, "MT4 Server API 5 required");
static_assert(sizeof(void*) == 4 && sizeof(__time32_t) == 4, "MT4 requires Win32 ABI");
static_assert(OP_BUY == 0 && OP_SELL == 1 && OP_CREDIT == 7);
static_assert(TS_OPEN_NORMAL == 0 && TS_OPEN_REMAND == 1 && TS_OPEN_RESTORED == 2 && TS_DELETED == 6);
namespace {
void require_success(int code, const char* name) {
  if (code == FALSE) throw std::runtime_error(std::string(name) + " failed");
}
struct FreeOrders {
  void operator()(TradeRecord* value) const noexcept { if (value) HEAP_FREE(value); }
};
struct FreeUsers {
  void operator()(UserRecord* value) const noexcept { if (value) HEAP_FREE(value); }
};
std::string group_name(const UserRecord& user) {
  const auto raw = fixed(user.group);
  if (raw.empty()) throw std::runtime_error("MT4 account group is empty");
  return sdk_text_utf8(raw);
}
}

std::string sdk_text_utf8(const std::string& raw) {
  if (raw.empty()) return {};
  const auto size = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
  if (size <= 0) throw std::runtime_error("MT4 SDK text decoding failed");
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), wide.data(), size))
    throw std::runtime_error("MT4 SDK text decoding failed");
  const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), size, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) throw std::runtime_error("MT4 SDK text UTF-8 conversion failed");
  std::string result(static_cast<std::size_t>(bytes), '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), size, result.data(), bytes, nullptr, nullptr))
    throw std::runtime_error("MT4 SDK text UTF-8 conversion failed");
  return result;
}

void log(CServerInterface* api, const std::string& message) noexcept {
  try { if (api) api->LogsOut(CmdOK, "DynamicLeverage", message.c_str()); } catch (...) {}
}

UserRecord SdkPort::user(int login) {
  UserRecord result{};
  require_success(api_->ClientsUserInfo(login, &result), "ClientsUserInfo");
  if (result.login != login || result.leverage <= 0)
    throw std::runtime_error("MT4 account identity/leverage mismatch");
  return result;
}

dynamicleverage::Snapshot SdkPort::snapshot(dynamicleverage::Login login) {
  const auto record = user(native_login(login));
  UserInfo query{};
  query.login = record.login;
  // Populate the native short account object from the current full record.
  fixed(record.group); fixed(record.name);
  std::memcpy(query.group, record.group, sizeof(query.group));
  std::memcpy(query.name, record.name, sizeof(query.name));
  query.enable = record.enable; query.enable_change_password = record.enable_change_password;
  query.enable_read_only = record.enable_read_only; query.enable_flags = record.enable_flags;
  query.leverage = record.leverage; query.agent_account = record.agent_account;
  query.balance = record.balance; query.credit = record.credit; query.prevbalance = record.prevbalance;
  require_success(api_->GroupsGet(query.group, &query.grp), "GroupsGet");
  if (fixed(query.grp.group) != fixed(query.group)) throw std::runtime_error("MT4 account group mismatch");

  // -1 is an error sentinel: an untouched count can never become empty exposure.
  int total = -1;
  std::unique_ptr<TradeRecord, FreeOrders> records(api_->OrdersGetOpen(&query, &total));
  if (total < 0 || (total > 0 && !records)) throw std::runtime_error("OrdersGetOpen returned an incomplete snapshot");
  std::vector<Order> orders;
  orders.reserve(static_cast<std::size_t>(total));
  for (int i = 0; i < total; ++i) {
    const auto& item = records.get()[i];
    orders.push_back({item.order, item.login, item.cmd, item.volume, item.close_time, item.state});
  }
  return make_snapshot(record.login, record.leverage, orders, group_name(record));
}

bool SdkPort::set_leverage(dynamicleverage::Login, std::uint32_t, std::uint32_t) {
  dynamicleverage::require_native_execution();
}

std::vector<dynamicleverage::Account> SdkPort::accounts() {
  int total = -1;
  std::unique_ptr<UserRecord, FreeUsers> records(api_->ClientsAllUsers(&total));
  if (total < 0 || (total > 0 && !records))
    throw std::runtime_error("ClientsAllUsers returned an incomplete account list");
  std::vector<dynamicleverage::Account> result;
  std::set<int> seen;
  result.reserve(static_cast<std::size_t>(total));
  for (int i = 0; i < total; ++i) {
    const auto& record = records.get()[i];
    native_login(static_cast<dynamicleverage::Login>(record.login));
    if (!seen.insert(record.login).second) throw std::runtime_error("ClientsAllUsers returned duplicate accounts");
    result.push_back({static_cast<dynamicleverage::Login>(record.login), group_name(record)});
  }
  return result;
}

bool SdkPort::set_leverage_for_group(dynamicleverage::Login, std::uint32_t,
                                    std::uint32_t, const std::string&) {
  dynamicleverage::require_native_execution();
}
} // namespace mt4leverage

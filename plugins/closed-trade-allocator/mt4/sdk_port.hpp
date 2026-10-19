#pragma once
#include <windows.h>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include "MT4ServerAPI.h"
#include "model.hpp"
#include <memory>

namespace mt4alloc {
template<std::size_t N> std::string fixed(const char (&value)[N]) {
  const auto* end=static_cast<const char*>(std::memchr(value,0,N));
  if(!end) throw std::runtime_error("unterminated SDK field");
  return {value,end};
}
struct Account {
  UserInfo user{};
  double margin=0,free_margin=0,equity=0;
};
class SdkPort final : public closedalloc::Port {
public:
  SdkPort(CServerInterface* api,Parameters parameters) : api_(api),parameters_(std::move(parameters)) {}
  std::int64_t now();
  Account account(int login);
  std::vector<Record> history(int login);
  std::vector<Record> open_orders(const UserInfo&);
  Payload payload(const Record&);
  void block_submissions(bool value) { blocked_=value; }
  closedalloc::Result submit(const closedalloc::Job&,const closedalloc::SaveProgress&) override;
  closedalloc::Result reconcile(const closedalloc::Job&) override;
private:
  CServerInterface* api_;
  Parameters parameters_;
  bool blocked_=false;
};
Record copy(const TradeRecord&);
TradeRecord native(const Record&);
void log(CServerInterface*,const std::string&) noexcept;
} // namespace mt4alloc

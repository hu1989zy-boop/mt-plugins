#pragma once
#include "closedalloc/core.hpp"
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mt4alloc {
constexpr int api_namespace = 0x43414c31; // CAL1, private api_data namespace.
struct Record {
  int ticket=0, login=0, digits=0, side=0, volume=0, state=0, reason=0;
  std::int64_t opened=0, closed=0;
  std::string symbol, comment;
  double open_price=0, close_price=0, sl=0, tp=0;
  double profit=0, commission=0, agent_commission=0, swap=0, taxes=0;
  double rate_open=0, rate_close=0, margin_rate=0;
  int magic=0;
  std::array<int,4> api{};
};
struct Payload { Record record; std::string currency; unsigned currency_digits=2; };
std::string encode(const Payload&);
Payload decode(const std::string&);
std::string partial_reason(const Record&, const std::vector<Record>& all);
std::set<int> linked_tickets(const std::vector<Record>& all);
bool generated(const Record&);
void mark(Record&, std::uint64_t tag, int source_ticket);
bool tagged(const Record&, std::uint64_t tag, int source_ticket);
std::string mismatch(const Record& actual, const Record& wanted);
double money_round(double value, unsigned digits);

using Values=std::map<std::string,std::string>;
Values defaults();
struct Parameters {
  int source=0,target=0,server_build=0;
  std::string server_name,journal;
  double loss=0.3,profit=0.7;
  unsigned settle_seconds=30,poll_seconds=5,currency_digits=2;
  bool enabled=true,armed=false,direct_closed_add_verified=false;
  std::int64_t enabled_since=0;
  bool configured() const { return source!=0 || target!=0; }
};
Parameters parse(const Values&);
bool same_identity(const Parameters&,const Parameters&);
closedalloc::Config config(const Parameters&,const std::string& server);
} // namespace mt4alloc

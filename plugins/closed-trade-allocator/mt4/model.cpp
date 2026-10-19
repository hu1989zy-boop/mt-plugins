#include "model.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mt4alloc {
namespace {
int integer(closedalloc::Decoder& d) {
  const auto v=d.i64();
  if(v<INT_MIN || v>INT_MAX) throw std::runtime_error("MT4 payload integer overflow");
  return static_cast<int>(v);
}
std::uint64_t number(const std::string& v,std::uint64_t maximum) {
  if(v.empty() || v.find_first_not_of("0123456789")!=std::string::npos)
    throw std::runtime_error("parameter requires an unsigned decimal integer");
  std::size_t used=0; const auto n=std::stoull(v,&used);
  if(used!=v.size() || n>maximum) throw std::runtime_error("parameter integer outside allowed range");
  return n;
}
double percent(const std::string& v) {
  if(v.empty() || v.find_first_not_of("0123456789.")!=std::string::npos)
    throw std::runtime_error("probability must be a percentage in [0,100]");
  std::size_t used=0; const auto n=std::stod(v,&used);
  if(used!=v.size() || !std::isfinite(n) || n<0 || n>100)
    throw std::runtime_error("percentage outside [0,100]");
  return n/100.0;
}
bool link(const std::string& c) {
  return c.find("from #")!=std::string::npos || c.find("to #")!=std::string::npos;
}
bool refers(const std::string& c,int ticket) {
  for(const auto* prefix:{"from #","to #"}) {
    auto pos=c.find(prefix);
    while(pos!=std::string::npos) {
      pos+=std::char_traits<char>::length(prefix);
      const auto end=c.find_first_not_of("0123456789",pos);
      if(c.substr(pos,end==std::string::npos?end:end-pos)==std::to_string(ticket)) return true;
      pos=c.find(prefix,pos);
    }
  }
  return false;
}
}
std::string encode(const Payload& p) {
  closedalloc::Encoder e; e.text("mt4-closed-v1"); const auto& r=p.record;
  for(const auto n:{r.ticket,r.login,r.digits,r.side,r.volume,r.state,r.reason}) e.i64(n);
  e.i64(r.opened); e.i64(r.closed); e.text(r.symbol); e.text(r.comment);
  for(const auto n:{r.open_price,r.close_price,r.sl,r.tp,r.profit,r.commission,
      r.agent_commission,r.swap,r.taxes,r.rate_open,r.rate_close,r.margin_rate}) e.real(n);
  e.i64(r.magic); for(const auto n:r.api) e.i64(n);
  e.text(p.currency); e.u64(p.currency_digits); return e.data();
}
Payload decode(const std::string& text) {
  closedalloc::Decoder d(text); if(d.text()!="mt4-closed-v1") throw std::runtime_error("MT4 payload version mismatch");
  Payload p; auto& r=p.record;
  r.ticket=integer(d);r.login=integer(d);r.digits=integer(d);r.side=integer(d);
  r.volume=integer(d);r.state=integer(d);r.reason=integer(d);r.opened=d.i64();r.closed=d.i64();
  r.symbol=d.text();r.comment=d.text();r.open_price=d.real();r.close_price=d.real();
  r.sl=d.real();r.tp=d.real();r.profit=d.real();r.commission=d.real();r.agent_commission=d.real();
  r.swap=d.real();r.taxes=d.real();r.rate_open=d.real();r.rate_close=d.real();r.margin_rate=d.real();
  r.magic=integer(d); for(auto& n:r.api) n=integer(d);
  p.currency=d.text(); const auto digits=d.u64(); if(digits>8) throw std::runtime_error("invalid currency precision");
  p.currency_digits=static_cast<unsigned>(digits); d.finish(); return p;
}
bool generated(const Record& r) { return r.api[0]==api_namespace || r.magic==api_namespace; }
void mark(Record& r,std::uint64_t tag,int ticket) {
  r.magic=api_namespace;
  // Conversion preserves all 32 bits on the required Windows two's-complement ABI.
  r.api={api_namespace,static_cast<int>(static_cast<std::uint32_t>(tag)),
    static_cast<int>(static_cast<std::uint32_t>(tag>>32)),ticket};
  r.reason=7; r.comment.clear();
}
bool tagged(const Record& r,std::uint64_t tag,int ticket) {
  return r.api[0]==api_namespace && r.api[3]==ticket &&
    static_cast<std::uint32_t>(r.api[1])==static_cast<std::uint32_t>(tag) &&
    static_cast<std::uint32_t>(r.api[2])==static_cast<std::uint32_t>(tag>>32);
}
std::string partial_reason(const Record& r,const std::vector<Record>& all) {
  if(r.state==1 || r.state==4 || link(r.comment)) return "partial close or remainder chain is unsupported";
  for(const auto& other:all) if(other.login==r.login && other.ticket!=r.ticket && refers(other.comment,r.ticket))
    return "linked partial-close ticket is unsupported";
  if(r.state!=3) return "only TS_CLOSED_NORMAL complete closes are supported";
  return {};
}
std::set<int> linked_tickets(const std::vector<Record>& all) {
  std::set<int> result;
  for(const auto& r:all)for(const auto* prefix:{"from #","to #"}) {
    auto pos=r.comment.find(prefix);
    while(pos!=std::string::npos) {
      pos+=std::char_traits<char>::length(prefix);
      const auto end=r.comment.find_first_not_of("0123456789",pos);
      const auto ticket=r.comment.substr(pos,end==std::string::npos?end:end-pos);
      if(!ticket.empty())try{const auto n=number(ticket,INT_MAX);if(n)result.insert(static_cast<int>(n));}catch(const std::exception&){}
      pos=r.comment.find(prefix,pos);
    }
  }
  return result;
}
std::string mismatch(const Record& a,const Record& b) {
  if(a.login!=b.login || a.symbol!=b.symbol || a.digits!=b.digits || a.side!=b.side ||
     a.volume!=b.volume || a.state!=b.state || a.opened!=b.opened || a.closed!=b.closed)
    return "target identity, direction, volume, state or source times differ";
  if(a.open_price!=b.open_price || a.close_price!=b.close_price || a.sl!=b.sl || a.tp!=b.tp)
    return "target price, SL or TP differs";
  if(a.profit!=b.profit || a.commission!=b.commission || a.agent_commission!=b.agent_commission ||
     a.swap!=b.swap || a.taxes!=b.taxes) return "target profit or fees differ";
  if(a.rate_open!=b.rate_open || a.rate_close!=b.rate_close || a.margin_rate!=b.margin_rate)
    return "target currency conversion metadata differs";
  if(a.api!=b.api || a.magic!=b.magic || a.reason!=7 || !a.comment.empty())
    return "target API provenance or empty comment differs";
  return {};
}
double money_round(double value,unsigned digits) {
  if(digits>8 || !std::isfinite(value) || std::abs(value)>1e13)
    throw std::runtime_error("invalid money value");
  const auto scale=std::pow(10.0,static_cast<int>(digits)); return std::round(value*scale)/scale;
}
Values defaults() {
  return {{"Armed","0"},{"Enabled","1"},{"SourceLogin","0"},{"TargetLogin","0"},
    {"ServerName",""},{"ServerBuild","0"},{"JournalPath",""},
    {"LossPercent","30"},{"ProfitPercent","70"},{"EnabledSince","0"},
    {"SettleSeconds","30"},{"PollSeconds","5"},{"CurrencyDigits","2"},{"DirectClosedAddVerified","0"}};
}
Parameters parse(const Values& v) {
  const auto initial=defaults(); if(v.size()!=initial.size()) throw std::runtime_error("invalid parameter set");
  for(const auto& item:v) if(!initial.count(item.first) || item.second.size()>=128 || item.second.find('\0')!=std::string::npos)
    throw std::runtime_error("unknown or invalid MT4 parameter");
  Parameters p;
  p.source=static_cast<int>(number(v.at("SourceLogin"),INT_MAX));
  p.target=static_cast<int>(number(v.at("TargetLogin"),INT_MAX));
  p.server_build=static_cast<int>(number(v.at("ServerBuild"),INT_MAX));
  p.server_name=v.at("ServerName");p.journal=v.at("JournalPath");
  p.loss=percent(v.at("LossPercent"));p.profit=percent(v.at("ProfitPercent"));
  p.armed=number(v.at("Armed"),1)!=0;p.enabled=number(v.at("Enabled"),1)!=0;
  p.direct_closed_add_verified=number(v.at("DirectClosedAddVerified"),1)!=0;
  p.enabled_since=static_cast<std::int64_t>(number(v.at("EnabledSince"),INT_MAX));
  p.settle_seconds=static_cast<unsigned>(number(v.at("SettleSeconds"),3600));
  p.poll_seconds=static_cast<unsigned>(number(v.at("PollSeconds"),300));
  p.currency_digits=static_cast<unsigned>(number(v.at("CurrencyDigits"),8));
  if(p.settle_seconds<5 || p.poll_seconds<1) throw std::runtime_error("SettleSeconds >=5 and PollSeconds >=1 required");
  if(p.configured() && (!p.source || !p.target || p.source==p.target || p.server_name.empty() || p.server_name.size()>=32 ||
      !p.server_build || p.journal.empty())) throw std::runtime_error("incomplete route, server identity or journal path");
  if(p.armed && (!p.configured() || !p.direct_closed_add_verified))
    throw std::runtime_error("Armed=1 requires configured route and DirectClosedAddVerified=1 after Demo validation");
  return p;
}
bool same_identity(const Parameters& a,const Parameters& b) {
  return a.source==b.source && a.target==b.target && a.server_name==b.server_name && a.server_build==b.server_build &&
    a.journal==b.journal && a.enabled_since==b.enabled_since && a.currency_digits==b.currency_digits &&
    a.settle_seconds==b.settle_seconds && a.poll_seconds==b.poll_seconds &&
    a.direct_closed_add_verified==b.direct_closed_add_verified;
}
closedalloc::Config config(const Parameters& p,const std::string& server) {
  closedalloc::Config c; c.platform="mt4";c.server=server;c.source=p.source;c.target=p.target;
  c.loss_probability=p.loss;c.profit_probability=p.profit;c.enabled_since=p.enabled_since;
  c.enabled=p.enabled;c.armed=p.armed;return c;
}
} // namespace mt4alloc

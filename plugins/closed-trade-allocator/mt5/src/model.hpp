#pragma once
#include "closedalloc/core.hpp"
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <limits>
#include <map>

namespace closedalloc::mt5 {
// SDK IMTOrder/IMTDeal::ApiDataSet: app 1..4095, slot 1..15,
// at most eight parameters in a record. Seven fields are owned here.
inline constexpr std::uint16_t kApiApp=0xCA5;
inline constexpr std::uint64_t kApiMagic=0x434c4f5345440001ULL;
inline constexpr std::uint8_t kSlotMagic=1,kSlotTag=2,kSlotRole=3,kSlotSource=4,
                              kSlotPosition=5,kSlotTicket=6,kSlotWriteTime=7;
inline constexpr std::array<std::uint8_t,7> kApiSlots{kSlotMagic,kSlotTag,kSlotRole,kSlotSource,kSlotPosition,kSlotTicket,kSlotWriteTime};
constexpr bool valid_api_slots() {
  if(kApiApp<1||kApiApp>4095||kApiSlots.size()>8)return false;
  for(std::size_t i=0;i<kApiSlots.size();++i) {
    if(kApiSlots[i]<1||kApiSlots[i]>15)return false;
    for(std::size_t j=0;j<i;++j)if(kApiSlots[i]==kApiSlots[j])return false;
  }
  return true;
}
static_assert(valid_api_slots(),"MT5 API marker fields exceed SDK limits");
constexpr bool has_exit_fields(std::uint32_t entry) {return entry==1||entry==2;}
constexpr bool has_trigger_price(std::uint32_t type) {return type==6||type==7;}
constexpr bool has_pair_capacity(std::uint64_t orders,std::uint64_t deals,std::uint64_t limit) {
  return limit>=2&&orders<=limit-2&&deals<=limit-2;
}
// SDK-independent immutable records. Comments deliberately have no field.
struct Order {
  std::uint64_t ticket=0, login=0, position=0, volume=0, remaining=0;
  std::int64_t setup=0, done=0, expiration=0;
  std::uint32_t type=0, state=0, digits=0, currency_digits=0, fill=0, time_type=0;
  std::string symbol;
  // contract, requested/current price, SL, TP, trigger, margin conversion.
  std::array<double,7> values{};
};
struct Deal {
  std::uint64_t ticket=0, login=0, order=0, position=0, volume=0, closed_volume=0;
  std::int64_t time=0;
  std::uint32_t action=0, entry=0, digits=0, currency_digits=0;
  std::string symbol;
  // contract, price, profit, commission, swap, fee, SL, TP,
  // raw profit, position price, profit/margin conversion, tick value/size.
  std::array<double,14> values{};
};
struct Snapshot { Order opening_order, closing_order; Deal opening, closing; std::string currency; };
inline bool same_position_deal_set(const Snapshot& s,const std::vector<std::uint64_t>& tickets) {
  return tickets.size()==2&&s.opening.ticket!=s.closing.ticket&&
         ((tickets[0]==s.opening.ticket&&tickets[1]==s.closing.ticket)||
          (tickets[1]==s.opening.ticket&&tickets[0]==s.closing.ticket));
}
class Stability {
public:
  bool ready(const std::string& id,const std::string& payload,std::int64_t now,unsigned seconds) {
    auto it=seen_.find(id);
    if(it==seen_.end()||it->second.payload!=payload||now<it->second.since) {
      seen_[id]={payload,now};return false;
    }
    return now-it->second.since>=static_cast<std::int64_t>(seconds);
  }
private:
  struct Entry {std::string payload;std::int64_t since;};
  std::map<std::string,Entry> seen_;
};

inline void encode(Encoder& e,const Order& o) {
  for(auto x:{o.ticket,o.login,o.position,o.volume,o.remaining}) e.u64(x);
  for(auto x:{o.setup,o.done,o.expiration}) e.i64(x);
  for(auto x:{o.type,o.state,o.digits,o.currency_digits,o.fill,o.time_type}) e.u64(x);
  e.text(o.symbol); for(auto x:o.values) e.real(x);
}
inline void encode(Encoder& e,const Deal& d) {
  for(auto x:{d.ticket,d.login,d.order,d.position,d.volume,d.closed_volume}) e.u64(x);
  e.i64(d.time); for(auto x:{d.action,d.entry,d.digits,d.currency_digits}) e.u64(x);
  e.text(d.symbol); for(auto x:d.values) e.real(x);
}
inline std::uint32_t decode_u32(Decoder& d) {
  const auto n=d.u64();if(n>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("MT5 snapshot integer overflow");
  return static_cast<std::uint32_t>(n);
}
inline Order decode_order(Decoder& d) {
  Order o; o.ticket=d.u64();o.login=d.u64();o.position=d.u64();o.volume=d.u64();o.remaining=d.u64();
  o.setup=d.i64();o.done=d.i64();o.expiration=d.i64();o.type=decode_u32(d);
  o.state=decode_u32(d);o.digits=decode_u32(d);
  o.currency_digits=decode_u32(d);o.fill=decode_u32(d);
  o.time_type=decode_u32(d);o.symbol=d.text();for(auto& x:o.values)x=d.real();return o;
}
inline Deal decode_deal(Decoder& d) {
  Deal o;o.ticket=d.u64();o.login=d.u64();o.order=d.u64();o.position=d.u64();o.volume=d.u64();
  o.closed_volume=d.u64();o.time=d.i64();o.action=decode_u32(d);
  o.entry=decode_u32(d);o.digits=decode_u32(d);
  o.currency_digits=decode_u32(d);o.symbol=d.text();for(auto& x:o.values)x=d.real();return o;
}
inline std::string encode(const Snapshot& s) {
  Encoder e;e.text("mt5-closed-v1");e.text(s.currency);encode(e,s.opening_order);encode(e,s.closing_order);
  encode(e,s.opening);encode(e,s.closing);return e.data();
}
inline Snapshot decode(const std::string& payload) {
  Decoder d(payload);if(d.text()!="mt5-closed-v1")throw std::runtime_error("unknown MT5 snapshot version");
  Snapshot s;s.currency=d.text();s.opening_order=decode_order(d);s.closing_order=decode_order(d);
  s.opening=decode_deal(d);s.closing=decode_deal(d);d.finish();return s;
}
inline std::string validate_pair(const Snapshot& s) {
  const auto& a=s.opening; const auto& b=s.closing;
  if(!a.ticket||!b.ticket||a.ticket==b.ticket||!a.position||a.position!=b.position||a.login!=b.login)
    return "invalid source identity";
  if(a.action>1||b.action>1||a.action==b.action||a.entry!=0||b.entry!=1)
    return "unsupported reversal, close-by or non-trade deal";
  if(!a.volume||a.volume!=b.volume||a.closed_volume||b.closed_volume!=b.volume)
    return "partial close or unequal opening/closing volume is unsupported";
  if(a.values[8]!=0||a.values[9]!=0)return "opening deal contains exit-only fields";
  if(a.time<=0||b.time<a.time||a.symbol.empty()||a.symbol!=b.symbol||s.currency.empty())
    return "invalid source time, symbol or currency";
  if(a.digits!=b.digits||a.currency_digits!=b.currency_digits||a.values[0]!=b.values[0])
    return "source contract or currency metadata changed";
  for(const auto* x:{&a,&b}) for(double v:x->values)if(!std::isfinite(v))return "nonfinite deal value";
  for(const auto* x:{&s.opening_order,&s.closing_order}) {
    for(double v:x->values)if(!std::isfinite(v))return "nonfinite order value";
    if(x->state!=4||x->remaining||x->volume!=a.volume||x->login!=a.login||x->position!=a.position||
       x->symbol!=a.symbol||x->done<x->setup||x->setup<=0||x->currency_digits!=a.currency_digits||
       x->digits!=a.digits||x->values[0]!=a.values[0]) return "incomplete or incompatible source history order";
  }
  if(a.order!=s.opening_order.ticket||b.order!=s.closing_order.ticket||a.order==b.order)
    return "source order/deal association mismatch";
  if(s.opening_order.type>7||s.opening_order.type%2!=a.action)return "opening order direction mismatch";
  if(s.closing_order.type!=b.action)return "closing order direction mismatch";
  // Pending opening orders may resolve into BUY/SELL; the deal is authoritative.
  if(a.values[0]<=0||a.values[1]<=0||b.values[1]<=0||a.currency_digits>8||a.digits>12)
    return "invalid price, contract or precision";
  return {};
}
inline Trade make_trade(const Snapshot& s) {
  Trade t;t.id=std::to_string(s.opening.position);t.closed_at=s.closing.time/1000;
  t.currency_digits=s.closing.currency_digits;t.profit=s.opening.values[2]+s.closing.values[2];
  t.commission=s.opening.values[3]+s.closing.values[3];t.swap=s.opening.values[4]+s.closing.values[4];
  t.fee=s.opening.values[5]+s.closing.values[5];t.payload=encode(s);t.exclusion=validate_pair(s);return t;
}
} // namespace closedalloc::mt5

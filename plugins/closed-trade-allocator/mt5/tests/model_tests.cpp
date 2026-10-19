#include "model.hpp"
#include <iostream>
#include <stdexcept>
using namespace closedalloc::mt5;
void expect(bool b,const char* what){if(!b)throw std::runtime_error(what);}
Snapshot sample() {
  Snapshot s;s.currency="USD";
  s.opening.ticket=100;s.opening.login=10;s.opening.order=90;s.opening.position=90;
  s.opening.volume=100000000;s.opening.time=1700000000000;s.opening.action=0;s.opening.entry=0;
  s.opening.digits=5;s.opening.currency_digits=2;s.opening.symbol="EURUSD";
  s.opening.values={100000,1.1,0,-2,0,-0.2,1.08,1.15,0,0,1,1,1,0.00001};
  s.closing=s.opening;s.closing.ticket=101;s.closing.order=91;s.closing.time+=3600000;
  s.closing.action=1;s.closing.entry=1;s.closing.closed_volume=s.closing.volume;
  s.closing.values={100000,1.105,500,-2,-3,-0.2,1.09,1.16,500,1.1,1,1,1,0.00001};
  s.opening_order.ticket=90;s.opening_order.login=10;s.opening_order.position=90;
  s.opening_order.volume=s.opening.volume;s.opening_order.setup=s.opening.time;s.opening_order.done=s.opening.time;
  s.opening_order.type=0;s.opening_order.state=4;s.opening_order.digits=5;s.opening_order.currency_digits=2;
  s.opening_order.symbol="EURUSD";s.opening_order.values={100000,1.1,1.1,1.08,1.15,0,1};
  s.closing_order=s.opening_order;s.closing_order.ticket=91;s.closing_order.type=1;
  s.closing_order.setup=s.closing.time;s.closing_order.done=s.closing.time;
  s.closing_order.values={100000,1.105,1.105,1.09,1.16,0,1};return s;
}
int main() {
  try {
    expect(valid_api_slots(),"custom API marker fields violate SDK app/slot/count constraints");
    expect(!has_exit_fields(0)&&has_exit_fields(1)&&has_exit_fields(2)&&!has_exit_fields(3),"exit-only SDK field applicability is incorrect");
    expect(!has_trigger_price(0)&&!has_trigger_price(2)&&has_trigger_price(6)&&has_trigger_price(7),"stop-limit SDK field applicability is incorrect");
    expect(has_pair_capacity(99998,99998,100000),"last complete pair capacity rejected");
    expect(!has_pair_capacity(99999,0,100000)&&!has_pair_capacity(0,99999,100000)&&!has_pair_capacity(100000,100000,100000),"insufficient pair capacity accepted");
    const auto original=sample();expect(validate_pair(original).empty(),"complete pair rejected");
    expect(same_position_deal_set(original,{100,101})&&same_position_deal_set(original,{101,100}),"frozen position deal set rejected");
    expect(!same_position_deal_set(original,{100,101,102}),"late third fee/correction deal would bypass frozen-net validation");
    expect(!same_position_deal_set(original,{100})&&!same_position_deal_set(original,{100,100})&&!same_position_deal_set(original,{100,102}),"missing, duplicate or replaced source deal accepted");
    const auto bytes=encode(original);const auto restored=decode(bytes);
    expect(encode(restored)==bytes,"snapshot did not round-trip exactly");
    expect(restored.opening_order.values[3]==1.08&&restored.opening_order.values[4]==1.15&&
           restored.closing.values[6]==1.09&&restored.closing.values[7]==1.16,"SL/TP lost or substituted across lifecycle");
    const auto trade=make_trade(restored);expect(trade.id=="90"&&trade.closed_at==1700003600,"wrong dedup identity or close time");
    expect(std::abs(closedalloc::net_profit(trade)-492.6)<0.00001,"opening/closing costs omitted or double counted");
    auto bad=original;bad.closing.volume/=2;expect(!validate_pair(bad).empty(),"partial closing accepted");
    bad=original;bad.closing.entry=2;expect(!validate_pair(bad).empty(),"reversal accepted");
    bad=original;bad.closing.entry=3;expect(!validate_pair(bad).empty(),"close-by accepted");
    bad=original;bad.closing_order.remaining=1;expect(!validate_pair(bad).empty(),"unfinished order accepted");
    bad=original;bad.closing_order.position=1;expect(!validate_pair(bad).empty(),"broken association accepted");
    bad=original;bad.closing.currency_digits=3;expect(!validate_pair(bad).empty(),"currency precision mismatch accepted");
    bad=original;bad.opening.values[9]=1.1;expect(!validate_pair(bad).empty(),"entry deal with exit-only position price accepted");
    bad=original;bad.opening.values[3]=-510;const auto loss=make_trade(bad);
    expect(closedalloc::net_profit(loss)<0,"positive gross profit did not become a loss after opening commission");
    bool rejected=false;try{decode(bytes+"x");}catch(const std::exception&){rejected=true;}expect(rejected,"trailing payload corruption accepted");
    rejected=false;try{decode(bytes.substr(0,bytes.size()-1));}catch(const std::exception&){rejected=true;}expect(rejected,"truncated payload accepted");
    Stability stable;expect(!stable.ready("90",bytes,100,30),"first source observation accepted");
    expect(!stable.ready("90",bytes,129,30),"source stability window skipped");
    expect(stable.ready("90",bytes,130,30),"stable second source observation rejected");
    expect(!stable.ready("90",bytes+"revision",131,30),"source revision did not reset stability");
    expect(!stable.ready("90",bytes+"revision",160,30),"revised source accepted too early");
    expect(stable.ready("90",bytes+"revision",161,30),"stable revised source not surfaced to core");
    expect(!stable.ready("90",bytes+"revision",90,30),"server clock reversal accepted");
    std::cout<<"MT5 closed-pair model tests passed\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

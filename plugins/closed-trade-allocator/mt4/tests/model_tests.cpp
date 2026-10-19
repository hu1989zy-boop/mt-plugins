#include "model.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F function,const char* message){bool failed=false;try{function();}catch(const std::exception&){failed=true;}check(failed,message);}
mt4alloc::Payload sample() {
  mt4alloc::Payload p;p.currency="USD";auto& r=p.record;
  r.ticket=101;r.login=1001;r.symbol="EURUSD";r.digits=5;r.side=0;r.volume=100;r.state=3;
  r.opened=1700000000;r.closed=1700000123;r.open_price=1.125;r.close_price=1.13;r.sl=1.10;r.tp=1.15;
  r.profit=50;r.commission=-7;r.swap=-1;r.taxes=-0.1;r.rate_open=1;r.rate_close=1;r.margin_rate=1;
  r.comment="source text must not be copied";return p;
}
}
int main() {
  try {
    using namespace mt4alloc;
    const auto p=sample();const auto data=encode(p);const auto restored=decode(data);
    check(encode(restored)==data,"payload round trip loses trade fields");
    check(restored.record.sl==1.10 && restored.record.tp==1.15,"SL/TP not persisted");
    rejects([&]{decode(data+"x");},"payload trailing bytes accepted");
    rejects([&]{decode(data.substr(0,data.size()-1));},"truncated payload accepted");
    const std::uint64_t tag=0xfedcba9876543210ULL;
    auto target=restored.record;target.login=2001;target.ticket=0;mark(target,tag,p.record.ticket);
    check(target.comment.empty() && target.reason==7,"comment/provenance incorrect");
    check(target.sl==p.record.sl && target.tp==p.record.tp,"marking changes SL/TP");
    check(generated(target) && tagged(target,tag,101) && !tagged(target,tag,102) && !tagged(target,tag+1,101),"API tag mismatch");
    auto actual=target;actual.ticket=456;
    check(mismatch(actual,target).empty(),"server-generated target ticket rejected");
    actual.sl=0;check(!mismatch(actual,target).empty(),"SL omission not detected");actual=target;
    actual.tp=0;check(!mismatch(actual,target).empty(),"TP omission not detected");actual=target;
    actual.comment="copied";check(!mismatch(actual,target).empty(),"target comment accepted");actual=target;
    actual.commission=0;check(!mismatch(actual,target).empty(),"fees mismatch not detected");
    auto source=p.record;check(partial_reason(source,{source}).empty(),"complete close rejected");
    source.state=4;check(!partial_reason(source,{source}).empty(),"partial state accepted");
    source=p.record;source.comment="from #77";check(!partial_reason(source,{source}).empty(),"remainder chain accepted");
    source=p.record;auto remainder=source;remainder.ticket=102;remainder.state=1;remainder.closed=0;remainder.comment="from #101";
    check(!partial_reason(source,{source,remainder}).empty(),"partial sibling chain accepted");
    check(linked_tickets({source,remainder}).count(101)==1,"runtime partial ticket index missed a chain");
    remainder.comment="from #1010";check(partial_reason(source,{source,remainder}).empty(),"ticket prefix treated as same ticket");
    check(linked_tickets({source,remainder}).count(101)==0,"runtime index confused a ticket prefix");
    const auto initial=parse(defaults());check(!initial.armed && !initial.configured(),"unsafe initial defaults");
    auto values=defaults();values["SourceLogin"]="1001";values["TargetLogin"]="2001";values["ServerName"]="Demo";
    values["ServerBuild"]="1440";values["JournalPath"]="C:\\allocator\\state.log";
    auto configured=parse(values);check(configured.configured(),"valid configuration rejected");
    values["Armed"]="1";rejects([&]{parse(values);},"arming without Demo verification accepted");
    values["DirectClosedAddVerified"]="1";check(parse(values).armed,"verified arm rejected");
    values["LossPercent"]="100";values["ProfitPercent"]="100";check(parse(values).loss==1 && parse(values).profit==1,"probabilities incorrectly required to sum to one");
    values["LossPercent"]="30trailing";rejects([&]{parse(values);},"malformed probability accepted");
    values["LossPercent"]="30";values["TargetLogin"]="1001";rejects([&]{parse(values);},"self allocation accepted");
    values["TargetLogin"]="2001";values["SourceLogin"]="2147483648";rejects([&]{parse(values);},"MT4 login overflow accepted");
    check(money_round(-0.004,2)==0 && money_round(-0.006,2)==-0.01,"currency rounding incorrect");
    std::cout<<"MT4 model tests passed\n";return EXIT_SUCCESS;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}

#include "sdk_port.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace mt4alloc {
static_assert(ServerApiVersion==5,"MT4 Server API 5 required");
static_assert(sizeof(void*)==4 && sizeof(__time32_t)==4,"MT4 requires the Win32 ABI");
static_assert(TR_REASON_API==7 && TS_CLOSED_NORMAL==3 && TS_CLOSED_PART==4 && TS_OPEN_REMAND==1);
namespace {
template<std::size_t N> void put(char (&out)[N],const std::string& text) {
  if(text.size()>=N || text.find('\0')!=std::string::npos) throw std::runtime_error("SDK field length overflow");
  std::memset(out,0,N);std::memcpy(out,text.data(),text.size());
}
void found(int code,const char* label) {
  // The supplied Server API examples use BOOL for account/group/order lookups.
  if(code==FALSE) throw std::runtime_error(std::string(label)+" failed");
}
struct Free { void operator()(TradeRecord* p) const noexcept { if(p) HEAP_FREE(p); } };
std::vector<Record> records(TradeRecord* raw,int total,int login) {
  std::unique_ptr<TradeRecord,Free> owner(raw);
  if(total<0 || total>100000 || (total>0 && !raw)) throw std::runtime_error("invalid or oversized SDK history (limit 100000)");
  std::vector<Record> result;result.reserve(static_cast<std::size_t>(total));std::set<int> tickets;
  for(int i=0;i<total;++i) {
    if(raw[i].login!=login || raw[i].order<=0 || !tickets.insert(raw[i].order).second)
      throw std::runtime_error("SDK snapshot account/ticket mismatch");
    result.push_back(copy(raw[i]));
  }
  std::sort(result.begin(),result.end(),[](const Record& a,const Record& b){return a.ticket<b.ticket;});
  return result;
}
std::string digest(const std::vector<Record>& records) {
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  auto check=[](NTSTATUS code){if(code<0) throw std::runtime_error("history SHA256 failed");};
  try {
    check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    DWORD size=0,result=0;
    check(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&result,0));
    std::vector<unsigned char> object(size);
    check(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0));
    for(const auto& record:records) {
      const auto bytes=encode({record,"",2});
      check(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0));
    }
    unsigned char value[32]{};check(BCryptFinishHash(hash,value,sizeof(value),0));
    BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;
    constexpr char hex[]="0123456789abcdef";std::string text;
    for(const auto byte:value) {text+=hex[byte>>4];text+=hex[byte&15];}return text;
  } catch(...) {if(hash) BCryptDestroyHash(hash);if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
struct Evidence {
  std::int64_t attempted_at=0;
  double balance=0,credit=0,margin=0,free_margin=0,equity=0;
  std::string history_hash;
  bool returned=false;
  int sdk_result=0;
};
std::string encode_evidence(const Evidence& a) {
  closedalloc::Encoder e;e.text("mt4-orders-add-v1");e.i64(a.attempted_at);
  for(const auto v:{a.balance,a.credit,a.margin,a.free_margin,a.equity})e.real(v);
  e.text(a.history_hash);e.u64(a.returned?1:0);e.i64(a.sdk_result);return e.data();
}
Evidence decode_evidence(const std::string& data) {
  closedalloc::Decoder d(data);if(d.text()!="mt4-orders-add-v1")throw std::runtime_error("missing/unsupported MT4 write evidence");
  Evidence a;a.attempted_at=d.i64();a.balance=d.real();a.credit=d.real();a.margin=d.real();a.free_margin=d.real();a.equity=d.real();
  a.history_hash=d.text();const auto returned=d.u64();const auto code=d.i64();d.finish();
  if(returned>1 || code<INT_MIN || code>INT_MAX || a.history_hash.size()!=64 || a.attempted_at<=0)
    throw std::runtime_error("invalid MT4 write evidence");
  a.returned=returned!=0;a.sdk_result=static_cast<int>(code);return a;
}
bool amount(double a,double b,unsigned digits) {return money_round(a,digits)==money_round(b,digits);}
Record wanted(const Payload& source,int target,std::uint64_t tag) {
  Record record=source.record;record.ticket=0;record.login=target;record.state=TS_CLOSED_NORMAL;
  mark(record,tag,source.record.ticket);return record;
}
std::string source_valid(const Payload& p,const Parameters& parameters) {
  const auto& r=p.record;
  if(r.login!=parameters.source || r.ticket<=0 || r.closed<=0 || r.closed>INT_MAX || r.opened<=0 ||
     r.opened>r.closed || (r.side!=OP_BUY && r.side!=OP_SELL) || r.volume<=0 ||
     r.state!=TS_CLOSED_NORMAL || r.digits<0 || r.digits>12 || r.symbol.empty() || r.symbol.size()>=12 ||
     r.open_price<=0 || r.close_price<=0 || r.sl<0 || r.tp<0 || generated(r))
    return "invalid/non-final MT4 source payload";
  // Agent commission can affect a third-party agent account in some deployments.
  if(r.agent_commission!=0) return "nonzero agent commission requires separate Demo validation; unsupported in v1";
  if(p.currency_digits!=parameters.currency_digits || p.currency.empty()) return "source currency precision changed";
  return {};
}
}
Record copy(const TradeRecord& t) {
  Record r;r.ticket=t.order;r.login=t.login;r.digits=t.digits;r.side=t.cmd;r.volume=t.volume;
  r.state=t.state;r.reason=t.reason;r.opened=t.open_time;r.closed=t.close_time;r.symbol=fixed(t.symbol);r.comment=fixed(t.comment);
  r.open_price=t.open_price;r.close_price=t.close_price;r.sl=t.sl;r.tp=t.tp;r.profit=t.profit;
  r.commission=t.commission;r.agent_commission=t.commission_agent;r.swap=t.storage;r.taxes=t.taxes;
  r.rate_open=t.conv_rates[0];r.rate_close=t.conv_rates[1];r.margin_rate=t.margin_rate;r.magic=t.magic;
  std::copy(std::begin(t.api_data),std::end(t.api_data),r.api.begin());return r;
}
TradeRecord native(const Record& r) {
  TradeRecord t{};t.order=r.ticket;t.login=r.login;t.digits=r.digits;t.cmd=r.side;t.volume=r.volume;
  t.state=r.state;t.reason=static_cast<char>(r.reason);t.open_time=static_cast<__time32_t>(r.opened);
  t.close_time=static_cast<__time32_t>(r.closed);put(t.symbol,r.symbol);put(t.comment,r.comment);
  t.open_price=r.open_price;t.close_price=r.close_price;t.sl=r.sl;t.tp=r.tp;t.profit=r.profit;
  t.commission=r.commission;t.commission_agent=r.agent_commission;t.storage=r.swap;t.taxes=r.taxes;
  t.conv_rates[0]=r.rate_open;t.conv_rates[1]=r.rate_close;t.margin_rate=r.margin_rate;t.magic=r.magic;
  std::copy(r.api.begin(),r.api.end(),std::begin(t.api_data));return t;
}
void log(CServerInterface* api,const std::string& message) noexcept {
  if(api)try{api->LogsOut(CmdAtt,"closed-allocator",message.c_str());}catch(...){}
}
std::int64_t SdkPort::now() {
  const auto t=api_->TradeTime();if(t<=0)throw std::runtime_error("invalid MT4 server time");return t;
}
Account SdkPort::account(int login) {
  UserRecord raw{};found(api_->ClientsUserInfo(login,&raw),"ClientsUserInfo");
  if(raw.login!=login)throw std::runtime_error("account identity mismatch");
  Account a;auto& u=a.user;u.login=raw.login;put(u.group,fixed(raw.group));
  u.enable=raw.enable;u.enable_read_only=raw.enable_read_only;u.enable_flags=raw.enable_flags;
  u.enable_change_password=raw.enable_change_password;u.leverage=raw.leverage;u.agent_account=raw.agent_account;
  u.balance=raw.balance;u.credit=raw.credit;u.prevbalance=raw.prevbalance;
  found(api_->GroupsGet(u.group,&u.grp),"GroupsGet");
  if(fixed(u.grp.group)!=fixed(u.group))throw std::runtime_error("account group mismatch");
  a.margin=a.free_margin=a.equity=std::numeric_limits<double>::quiet_NaN();
  api_->TradesMarginInfo(&u,&a.margin,&a.free_margin,&a.equity);
  for(const auto value:{u.balance,u.credit,a.margin,a.free_margin,a.equity})
    if(!std::isfinite(value) || std::abs(value)>1e13)throw std::runtime_error("invalid account financial snapshot");
  return a;
}
std::vector<Record> SdkPort::history(int login) {
  int total=-1;auto* raw=api_->OrdersGetClosed(0,INT_MAX,&login,1,&total);return records(raw,total,login);
}
std::vector<Record> SdkPort::open_orders(const UserInfo& u) {
  int total=-1;auto* raw=api_->OrdersGetOpen(&u,&total);return records(raw,total,u.login);
}
Payload SdkPort::payload(const Record& r) {return {r,fixed(account(r.login).user.grp.currency),parameters_.currency_digits};}
closedalloc::Result SdkPort::submit(const closedalloc::Job& job,const closedalloc::SaveProgress& save) {
  using closedalloc::Outcome;
  closedalloc::Result result{Outcome::not_written,job.progress,"preflight not completed"};
  Payload source;Account before;ConSymbol symbol{};Evidence evidence;
  // All preflight failures have positive proof of no side effect: OrdersAdd has not run.
  try {
    if(blocked_)throw std::runtime_error("another allocation is awaiting reconciliation; new writes are paused");
    source=decode(job.trade.payload);
    const auto invalid=source_valid(source,parameters_);if(!invalid.empty())throw std::runtime_error(invalid);
    if(!parameters_.direct_closed_add_verified)throw std::runtime_error("DirectClosedAddVerified is disabled");
    TradeRecord current{};found(api_->OrdersGet(source.record.ticket,&current),"source OrdersGet");
    if(encode(payload(copy(current)))!=job.trade.payload) {
      result.outcome=Outcome::review;result.message="source history changed after frozen selection";return result;
    }
    before=account(parameters_.target);
    if(!before.user.enable || !before.user.grp.enable || !before.user.enable_read_only)
      throw std::runtime_error("target must be enabled and read-only (dedicated settlement account)");
    if(fixed(before.user.grp.currency)!=source.currency)throw std::runtime_error("source/target deposit currency mismatch");
    if(!open_orders(before.user).empty() || before.margin!=0)throw std::runtime_error("target has open/pending orders or margin exposure");
    found(api_->SymbolsGet(source.record.symbol.c_str(),&symbol),"SymbolsGet");
    if(fixed(symbol.symbol)!=source.record.symbol || symbol.digits!=source.record.digits || symbol.type<0 || symbol.type>=MAX_SEC_GROUPS)
      throw std::runtime_error("source/target symbol precision mismatch");
    const auto& access=before.user.grp.secgroups[symbol.type];
    if(!access.show || !access.trade || source.record.volume<access.lot_min || source.record.volume>access.lot_max ||
       access.lot_step<=0 || (source.record.volume-access.lot_min)%access.lot_step)
      throw std::runtime_error("target symbol or volume is incompatible");
    if(money_round(before.user.balance+job.net,source.currency_digits)<0)
      throw std::runtime_error("allocation would produce a negative target balance");
    const auto existing=history(parameters_.target);
    if(existing.size()>=100000)
      throw std::runtime_error("target history capacity reached; adding a record would prevent complete reconciliation");
    for(const auto& record:existing)if(record.api[0]==api_namespace && record.api[3]==source.record.ticket) {
      result.outcome=Outcome::review;result.message="source ticket already allocated in target history (possibly a different journal/tag)";return result;
    }
    evidence.attempted_at=now();evidence.balance=before.user.balance;evidence.credit=before.user.credit;
    evidence.margin=before.margin;evidence.free_margin=before.free_margin;evidence.equity=before.equity;
    evidence.history_hash=digest(existing);
  } catch(const std::exception& error) {result.message=error.what();return result;}
  auto expected=native(wanted(source,parameters_.target,job.tag));
  result.progress.data=encode_evidence(evidence);
  save(result.progress); // Durable baseline and intent BEFORE the only external write.
  blocked_=true; // Also blocks later jobs in this pump if the SDK throws.
  const int ticket=api_->OrdersAdd(&expected,&before.user,&symbol);
  evidence.returned=true;evidence.sdk_result=ticket;result.progress.data=encode_evidence(evidence);
  if(ticket>0)result.progress.target_ids={static_cast<std::uint64_t>(ticket)};
  save(result.progress); // If this fails, recovery scans the persisted API tag.
  auto submitted=job;submitted.progress=result.progress;
  auto verified=reconcile(submitted);
  if(verified.outcome==Outcome::completed)blocked_=false;
  return verified;
}
closedalloc::Result SdkPort::reconcile(const closedalloc::Job& job) {
  using closedalloc::Outcome;
  closedalloc::Result result{Outcome::review,job.progress,"awaiting MT4 historical and balance verification"};
  try {
    // An empty checkpoint means a crash before the adapter's write intent. The
    // core recorded Writing, but no external operation was permitted yet.
    if(job.progress.data.empty() && job.progress.target_ids.empty()) {
      result.outcome=Outcome::not_written;result.message="no adapter write intent was persisted";return result;
    }
    const auto evidence=decode_evidence(job.progress.data);const auto source=decode(job.trade.payload);
    auto history_records=history(parameters_.target);std::vector<Record> matches,baseline;
    for(const auto& record:history_records) {
      if(tagged(record,job.tag,source.record.ticket))matches.push_back(record);else baseline.push_back(record);
    }
    if(matches.size()!=1) {
      result.message=matches.empty()?"target tag absent; attempted write outcome is unknown; no automatic resend":
        "multiple target records carry the same allocation tag";return result;
    }
    const auto& target=matches.front();
    if(!job.progress.target_ids.empty() && (job.progress.target_ids.size()!=1 || job.progress.target_ids.front()!=static_cast<std::uint64_t>(target.ticket)))
      {result.message="returned ticket and target tag disagree";return result;}
    result.progress.target_ids={static_cast<std::uint64_t>(target.ticket)};
    const auto different=mismatch(target,wanted(source,parameters_.target,job.tag));
    if(!different.empty()){result.message=different;return result;}
    if(digest(baseline)!=evidence.history_hash) {
      result.message="unrelated target history changed; net balance attribution cannot be proven";return result;
    }
    const auto after=account(parameters_.target);
    if(fixed(after.user.grp.currency)!=source.currency || !open_orders(after.user).empty() || after.margin!=0) {
      result.message="target currency/exposure differs after historical insertion";return result;
    }
    const auto digits=source.currency_digits;
    if(!amount(after.user.balance,evidence.balance+job.net,digits) || !amount(after.user.credit,evidence.credit,digits) ||
       !amount(after.equity,evidence.equity+job.net,digits) || !amount(after.free_margin,evidence.free_margin+job.net,digits)) {
      result.message="history exists but balance/equity/free-margin net change is unverified; no balance adjustment or resend";return result;
    }
    result.outcome=Outcome::completed;
    result.message="closed record, SL/TP, empty comment, API tag and net balance effect verified";
  }catch(const std::exception& error){result.message=std::string("MT4 reconciliation: ")+error.what();}
  return result;
}
} // namespace mt4alloc

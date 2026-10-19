#include <windows.h>
#include <bcrypt.h>
#include <MT5APIServer.h>
#include "model.hpp"
#include "events.hpp"
#include "source_work.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace {
using namespace closedalloc;
namespace m = closedalloc::mt5;

constexpr std::uint32_t kHistoryLimit=100000;
struct ApiError : std::runtime_error { using std::runtime_error::runtime_error; };
struct PreflightReview : ApiError { using ApiError::ApiError; };
struct PartialQuery : ApiError { using ApiError::ApiError; };
std::int64_t steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void require(bool ok,const char* message) { if(!ok)throw ApiError(message); }
void check(MTAPIRES r,const char* op) {
  if(r!=MT_RET_OK)throw ApiError(std::string(op)+" returned "+std::to_string(r));
}
template<class T> struct Release { void operator()(T* p)const { if(p)p->Release(); } };
template<class T> using Ptr=std::unique_ptr<T,Release<T>>;
template<class T> Ptr<T> own(T* p) { require(p!=nullptr,"SDK allocation failed");return Ptr<T>(p); }
std::string utf8(const wchar_t* text) {
  if(!text||!*text)return {};
  int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);
  require(n>0,"invalid UTF-16");std::string out(static_cast<std::size_t>(n),'\0');
  require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,out.data(),n,nullptr,nullptr)>0,"UTF-8 conversion failed");
  out.pop_back();return out;
}
std::wstring wide(const std::string& text) {
  if(text.empty())return {};
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
  require(n>0,"invalid UTF-8");std::wstring out(static_cast<std::size_t>(n),L'\0');
  require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n)>0,"UTF-16 conversion failed");return out;
}
template<class T> std::uint64_t api_data(const T* x,std::uint8_t slot) {
  std::uint64_t v=0;return x->ApiDataGet(m::kApiApp,slot,v)==MT_RET_OK?v:0;
}
template<class T> bool generated(const T* x) { return api_data(x,m::kSlotMagic)==m::kApiMagic; }
template<class T> void tag(T* x,const Job& job,const Config& c,std::uint64_t role,std::uint64_t ticket,std::int64_t now) {
  check(x->ApiDataClearAll(),"clear foreign API data");
  check(x->ApiDataSet(m::kApiApp,m::kSlotMagic,m::kApiMagic),"tag magic");check(x->ApiDataSet(m::kApiApp,m::kSlotTag,job.tag),"tag job");
  check(x->ApiDataSet(m::kApiApp,m::kSlotRole,role),"tag role");check(x->ApiDataSet(m::kApiApp,m::kSlotSource,c.source),"tag source");
  check(x->ApiDataSet(m::kApiApp,m::kSlotPosition,static_cast<std::uint64_t>(std::stoull(job.trade.id))),"tag source position");
  check(x->ApiDataSet(m::kApiApp,m::kSlotTicket,ticket),"tag source ticket");check(x->ApiDataSet(m::kApiApp,m::kSlotWriteTime,static_cast<std::uint64_t>(now)),"tag write time");
}
m::Order read(const IMTOrder* x) {
  m::Order o;o.ticket=x->Order();o.login=x->Login();o.position=x->PositionID();o.volume=x->VolumeInitialExt();
  o.remaining=x->VolumeCurrentExt();o.setup=x->TimeSetupMsc();o.done=x->TimeDoneMsc();o.expiration=x->TimeExpiration();
  o.type=x->Type();o.state=x->State();o.digits=x->Digits();o.currency_digits=x->DigitsCurrency();o.fill=x->TypeFill();
  o.time_type=x->TypeTime();o.symbol=utf8(x->Symbol());
  o.values={x->ContractSize(),x->PriceOrder(),x->PriceCurrent(),x->PriceSL(),x->PriceTP(),m::has_trigger_price(o.type)?x->PriceTrigger():0,x->RateMargin()};return o;
}
m::Deal read(const IMTDeal* x) {
  m::Deal d;d.ticket=x->Deal();d.login=x->Login();d.order=x->Order();d.position=x->PositionID();d.volume=x->VolumeExt();
  d.time=x->TimeMsc();d.action=x->Action();d.entry=x->Entry();d.digits=x->Digits();
  d.closed_volume=m::has_exit_fields(d.entry)?x->VolumeClosedExt():0;
  d.currency_digits=x->DigitsCurrency();d.symbol=utf8(x->Symbol());
  d.values={x->ContractSize(),x->Price(),x->Profit(),x->Commission(),x->Storage(),x->Fee(),x->PriceSL(),x->PriceTP(),
            m::has_exit_fields(d.entry)?x->ProfitRaw():0,m::has_exit_fields(d.entry)?x->PricePosition():0,x->RateProfit(),x->RateMargin(),x->TickValue(),x->TickSize()};return d;
}
void fill(IMTOrder* x,const m::Order& o,std::uint64_t login,std::uint64_t position) {
  check(x->Clear(),"Order.Clear");check(x->Login(login),"Order.Login");check(x->Symbol(wide(o.symbol).c_str()),"Order.Symbol");
  check(x->PositionID(position),"Order.PositionID");check(x->VolumeInitialExt(o.volume),"Order.VolumeInitialExt");
  check(x->VolumeCurrentExt(0),"Order.VolumeCurrentExt");check(x->TimeSetupMsc(o.setup),"Order.TimeSetupMsc");
  check(x->TimeDoneMsc(o.done),"Order.TimeDoneMsc");check(x->TimeExpiration(o.expiration),"Order.TimeExpiration");
  check(x->Type(o.type),"Order.Type");check(x->StateSet(IMTOrder::ORDER_STATE_FILLED),"Order.StateSet");
  check(x->ReasonSet(IMTOrder::ORDER_REASON_DEALER),"Order.ReasonSet");check(x->Digits(o.digits),"Order.Digits");
  check(x->DigitsCurrency(o.currency_digits),"Order.DigitsCurrency");check(x->TypeFill(o.fill),"Order.TypeFill");
  check(x->TypeTime(o.time_type),"Order.TypeTime");check(x->ContractSize(o.values[0]),"Order.ContractSize");
  check(x->PriceOrder(o.values[1]),"Order.PriceOrder");check(x->PriceCurrent(o.values[2]),"Order.PriceCurrent");
  check(x->PriceSL(o.values[3]),"Order.PriceSL");check(x->PriceTP(o.values[4]),"Order.PriceTP");
  if(m::has_trigger_price(o.type))check(x->PriceTrigger(o.values[5]),"Order.PriceTrigger");
  check(x->RateMargin(o.values[6]),"Order.RateMargin");
  check(x->Comment(L""),"Order.Comment");
}
void fill(IMTDeal* x,const m::Deal& d,std::uint64_t login,std::uint64_t order,std::uint64_t position) {
  check(x->Clear(),"Deal.Clear");check(x->Login(login),"Deal.Login");check(x->Symbol(wide(d.symbol).c_str()),"Deal.Symbol");
  check(x->Action(d.action),"Deal.Action");check(x->Entry(d.entry),"Deal.Entry");
  check(x->Order(order),"Deal.Order");check(x->PositionID(position),"Deal.PositionID");check(x->VolumeExt(d.volume),"Deal.VolumeExt");
  if(m::has_exit_fields(d.entry))check(x->VolumeClosedExt(d.closed_volume),"Deal.VolumeClosedExt");
  check(x->TimeMsc(d.time),"Deal.TimeMsc");check(x->Digits(d.digits),"Deal.Digits");
  check(x->DigitsCurrency(d.currency_digits),"Deal.DigitsCurrency");check(x->ContractSize(d.values[0]),"Deal.ContractSize");
  check(x->Price(d.values[1]),"Deal.Price");check(x->Profit(d.values[2]),"Deal.Profit");
  check(x->Commission(d.values[3]),"Deal.Commission");check(x->Storage(d.values[4]),"Deal.Storage");check(x->Fee(d.values[5]),"Deal.Fee");
  check(x->PriceSL(d.values[6]),"Deal.PriceSL");check(x->PriceTP(d.values[7]),"Deal.PriceTP");
  if(m::has_exit_fields(d.entry)) {
    check(x->ProfitRaw(d.values[8]),"Deal.ProfitRaw");check(x->PricePosition(d.values[9]),"Deal.PricePosition");
  }
  check(x->RateProfit(d.values[10]),"Deal.RateProfit");check(x->RateMargin(d.values[11]),"Deal.RateMargin");
  check(x->TickValue(d.values[12]),"Deal.TickValue");check(x->TickSize(d.values[13]),"Deal.TickSize");
  check(x->ReasonSet(IMTDeal::DEAL_REASON_DEALER),"Deal.ReasonSet");check(x->Comment(L""),"Deal.Comment");
}
std::string digest(const std::map<std::uint64_t,std::string>& records) {
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider failed");
  struct Cleanup { BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);} } cleanup{algorithm,hash};
  require(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"SHA256 create failed");
  for(const auto& pair:records) {
    Encoder e;e.u64(pair.first);e.text(pair.second);const auto& bytes=e.data();
    require(bytes.size()<=(std::numeric_limits<ULONG>::max)(),"history record is too large");
    require(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0)>=0,"SHA256 update failed");
  }
  unsigned char out[32]{};require(BCryptFinishHash(hash,out,sizeof out,0)>=0,"SHA256 finish failed");
  std::ostringstream s;for(auto b:out)s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return s.str();
}
struct Evidence {
  std::uint64_t phase=0;
  double balance=0,equity=0,free_margin=0,credit=0;
  std::string deals,orders;
  std::string encode() const { Encoder e;e.text("mt5-progress-v1");e.u64(phase);e.real(balance);e.real(equity);e.real(free_margin);e.real(credit);e.text(deals);e.text(orders);return e.data(); }
  static Evidence decode(const std::string& bytes) { Decoder d(bytes);require(d.text()=="mt5-progress-v1","invalid progress version");Evidence e;e.phase=d.u64();e.balance=d.real();e.equity=d.real();e.free_margin=d.real();e.credit=d.real();e.deals=d.text();e.orders=d.text();d.finish();return e; }
};
struct Target {
  std::map<std::uint64_t,m::Order> orders;
  std::map<std::uint64_t,m::Deal> deals;
  std::string other_orders,other_deals;
  std::map<std::uint64_t,std::array<std::uint64_t,3>> origins;
  std::uint32_t order_count=0,deal_count=0;
};
class Adapter final : public Port {
public:
  Adapter(IMTServerAPI* api,Config config,const std::atomic<bool>& resync,const std::atomic<bool>& stopping)
    :api_(api),config_(std::move(config)),resync_(resync),stopping_(stopping){}
  void configure(Config c) {config_=std::move(c);}
  Ptr<IMTConGroup> group(std::uint64_t login) {
    auto u=own(api_->UserCreate());check(api_->UserGet(login,u.get()),"UserGet");auto g=own(api_->GroupCreate());
    check(api_->GroupGet(u->Group(),g.get()),"GroupGet");return g;
  }
  std::set<std::uint64_t> positions(std::uint64_t login) {
    auto p=own(api_->PositionCreateArray());check(api_->PositionGet(login,p.get()),"PositionGet");std::set<std::uint64_t> ids;
    for(std::uint32_t i=0;i<p->Total();++i){auto x=p->Next(i);require(x!=nullptr,"null position");ids.insert(x->Position());}return ids;
  }
  void validate_target(const m::Snapshot& s) {
    auto user=own(api_->UserCreate());check(api_->UserGet(config_.target,user.get()),"target UserGet");
    require((user->Rights()&(IMTUser::USER_RIGHT_READONLY|IMTUser::USER_RIGHT_TRADE_DISABLED))!=0,
            "target must have readonly or trading-disabled rights");
    auto g=group(config_.target);require(utf8(g->Currency())==s.currency&&g->CurrencyDigits()==s.closing.currency_digits,"target currency mismatch");
    require(g->MarginMode()==IMTConGroup::MARGIN_MODE_RETAIL_HEDGED,"only hedging target groups are supported");
    require(positions(config_.target).empty(),"target must be a dedicated account with no open positions");
    auto live=own(api_->OrderCreateArray());check(api_->OrderGet(config_.target,live.get()),"OrderGet live");
    require(live->Total()==0,"target must have no pending orders");
    auto symbol=own(api_->SymbolCreate());check(api_->SymbolGet(wide(s.closing.symbol).c_str(),g.get(),symbol.get()),"SymbolGet for target group");
    require(symbol->Digits()==s.closing.digits&&symbol->ContractSize()==s.closing.values[0],"target symbol contract differs from frozen source");
  }
  std::vector<std::uint64_t> select_source(std::int64_t from,std::int64_t to,
                                         std::uint64_t position=0) {
    auto request=own(api_->DatasetRequestCreate());auto field=own(request->FieldCreate());
    auto dataset=own(api_->DatasetCreate());
    check(request->RowLimit(kHistoryLimit+1),"source dataset RowLimit");
    check(field->Id(position?IMTDatasetField::FIELD_DEAL_DEAL:IMTDatasetField::FIELD_DEAL_POSITION_ID),"source dataset field");
    check(field->Offset(0),"source dataset offset");
    check(field->Flags(IMTDatasetField::FLAG_SELECT),"source dataset select");check(request->FieldAdd(field.get()),"source dataset FieldAdd");
    if(position) {
      check(field->Clear(),"source filter Clear");check(field->Id(IMTDatasetField::FIELD_DEAL_POSITION_ID),"source position filter");
      check(field->Flags(0),"source filter flags");check(field->WhereAddUInt(position),"source position condition");
      check(request->FieldAdd(field.get()),"source filter FieldAdd");
    }
    ++source_queries_;
    const auto result=api_->DealSelectByLogins(&config_.source,1,from,to,request.get(),dataset.get());
    if(result==MT_RET_ERR_PARTIAL||dataset->RowTotal()>kHistoryLimit)throw PartialQuery("source dataset exceeds bounded query capacity");
    if(result==MT_RET_OK_NONE) {require(dataset->RowTotal()==0,"source dataset returned contradictory no-data result");return {};}
    check(result,"source DealSelectByLogins");
    std::vector<std::uint64_t> ids;ids.reserve(dataset->RowTotal());
    for(std::uint32_t i=0;i<dataset->RowTotal();++i) {
      std::uint64_t id=0;check(dataset->RowRead(i,&id,sizeof(id)),"source dataset RowRead");if(id)ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());
    if(position)require(std::adjacent_find(ids.begin(),ids.end())==ids.end(),"duplicate source deal ticket in dataset");
    else ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    return ids;
  }
  void discover(std::int64_t from,std::int64_t to,std::int64_t tick) {
    // Only IDs from the catch-up time window cross the API boundary. Full
    // position history is obtained separately, including an old opening deal.
    const auto ids=select_source(from,to);
    for(auto id:ids)work_.touch(id,tick);
    ++discovery_queries_;
  }
  void touch(std::uint64_t position,std::int64_t tick,bool closing=false) {
    work_.touch(position,tick);if(closing)live_retries_.emplace(position,0);
  }
  void audit(std::uint64_t position,std::int64_t tick) {touch(position,tick);audit_.insert(position);}
  bool audit_pending() const {return !audit_.empty();}
  void forget(std::uint64_t position) {live_retries_.erase(position);audit_.erase(position);work_.erase(position);}
  void invalidate(Engine& engine,std::uint64_t position,const char* why) {
    engine.invalidate_source(std::to_string(position),why);++observations_;forget(position);
  }
  std::size_t pending() const {return work_.size();}
  bool ready(std::int64_t tick) const {return work_.ready(tick);}
  std::uint64_t observations() const {return observations_;}
  std::uint64_t source_queries() const {return source_queries_;}
  std::uint64_t discovery_queries() const {return discovery_queries_;}
  // One worker tick is bounded by both a count and a soft time budget. An SDK
  // call already in progress cannot be cancelled; the caller adds a cooldown.
  void scan_ready(Engine& engine,unsigned settle_seconds,unsigned retry_seconds,
                  std::int64_t tick,std::int64_t deadline,const std::atomic<bool>& stopping) {
    for(unsigned count=0;count<8&&!stopping.load()&&steady_ms()<deadline;++count) {
      const auto id=work_.take(tick);if(!id)break;
      try {scan_position(engine,id,settle_seconds);}
      catch(...) {work_.defer(id,steady_ms()+static_cast<std::int64_t>(retry_seconds)*1000);throw;}
    }
  }
  void scan_position(Engine& engine,std::uint64_t position,unsigned settle_seconds) {
    const auto known=engine.find_trade(std::to_string(position));
    if(known&&known->source_changed) {forget(position);return;}
    auto live=own(api_->PositionCreate());const auto live_result=api_->PositionGetByTicket(position,live.get());
    if(live_result==MT_RET_OK) {
      if(known) {invalidate(engine,position,"previously closed source position is open again");return;}
      // Deal/history notifications may precede position removal. Retry only
      // close hints, and only three times; partial closes never poll forever.
      const auto retry=live_retries_.find(position);
      if(retry!=live_retries_.end()&&retry->second++<3) {work_.defer(position,steady_ms()+5000);return;}
      forget(position);return;
    }
    require(live_result==MT_RET_ERR_NOTFOUND,"source PositionGetByTicket failed; absence is not proven");
    const auto now=api_->TimeCurrent();const auto tickets=select_source(0,now,position);
    if(tickets.empty()) {if(known)invalidate(engine,position,"source position deal history was deleted");else forget(position);return;}
    auto all=own(api_->DealCreateArray());check(api_->DealGetByTickets(tickets.data(),static_cast<std::uint32_t>(tickets.size()),all.get()),"source DealGetByTickets");
    require(all->Total()==tickets.size(),"source position deal set changed during read");
    std::vector<const IMTDeal*> rows;std::set<std::uint64_t> actual;
    for(std::uint32_t i=0;i<all->Total();++i) {
      auto x=all->Next(i);require(x&&x->Login()==config_.source&&x->PositionID()==position,"source position identity changed during read");
      require(actual.insert(x->Deal()).second,"duplicate source deal object");rows.push_back(x);
    }
    require(std::equal(actual.begin(),actual.end(),tickets.begin(),tickets.end()),"source deal tickets changed during read");
    std::sort(rows.begin(),rows.end(),[](const IMTDeal* a,const IMTDeal* b){return a->TimeMsc()==b->TimeMsc()?a->Deal()<b->Deal():a->TimeMsc()<b->TimeMsc();});
    const IMTDeal* last=nullptr;bool is_generated=false;
    for(auto d:rows) {if(d->Action()<=IMTDeal::DEAL_SELL&&d->Entry()!=IMTDeal::ENTRY_IN)last=d;is_generated=is_generated||generated(d);}
    if(!last||last->Time()<=engine.scan_from()||is_generated) {
      if(known)invalidate(engine,position,"source closing deal disappeared or its time/origin changed");else forget(position);return;
    }
    auto g=group(config_.source);
    std::string unsupported;
    if(g->MarginMode()!=IMTConGroup::MARGIN_MODE_RETAIL_HEDGED)unsupported="netting source groups are unsupported";
    auto commission=own(api_->GroupCommissionCreate());
    for(std::uint32_t i=0;i<g->CommissionTotal();++i) {
      check(g->CommissionNext(i,commission.get()),"CommissionNext");
      if(commission->ChargeMode()!=IMTConCommission::COMM_CHARGE_INSTANT)
        unsupported="deferred daily/monthly source commission cannot be attributed to a closed pair";
    }
      Trade t;t.id=std::to_string(position);t.closed_at=last->Time();t.currency_digits=g->CurrencyDigits();
      Encoder raw;raw.text("mt5-excluded-v1");for(auto d:rows)m::encode(raw,read(d));t.payload=raw.data();
      if(rows.size()!=2)t.exclusion="partial closes, scale-ins, split fills or separately linked charges are unsupported";
      else if(!unsupported.empty())t.exclusion=unsupported;
      else {
        auto a=own(api_->OrderCreate());auto b=own(api_->OrderCreate());
        // Missing history may be a transient settlement lag: retry source reads next poll.
        const auto opening_result=api_->HistoryGet(rows[0]->Order(),a.get());
        const auto closing_result=api_->HistoryGet(rows[1]->Order(),b.get());
        if(known&&(opening_result==MT_RET_ERR_NOTFOUND||closing_result==MT_RET_ERR_NOTFOUND)) {
          invalidate(engine,position,"source historical order was deleted");return;
        }
        check(opening_result,"source opening HistoryGet");check(closing_result,"source closing HistoryGet");
        m::Snapshot snapshot{read(a.get()),read(b.get()),read(rows[0]),read(rows[1]),utf8(g->Currency())};
        t=m::make_trade(snapshot);
      }
      if(work_.stable(position,t.payload,steady_ms(),(now-t.closed_at)*1000,static_cast<std::int64_t>(settle_seconds)*1000)) {
        engine.observe(t,now);++observations_;forget(position);
      }
  }
  Result submit(const Job& job,const SaveProgress& save) override {
    Progress progress=job.progress;Evidence evidence;
    try {
      auto snapshot=m::decode(job.trade.payload);require(m::validate_pair(snapshot).empty(),"invalid frozen source pair");
      require(snapshot.opening.login==config_.source&&job.trade.id==std::to_string(snapshot.opening.position),"frozen source route mismatch");
      require(progress.data.empty()&&progress.target_ids.empty(),"submission contains prior evidence");
      verify_source(snapshot);
      validate_target(snapshot);
      ensure_unique_origin(job,snapshot.opening.position);
      auto target=read_target(job.tag);
      if(!target.orders.empty()||!target.deals.empty())throw PreflightReview("API marker already exists before submission");
      require(m::has_pair_capacity(target.order_count,target.deal_count,kHistoryLimit),
              "target history has insufficient capacity for two new orders and two new deals");
      double historical=0,credit_history=0;
      check(api_->TradeBalanceCheck(config_.target,0,evidence.balance,historical,evidence.credit,credit_history),"preflight TradeBalanceCheck");
      require(equal(evidence.balance,historical,snapshot)&&equal(evidence.credit,credit_history,snapshot),"target balance/credit already disagrees with history");
      auto account=own(api_->UserCreateAccount());check(api_->UserAccountGet(config_.target,account.get()),"preflight UserAccountGet");
      require(equal(account->Balance(),evidence.balance,snapshot),"target account changed during preflight");
      evidence.equity=account->Equity();evidence.free_margin=account->MarginFree();
      require(evidence.balance+job.net>=0&&evidence.free_margin+job.net>=0,"allocation would exceed target balance/free margin");
      evidence.orders=target.other_orders;evidence.deals=target.other_deals;
      auto checkpoint=[&](std::uint64_t phase){evidence.phase=phase;progress.data=evidence.encode();save(progress);};
      checkpoint(0);
      auto order=own(api_->OrderCreate());auto deal=own(api_->DealCreate());const auto written_at=api_->TimeCurrent();
      fill(order.get(),snapshot.opening_order,config_.target,0);tag(order.get(),job,config_,1,snapshot.opening_order.ticket,written_at);
      require(!resync_.load()&&!stopping_.load(),"source resync or stop requested before first write; submission deferred");
      checkpoint(1);check(api_->HistoryAdd(order.get()),"HistoryAdd opening");
      {const auto t=read_target(job.tag);require(t.orders.count(1)!=0,"cannot resolve opening order marker");progress.target_ids.push_back(t.orders.at(1).ticket);}checkpoint(1);
      // A MT5 position ID is its opening order ticket. It is only known after allocation.
      check(api_->HistoryGet(progress.target_ids[0],order.get()),"read new opening order");
      check(order->PositionID(progress.target_ids[0]),"set new position identity");
      checkpoint(2);check(api_->HistoryUpdate(order.get()),"link opening order to synthetic closed position");checkpoint(2);
      fill(order.get(),snapshot.closing_order,config_.target,progress.target_ids[0]);tag(order.get(),job,config_,2,snapshot.closing_order.ticket,written_at);
      checkpoint(3);check(api_->HistoryAdd(order.get()),"HistoryAdd closing");
      {const auto t=read_target(job.tag);require(t.orders.count(2)!=0,"cannot resolve closing order marker");progress.target_ids.push_back(t.orders.at(2).ticket);}checkpoint(3);
      fill(deal.get(),snapshot.opening,config_.target,progress.target_ids[0],progress.target_ids[0]);tag(deal.get(),job,config_,3,snapshot.opening.ticket,written_at);
      checkpoint(4);check(api_->DealAdd(deal.get()),"DealAdd opening");
      {const auto t=read_target(job.tag);require(t.deals.count(3)!=0,"cannot resolve opening deal marker");progress.target_ids.push_back(t.deals.at(3).ticket);}checkpoint(4);
      fill(deal.get(),snapshot.closing,config_.target,progress.target_ids[1],progress.target_ids[0]);tag(deal.get(),job,config_,4,snapshot.closing.ticket,written_at);
      checkpoint(5);check(api_->DealAdd(deal.get()),"DealAdd closing");
      {const auto t=read_target(job.tag);require(t.deals.count(4)!=0,"cannot resolve closing deal marker");progress.target_ids.push_back(t.deals.at(4).ticket);}checkpoint(5);
      // DealAdd intentionally leaves balances/positions unchanged. Never append
      // a balance deal: that would count the same P&L twice in financial history.
      verify_records(job,snapshot,evidence,progress);
      double user=0,history=0,credit=0,historical_credit=0;
      check(api_->TradeBalanceCheck(config_.target,0,user,history,credit,historical_credit),"before balance application");
      require(equal(user,evidence.balance,snapshot)&&equal(history,evidence.balance+job.net,snapshot)&&
              equal(credit,evidence.credit,snapshot)&&equal(historical_credit,evidence.credit,snapshot),"unexpected account or history change before balance application");
      // SDK operation scans/corrects the entire account; dedicated target only.
      checkpoint(6);check(api_->TradeBalanceCheck(config_.target,1,user,history,credit,historical_credit),"apply historical net balance once");checkpoint(7);
      Job updated=job;updated.progress=progress;return reconcile(updated);
    } catch(const PreflightReview& e) {return {Outcome::review,progress,e.what()};
    } catch(const ApiError& e) {
      return {evidence.phase==0?Outcome::not_written:Outcome::review,evidence.phase==0?Progress{}:progress,e.what()};
    }
  }
  Result reconcile(const Job& job) override {
    try {
      if(job.progress.data.empty()) {
        ensure_unique_origin(job,m::decode(job.trade.payload).opening.position);
        auto t=read_target(job.tag);
        require(t.orders.empty()&&t.deals.empty(),"found history without a persisted checkpoint");
        return {Outcome::not_written,job.progress,"no external-call checkpoint or tagged record exists"};
      }
      const auto e=Evidence::decode(job.progress.data);auto s=m::decode(job.trade.payload);
      if(e.phase==0) {
        auto t=read_target(job.tag);require(t.orders.empty()&&t.deals.empty(),"unexpected records before first write checkpoint");
        return {Outcome::not_written,{},"preflight-only checkpoint; no write attempted"};
      }
      auto progress=job.progress;verify_records(job,s,e,progress);
      double balance=0,history=0,credit=0,credit_history=0;
      check(api_->TradeBalanceCheck(config_.target,0,balance,history,credit,credit_history),"read-only balance reconciliation");
      require(equal(balance,e.balance+job.net,s)&&equal(history,balance,s)&&equal(credit,e.credit,s)&&equal(credit_history,credit,s),
              "history exists but the expected net balance is not proven; automatic repair is forbidden");
      auto account=own(api_->UserCreateAccount());check(api_->UserAccountGet(config_.target,account.get()),"reconcile UserAccountGet");
      require(equal(account->Balance(),balance,s)&&equal(account->Equity(),e.equity+job.net,s)&&equal(account->MarginFree(),e.free_margin+job.net,s),
              "account equity/free margin differs from the expected settled net change");
      return {Outcome::completed,progress,"two historical orders and two deals verified; SL/TP preserved; comments empty; net balance applied once; no open positions"};
    } catch(const ApiError& e) {return {Outcome::review,job.progress,e.what()};}
  }
private:
  void verify_source(const m::Snapshot& s) {
    auto a=own(api_->OrderCreate());auto b=own(api_->OrderCreate());auto c=own(api_->DealCreate());auto d=own(api_->DealCreate());
    check(api_->HistoryGet(s.opening_order.ticket,a.get()),"recheck source opening order");check(api_->HistoryGet(s.closing_order.ticket,b.get()),"recheck source closing order");
    check(api_->DealGet(s.opening.ticket,c.get()),"recheck source opening deal");check(api_->DealGet(s.closing.ticket,d.get()),"recheck source closing deal");
    const auto g=group(config_.source);
    if(g->MarginMode()!=IMTConGroup::MARGIN_MODE_RETAIL_HEDGED)throw PreflightReview("source account no longer uses hedging accounting");
    auto commission=own(api_->GroupCommissionCreate());
    for(std::uint32_t i=0;i<g->CommissionTotal();++i) {
      check(g->CommissionNext(i,commission.get()),"recheck source CommissionNext");
      if(commission->ChargeMode()!=IMTConCommission::COMM_CHARGE_INSTANT)
        throw PreflightReview("source commission is no longer settled instantly");
    }
    const auto tickets=select_source(0,api_->TimeCurrent(),s.opening.position);
    if(!m::same_position_deal_set(s,tickets)||generated(c.get())||generated(d.get()))
      throw PreflightReview("complete source lifecycle changed: missing/additional deal or allocated-origin record");
    const m::Snapshot current{read(a.get()),read(b.get()),read(c.get()),read(d.get()),utf8(g->Currency())};
    if(m::encode(current)!=m::encode(s)||positions(config_.source).count(s.opening.position))
      throw PreflightReview("source history changed after selection or source position is still open");
  }
  void ensure_unique_origin(const Job& job,std::uint64_t position) {
    auto orders=own(api_->OrderCreateArray());auto deals=own(api_->DealCreateArray());const auto now=api_->TimeCurrent();
    check(api_->HistoryGet(0,now,config_.target,orders.get()),"origin HistoryGet");check(api_->DealGet(0,now,config_.target,deals.get()),"origin DealGet");
    require(orders->Total()<=kHistoryLimit&&deals->Total()<=kHistoryLimit,"target history exceeds the 100000-record first-version limit");
    auto inspect=[&](const auto* x) {
      require(x!=nullptr,"null target history during origin check");
      if(generated(x)&&api_data(x,m::kSlotSource)==config_.source&&api_data(x,m::kSlotPosition)==position&&api_data(x,m::kSlotTag)!=job.tag)
        throw PreflightReview("this source position already has an allocation under a different journal tag; restore its original journal");
    };
    for(std::uint32_t i=0;i<orders->Total();++i)inspect(orders->Next(i));
    for(std::uint32_t i=0;i<deals->Total();++i)inspect(deals->Next(i));
  }
  bool equal(double a,double b,const m::Snapshot& s) const {
    return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<0.5*std::pow(10.0,-static_cast<int>(s.closing.currency_digits));
  }
  Target read_target(std::uint64_t job_tag) {
    Target t;auto orders=own(api_->OrderCreateArray());auto deals=own(api_->DealCreateArray());
    const auto now=api_->TimeCurrent();check(api_->HistoryGet(0,now,config_.target,orders.get()),"target HistoryGet");
    check(api_->DealGet(0,now,config_.target,deals.get()),"target DealGet");
    require(orders->Total()<=kHistoryLimit&&deals->Total()<=kHistoryLimit,"target history exceeds the 100000-record first-version limit");
    t.order_count=orders->Total();t.deal_count=deals->Total();
    std::map<std::uint64_t,std::string> ordinary_orders,ordinary_deals;
    for(std::uint32_t i=0;i<orders->Total();++i) {
      auto x=orders->Next(i);require(x!=nullptr,"null target order");auto row=read(x);
      if(generated(x)&&api_data(x,m::kSlotTag)==job_tag) {
        require(x->Comment()[0]==0,"target order comment must be empty");
        require(api_data(x,m::kSlotSource)==config_.source,"target order source identity mismatch");
        require(t.orders.emplace(api_data(x,m::kSlotRole),row).second,"duplicate tagged target order role");
        require(t.origins.emplace(api_data(x,m::kSlotRole),std::array<std::uint64_t,3>{api_data(x,m::kSlotPosition),api_data(x,m::kSlotTicket),api_data(x,m::kSlotWriteTime)}).second,"duplicate target API role");
      } else {Encoder e;m::encode(e,row);e.text(utf8(x->Comment()));ordinary_orders.emplace(row.ticket,e.data());}
    }
    for(std::uint32_t i=0;i<deals->Total();++i) {
      auto x=deals->Next(i);require(x!=nullptr,"null target deal");auto row=read(x);
      if(generated(x)&&api_data(x,m::kSlotTag)==job_tag) {
        require(x->Comment()[0]==0,"target deal comment must be empty");
        require(api_data(x,m::kSlotSource)==config_.source,"target deal source identity mismatch");
        require(t.deals.emplace(api_data(x,m::kSlotRole),row).second,"duplicate tagged target deal role");
        require(t.origins.emplace(api_data(x,m::kSlotRole),std::array<std::uint64_t,3>{api_data(x,m::kSlotPosition),api_data(x,m::kSlotTicket),api_data(x,m::kSlotWriteTime)}).second,"duplicate target API role");
      } else {Encoder e;m::encode(e,row);e.text(utf8(x->Comment()));ordinary_deals.emplace(row.ticket,e.data());}
    }
    t.other_orders=digest(ordinary_orders);t.other_deals=digest(ordinary_deals);return t;
  }
  void verify_records(const Job& job,const m::Snapshot& s,const Evidence& e,Progress& progress) {
    validate_target(s);const auto t=read_target(job.tag);
    require(t.other_orders==e.orders&&t.other_deals==e.deals,"unrelated target history changed; dedicated account requirement violated");
    require(t.orders.size()==2&&t.orders.count(1)&&t.orders.count(2)&&t.deals.size()==2&&t.deals.count(3)&&t.deals.count(4),"partial/missing target history; no automatic resend");
    const std::array<std::uint64_t,4> source_ids{s.opening_order.ticket,s.closing_order.ticket,s.opening.ticket,s.closing.ticket};
    for(std::uint64_t role=1;role<=4;++role) {
      const auto& origin=t.origins.at(role);
      require(origin[0]==s.opening.position&&origin[1]==source_ids[role-1]&&origin[2]>0,"target source provenance differs from frozen origin");
    }
    auto a=t.orders.at(1);auto b=t.orders.at(2);auto c=t.deals.at(3);auto d=t.deals.at(4);
    require(a.ticket!=b.ticket&&c.ticket!=d.ticket&&a.login==config_.target&&b.login==config_.target&&c.login==config_.target&&d.login==config_.target,"invalid target tickets or account");
    require(a.position==a.ticket&&b.position==a.ticket&&c.position==a.ticket&&d.position==a.ticket&&c.order==a.ticket&&d.order==b.ticket,"target order/deal/position links differ");
    progress.target_ids={a.ticket,b.ticket,c.ticket,d.ticket};
    // Only new server-generated identifiers and account identity may differ.
    a.ticket=s.opening_order.ticket;a.login=s.opening_order.login;a.position=s.opening_order.position;
    b.ticket=s.closing_order.ticket;b.login=s.closing_order.login;b.position=s.closing_order.position;
    c.ticket=s.opening.ticket;c.login=s.opening.login;c.order=s.opening.order;c.position=s.opening.position;
    d.ticket=s.closing.ticket;d.login=s.closing.login;d.order=s.closing.order;d.position=s.closing.position;
    m::Snapshot actual{a,b,c,d,s.currency};require(m::encode(actual)==m::encode(s),"target historical fields differ from frozen source, including SL/TP and costs");
  }
  IMTServerAPI* api_;Config config_;m::SourceWork work_;
  const std::atomic<bool>& resync_;const std::atomic<bool>& stopping_;
  std::map<std::uint64_t,unsigned> live_retries_;
  std::set<std::uint64_t> audit_;
  std::uint64_t source_queries_=0,discovery_queries_=0,observations_=0;
};

struct Settings {Config config;std::string journal;unsigned poll=30,settle=5,rescan=300;};
Settings settings(IMTServerAPI* api) {
  auto plugin=own(api->PluginCreate());check(api->PluginCurrent(plugin.get()),"PluginCurrent");auto p=own(api->PluginParamCreate());
  auto value=[&](const wchar_t* name,const char* fallback=nullptr){const auto result=plugin->ParameterGet(name,p.get());if(result==MT_RET_ERR_NOTFOUND&&fallback)return std::string(fallback);check(result,"ParameterGet");return utf8(p->Value());};
  auto integer=[&](const wchar_t* name,const char* fallback=nullptr){const auto text=value(name,fallback);require(!text.empty()&&text.find_first_not_of("0123456789")==std::string::npos,"expected unsigned integer parameter");return std::stoull(text);};
  auto probability=[&](const wchar_t* name){const auto text=value(name);std::size_t used=0;double result=std::stod(text,&used);require(used==text.size()&&std::isfinite(result)&&result>=0&&result<=100,"probability must be 0..100 percent");return result/100.0;};
  Settings s;MTServerInfo info{};check(api->About(info),"About");s.config.platform="mt5";s.config.server=std::to_string(info.server_id);
  s.config.source=integer(L"SourceLogin");s.config.target=integer(L"TargetLogin");s.config.loss_probability=probability(L"LossPercent");s.config.profit_probability=probability(L"ProfitPercent");
  const auto enabled=integer(L"Enabled"),armed=integer(L"Armed");require(enabled<=1&&armed<=1,"Enabled and Armed must be 0 or 1");
  s.config.enabled=enabled!=0;s.config.armed=armed!=0;s.config.enabled_since=0;
  const auto poll=integer(L"PollSeconds"),settle=integer(L"SettleSeconds"),rescan=integer(L"RescanSeconds","300");
  require(poll>=1&&poll<=3600&&settle>=1&&settle<=86400&&rescan>=60&&rescan<=86400,"invalid retry/settlement/rescan interval");
  s.poll=static_cast<unsigned>(poll);s.settle=static_cast<unsigned>(settle);s.journal=value(L"JournalPath");
  s.rescan=static_cast<unsigned>(rescan);
  require(!s.journal.empty(),"JournalPath is required");
  const auto journal=std::filesystem::u8path(s.journal);const auto root=journal.root_name().native();
  require(journal.is_absolute()&&root.size()==2&&root[1]==L':'&&GetDriveTypeW(journal.root_path().c_str())==DRIVE_FIXED,
          "JournalPath must be an absolute path on a fixed local drive; relative, UNC and network paths are forbidden");
  validate(s.config);return s;
}
class Plugin final : public IMTServerPlugin,public IMTDealSink,public IMTHistorySink {
public:
  ~Plugin(){Stop();}
  void Release() override {delete this;}
  MTAPIRES Start(IMTServerAPI* api) override {
    std::lock_guard<std::mutex> lock(lifecycle_);
    if(!api||api_)return MT_RET_ERR_PARAMS;api_=api;
    bool deals_subscribed=false,history_subscribed=false;
    try {
      settings_=settings(api_);const auto path=std::filesystem::u8path(settings_.journal);
      // Name lifetime, rather than thread ownership, excludes another instance
      // even if it points at a different journal or a different source for B.
      const auto name=wide("Global\\ClosedTradeAllocatorMT5-"+settings_.config.server+"-target-"+std::to_string(settings_.config.target));
      target_lock_=CreateMutexW(nullptr,FALSE,name.c_str());
      require(target_lock_!=nullptr,"cannot create target account instance mutex");
      require(GetLastError()!=ERROR_ALREADY_EXISTS,"another allocator instance already owns the target account");
      if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
      engine_=std::make_unique<Engine>(settings_.journal,settings_.config,api_->TimeCurrent());
      adapter_=std::make_unique<Adapter>(api_,engine_->config(),resync_,stopping_);
      wake_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(wake_event_!=nullptr,"cannot create worker wake event");
      m::SourceEvent stale;while(events_.pop(stale)){}
      watched_source_.store(settings_.config.source);resync_.store(false);wake_pending_.store(false);overflows_.store(0);
      last_stats_.clear();last_report_.clear();stopping_=false;callbacks_.open();
      // Subscribe before worker catch-up so events during initialization are retained.
      deals_subscribed=true;check(api_->DealSubscribe(this),"DealSubscribe");
      history_subscribed=true;check(api_->HistorySubscribe(this),"HistorySubscribe");
      worker_=std::thread([this]{run();});
      log(MTLogOK,"ClosedTradeAllocator MT5 started in event mode; one background operation per second; writes spaced by at least 5 seconds");return MT_RET_OK;
    } catch(const std::exception& e) {
      log(MTLogErr,e.what());callbacks_.close();stopping_=true;
      // Start rollback only. Normal Stop is called after the SDK unsubscribes.
      if(history_subscribed)api_->HistoryUnsubscribe(this);
      if(deals_subscribed)api_->DealUnsubscribe(this);
      close();return MT_RET_ERR_PARAMS;
    }
  }
  MTAPIRES Stop() override {
    std::lock_guard<std::mutex> lock(lifecycle_);close();return MT_RET_OK;
  }
  void OnDealAdd(const IMTDeal* deal) override {deal_event(deal,true);}
  void OnDealUpdate(const IMTDeal* deal) override {deal_event(deal);}
  void OnDealDelete(const IMTDeal* deal) override {deal_event(deal);}
  void OnDealClean(std::uint64_t login) override {sync_event(login);}
  void OnDealSync() override {sync_event(0);}
  void OnHistoryAdd(const IMTOrder* order) override {history_event(order,true);}
  void OnHistoryUpdate(const IMTOrder* order) override {history_event(order);}
  void OnHistoryDelete(const IMTOrder* order) override {history_event(order);}
  void OnHistoryClean(std::uint64_t login) override {sync_event(login);}
  void OnHistorySync() override {sync_event(0);}
private:
  void signal() noexcept {
    if(!wake_pending_.exchange(true,std::memory_order_acq_rel))SetEvent(wake_event_);
  }
  void enqueue(m::SourceEvent event) noexcept {
    if(!event.position)return;
    if(!events_.push(event)) {resync_.store(true);overflows_.fetch_add(1);}
    signal();
  }
  void deal_event(const IMTDeal* deal,bool added=false) noexcept {
    if(!callbacks_.enter())return;
    try {
      if(deal&&deal->Login()==watched_source_.load()) {
        const bool closing=deal->Action()<=IMTDeal::DEAL_SELL&&deal->Entry()!=IMTDeal::ENTRY_IN;
        if(!added||closing||deal->Action()>IMTDeal::DEAL_SELL)
          enqueue({m::EventKind::deal,deal->Deal(),deal->PositionID(),closing});
      }
    }
    catch(...) {resync_.store(true);signal();}
    callbacks_.leave();
  }
  void history_event(const IMTOrder* order,bool added=false) noexcept {
    if(!callbacks_.enter())return;
    try {
      if(order&&order->Login()==watched_source_.load()) {
        const bool closing=order->Order()!=order->PositionID();
        if(!added||closing)enqueue({m::EventKind::history,order->Order(),order->PositionID(),closing});
      }
    }
    catch(...) {resync_.store(true);signal();}
    callbacks_.leave();
  }
  void sync_event(std::uint64_t login) noexcept {
    if(!callbacks_.enter())return;
    if(!login||login==watched_source_.load()) {resync_.store(true);signal();}
    callbacks_.leave();
  }
  void close() {
    callbacks_.close();stopping_=true;if(wake_event_)SetEvent(wake_event_);
    if(worker_.joinable())worker_.join();
    while(!callbacks_.idle())Sleep(1);
    adapter_.reset();engine_.reset();
    if(wake_event_){CloseHandle(wake_event_);wake_event_=nullptr;}
    if(target_lock_){CloseHandle(target_lock_);target_lock_=nullptr;}
    api_=nullptr;
  }
  void log(std::uint32_t level,const std::string& s) noexcept {try{if(api_)api_->LoggerOutString(level,wide(s).c_str());}catch(...) {}}
  void report() {
    const auto text=engine_->report_tsv();if(text==last_report_)return;
    auto destination=std::filesystem::u8path(settings_.journal+".tsv");auto temp=destination;temp+=L".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<text;out.flush();if(!out)throw std::runtime_error("report write failed");}
    if(!MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("report replace failed");
    last_report_=text;
  }
  void log_stats() {
    const auto s=engine_->stats();std::ostringstream summary;
    auto category=[&](const char* label,std::uint64_t candidates,std::uint64_t chosen) {
      summary<<label<<" candidates="<<candidates<<" chosen="<<chosen<<" rate=";
      if(candidates)summary<<std::fixed<<std::setprecision(2)<<(100.0*static_cast<double>(chosen)/static_cast<double>(candidates))<<'%';
      else summary<<"n/a";
    };
    summary<<"ClosedTradeAllocator source="<<settings_.config.source<<" target="<<settings_.config.target
      <<" Enabled="<<settings_.config.enabled<<" Armed="<<settings_.config.armed<<"; ";
    category("profit",s.profit_candidates,s.profit_selected);summary<<"; ";
    category("loss",s.loss_candidates,s.loss_selected);
    summary<<"; skipped="<<s.skipped<<" selected="<<s.selected<<" writing="<<s.writing<<" completed="<<s.completed<<" review="<<s.review;
    const Job* pending=nullptr;
    for(const auto& pair:engine_->jobs()) {
      const auto& j=pair.second;if(!j.chosen)continue;
      if(j.state==State::review) {pending=&j;break;}
      if(!pending&&j.state==State::selected)pending=&j;
    }
    if(pending)summary<<"; sample_source="<<pending->trade.id<<" state="<<state_name(pending->state)<<" reason="<<pending->reason;
    const auto text=summary.str();if(text!=last_stats_){log(MTLogOK,text);last_stats_=text;}
  }
  void run() noexcept {
    try {run_worker();}
    catch(const std::exception& e) {log(MTLogErr,std::string("ClosedTradeAllocator worker stopped: ")+e.what());}
    catch(...) {log(MTLogErr,"ClosedTradeAllocator worker stopped with an unknown error");}
  }
  void run_worker() {
    if(!SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL)) {
      log(MTLogErr,"ClosedTradeAllocator worker stopped: cannot lower background thread priority");return;
    }
    auto next_tick=steady_ms(),next_config=next_tick,next_scan=next_tick,next_pump=next_tick,next_metrics=next_tick+60000;
    auto cursor=engine_->scan_from();std::int64_t scan_end=0,window=3600,max_work_ms=0;
    bool recovering=true,audit_enqueue=true,history_caught_up=false;std::string audit_after;
    unsigned phase=0;Stats stats=engine_->stats();
    while(!stopping_) {
      const auto tick=steady_ms();
      if(tick<next_tick) {WaitForSingleObject(wake_event_,static_cast<DWORD>(std::min<std::int64_t>(next_tick-tick,1000)));continue;}
      wake_pending_.store(false,std::memory_order_release);
      bool changed=false;std::int64_t cooldown=1000;
      try {
        if(tick>=next_config) {
          auto fresh=settings(api_);require(fresh.journal==settings_.journal,"changing JournalPath while running is forbidden");
          engine_->configure(fresh.config,api_->TimeCurrent());settings_=fresh;adapter_->configure(engine_->config());
          next_config=tick+5000;log_stats();
        }
        if(resync_.exchange(false)) {
          cursor=engine_->scan_from();scan_end=0;window=3600;next_scan=tick;recovering=true;
          audit_enqueue=true;history_caught_up=false;audit_after.clear();
          log(MTLogErr,"ClosedTradeAllocator source events need resync; new writes paused until bounded catch-up completes");
        }
        m::SourceEvent event;
        for(unsigned i=0;i<256&&events_.pop(event);++i)adapter_->touch(event.position,tick,event.closing);
        if(audit_enqueue) {
          auto it=engine_->jobs().upper_bound(audit_after);
          for(unsigned count=0;it!=engine_->jobs().end()&&count<64&&adapter_->pending()<4096;++count,++it) {
            audit_after=it->first;
            if(!it->second.source_changed)adapter_->audit(std::stoull(it->second.trade.id),tick);
          }
          if(it==engine_->jobs().end())audit_enqueue=false;
        }
        // Round-robin stages prevent a busy source, a retry, or catch-up from
        // monopolizing the worker. No loop drains the entire selected backlog.
        for(unsigned step=0;step<3&&!stopping_;++step) {
          const auto stage=phase;phase=(phase+1)%3;
          if(stage==0&&adapter_->ready(tick)) {
            const auto before=adapter_->observations();
            adapter_->scan_ready(*engine_,settings_.settle,std::max(5u,settings_.poll),tick,steady_ms()+100,stopping_);
            changed=adapter_->observations()!=before;break;
          }
          if(stage==1&&tick>=next_pump&&(stats.review||stats.writing||
              (!recovering&&!resync_.load()&&settings_.config.enabled&&settings_.config.armed&&stats.selected))) {
            const auto completed=stats.completed;
            engine_->pump(*adapter_,PumpBudget{recovering||resync_.load()?0u:1u,1});changed=true;
            stats=engine_->stats();
            next_pump=steady_ms()+(stats.completed>completed?5000:static_cast<std::int64_t>(std::max(5u,settings_.poll))*1000);break;
          }
          if(stage==2&&tick>=next_scan) {
            const auto server_now=api_->TimeCurrent();
            if(!scan_end)scan_end=server_now;
            if(cursor>scan_end) {cursor=engine_->scan_from();scan_end=server_now;}
            const auto to=std::min(scan_end,cursor+window);
            try {adapter_->discover(cursor,to,tick);}
            catch(const PartialQuery&) {
              recovering=true;history_caught_up=false;require(window>1,"too many source deals in one second; catch-up paused without truncating history");
              window=std::max<std::int64_t>(1,window/2);next_scan=tick+5000;break;
            }
            if(to>=scan_end) {
              cursor=std::max(engine_->scan_from(),scan_end-60);scan_end=0;window=3600;
              next_scan=tick+static_cast<std::int64_t>(settings_.rescan)*1000;
              history_caught_up=true;
            } else {cursor=to;next_scan=tick+1000;}
            break;
          }
        }
        if(recovering&&history_caught_up&&!audit_enqueue&&!adapter_->audit_pending()&&!resync_.load()) {
          recovering=false;log(MTLogOK,"ClosedTradeAllocator source catch-up and frozen-source audit complete; normal bounded processing resumed");
        }
        if(changed) {stats=engine_->stats();log_stats();report();}
        // Ensure a status file exists even before the first source close.
        if(last_report_.empty())report();
      } catch(const ApiError& e) {
        log(MTLogErr,std::string("ClosedTradeAllocator background read failed; retry is throttled: ")+e.what());
        cooldown=static_cast<std::int64_t>(std::max(5u,settings_.poll))*1000;
        stats=engine_->stats();log_stats();report();
      } catch(const std::exception& e) {
        // A journal/checkpoint failure, malformed config or unexpected failure
        // permanently stops this worker. No unpersisted submission can follow.
        log(MTLogErr,std::string("ClosedTradeAllocator halted; restart only after review: ")+e.what());break;
      }
      const auto elapsed=steady_ms()-tick;max_work_ms=std::max(max_work_ms,elapsed);
      // Soft budget: never interrupt an in-flight financial write. Slow SDK
      // operations reduce subsequent load instead of triggering more work.
      if(elapsed>1000)cooldown=std::max<std::int64_t>(cooldown,30000);
      else if(elapsed>100)cooldown=std::max<std::int64_t>(cooldown,5000);
      if(steady_ms()>=next_metrics) {
        std::ostringstream msg;msg<<"ClosedTradeAllocator load: pending_positions="<<adapter_->pending()
          <<" source_queries="<<adapter_->source_queries()<<" catchup_queries="<<adapter_->discovery_queries()
          <<" event_overflows="<<overflows_.load()<<" max_work_ms="<<max_work_ms<<" cooldown_ms="<<cooldown;
        log(MTLogOK,msg.str());max_work_ms=0;next_metrics=steady_ms()+60000;
      }
      next_tick=steady_ms()+cooldown;
    }
  }
  IMTServerAPI* api_=nullptr;Settings settings_;std::unique_ptr<Engine> engine_;std::unique_ptr<Adapter> adapter_;
  std::atomic<bool> stopping_{true},resync_{false},wake_pending_{false};
  std::atomic<std::uint64_t> watched_source_{0},overflows_{0};
  m::SourceEvents<4096> events_;m::CallbackGate callbacks_;
  std::thread worker_;std::mutex lifecycle_;
  HANDLE target_lock_=nullptr,wake_event_=nullptr;
  std::string last_stats_,last_report_;
};
} // namespace

// The static CRT needs thread attach/detach notifications.
BOOL APIENTRY DllMain(HMODULE,DWORD,LPVOID) {return TRUE;}
MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info) {
  info={};info.version=110;info.version_api=MTServerAPIVersion;
  wcscpy_s(info.name,L"Closed Trade Allocator");wcscpy_s(info.copyright,L"Closed Trade Allocator");
  wcscpy_s(info.description,L"Allocate fully closed source history by profit/loss probability; independent Server API plugin");
  const MTPluginParam defaults[]={
    {MTPluginParam::TYPE_INT,L"SourceLogin",L"0"},{MTPluginParam::TYPE_INT,L"TargetLogin",L"0"},
    {MTPluginParam::TYPE_FLOAT,L"LossPercent",L"30"},{MTPluginParam::TYPE_FLOAT,L"ProfitPercent",L"70"},
    {MTPluginParam::TYPE_INT,L"Enabled",L"1"},{MTPluginParam::TYPE_INT,L"Armed",L"0"},
    {MTPluginParam::TYPE_INT,L"PollSeconds",L"30"},{MTPluginParam::TYPE_INT,L"SettleSeconds",L"5"},
    {MTPluginParam::TYPE_INT,L"RescanSeconds",L"300"},
    {MTPluginParam::TYPE_STRING,L"JournalPath",L"C:\\ClosedTradeAllocator\\allocator-mt5.journal"}
  };
  std::copy(std::begin(defaults),std::end(defaults),std::begin(info.defaults));info.defaults_total=static_cast<std::uint32_t>(std::size(defaults));return MT_RET_OK;
}
MTAPIENTRY MTAPIRES MTServerCreate(std::uint32_t version,IMTServerPlugin** plugin) {
  if(!plugin)return MT_RET_ERR_PARAMS;*plugin=nullptr;
  if(version!=MTServerAPIVersion)return MT_RET_ERR_NOTSUPPORTED;
  try {*plugin=new(std::nothrow) Plugin;return *plugin?MT_RET_OK:MT_RET_ERR_MEM;}
  catch(...) {return MT_RET_ERR_MEM;}
}

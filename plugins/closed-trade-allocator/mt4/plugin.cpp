// Actual Server API plugin: no Manager API, terminal EA, trade requests or routes.
#if !defined(_WIN32) || !defined(_MSC_VER) || !defined(_M_IX86)
#error MT4ClosedTradeAllocator requires Windows MSVC Win32.
#endif
#include "sdk_port.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace {
using namespace mt4alloc;
class Instance {
public:
  Instance(CServerInterface* api,Parameters p):api_(api),parameters_(std::move(p)){}
  ~Instance(){stop();}
  void start() {
    if(!api_ || api_->Version()!=ServerApiVersion)throw std::runtime_error("MT4 Server API version mismatch");
    if(!parameters_.configured()){log(api_,"MT4ClosedTradeAllocator is unconfigured; Armed=0; no writes");return;}
    ConCommon common{};api_->CommonGet(&common);
    if(common.server_version!=400 || common.server_build!=parameters_.server_build || fixed(common.name)!=parameters_.server_name)
      throw std::runtime_error("MT4 server name/version/build does not match configured identity");
    server_identity_=fixed(common.name)+";address="+std::to_string(common.address)+";port="+std::to_string(common.port);
    const std::filesystem::path path=std::filesystem::u8path(parameters_.journal);
    const auto drive=path.root_name().native();
    if(!path.is_absolute() || drive.size()!=2 || drive[1]!=L':' || GetDriveTypeW(path.root_path().c_str())!=DRIVE_FIXED)
      throw std::runtime_error("JournalPath must be an absolute path on a fixed local Windows drive");
    // One allocator per physical server prevents accidental parallel relations
    // with separate journal paths. A journal has its own independent file lock.
    std::wstring name=L"Global\\MT4ClosedTradeAllocator."+std::to_wstring(common.address)+L"."+std::to_wstring(common.port);
    lease_=CreateMutexW(nullptr,FALSE,name.c_str());const auto error=GetLastError();
    if(!lease_ || error==ERROR_ALREADY_EXISTS)throw std::runtime_error("another closed-trade allocator already owns this MT4 server");
    {std::lock_guard<std::mutex> lock(wake_mutex_);
      wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!wake_)throw std::runtime_error("cannot create allocator worker event");}
    port_=std::make_unique<SdkPort>(api_,parameters_);
    engine_=std::make_unique<closedalloc::Engine>(parameters_.journal,config(parameters_,server_identity_),port_->now());
    write_report();
    log(api_,std::string("MT4ClosedTradeAllocator started; Armed=")+(parameters_.armed?"1":"0")+
      "; target Comment is empty; private API provenance enabled; partial closes excluded");
    thread_=std::thread([this]{run();});
  }
  void stop() noexcept {
    stopping_.store(true);wake();
    if(thread_.joinable())try{thread_.join();}catch(...){std::terminate();}
    engine_.reset();port_.reset();
    // All callbacks hold shared_ptr<Instance>, and wake() is serialized with
    // handle closure. No callback reads SDK or accesses the engine.
    std::lock_guard<std::mutex> lock(wake_mutex_);
    if(wake_){CloseHandle(wake_);wake_=nullptr;}
    if(lease_){CloseHandle(lease_);lease_=nullptr;}
  }
  void wake() noexcept {
    try{std::lock_guard<std::mutex> lock(wake_mutex_);if(wake_)SetEvent(wake_);}catch(...){}
  }
  bool configured() const {return parameters_.configured();}
  bool update(const Parameters& next) {
    if(faulted_.load() || !same_identity(parameters_,next))return false;
    {
      std::lock_guard<std::mutex> lock(update_mutex_);
      if(updates_.size()>=256)return false;
      updates_.push_back(next);
    }
    wake();return true;
  }
private:
  struct Stable {std::string payload;std::int64_t first_seen=0;};
  void write_report() {
    const auto report=std::filesystem::u8path(parameters_.journal+".status.tsv");
    const auto temporary=std::filesystem::u8path(parameters_.journal+".status.tmp");
    std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
    const auto text=engine_->report_tsv();
    if(!stream || !stream.write(text.data(),static_cast<std::streamsize>(text.size())) || !stream.flush())
      throw std::runtime_error("cannot write allocator status report");
    stream.close();
    if(!MoveFileExW(temporary.c_str(),report.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("cannot publish allocator status report");
  }
  void scan(std::int64_t now) {
    auto records=port_->history(parameters_.source);
    const auto source=port_->account(parameters_.source);
    const auto open=port_->open_orders(source.user);
    auto all=records;all.insert(all.end(),open.begin(),open.end());
    const auto linked=linked_tickets(all);
    const auto currency=fixed(source.user.grp.currency);
    std::set<int> present;
    for(const auto& record:records) {
      if((record.side!=OP_BUY && record.side!=OP_SELL) || record.closed<=engine_->scan_from())continue;
      present.insert(record.ticket);
      const auto snapshot=encode({record,currency,parameters_.currency_digits});
      auto& stable=stable_[record.ticket];
      if(stable.payload!=snapshot){stable.payload=snapshot;stable.first_seen=now;}
      if(record.closed>now || now-record.closed<parameters_.settle_seconds || now-stable.first_seen<parameters_.settle_seconds)continue;
      closedalloc::Trade trade;trade.id=std::to_string(record.ticket);trade.closed_at=record.closed;
      trade.profit=record.profit;trade.commission=record.commission;trade.swap=record.swap;trade.fee=record.taxes;
      trade.currency_digits=parameters_.currency_digits;trade.payload=snapshot;
      trade.exclusion=generated(record)?"allocator-generated record cannot become a source":partial_reason(record,{});
      if(trade.exclusion.empty() && linked.count(record.ticket))trade.exclusion="linked partial-close ticket is unsupported";
      engine_->observe(trade,now);
    }
    // Bound volatile stabilization cache to the actual source history snapshot.
    for(auto it=stable_.begin();it!=stable_.end();)if(!present.count(it->first))it=stable_.erase(it);else ++it;
  }
  void run() noexcept {
    try {
      auto next_scan=std::chrono::steady_clock::now();
      while(!stopping_.load()) {
        std::deque<Parameters> pending;
        {std::lock_guard<std::mutex> lock(update_mutex_);pending.swap(updates_);}
        for(const auto& next:pending)engine_->configure(config(next,server_identity_),port_->now());
        const auto clock=std::chrono::steady_clock::now();
        if(clock>=next_scan || !pending.empty()) {
          const auto now=port_->now();
          scan(now);
          // Engine reconciles every pending write before permitting new writes.
          port_->block_submissions(false);
          engine_->pump(*port_);write_report();
          const auto stats=engine_->stats();
          const auto rate=[](std::uint64_t selected,std::uint64_t candidates) {
            return candidates?std::to_string(100.0*static_cast<double>(selected)/static_cast<double>(candidates))+"%":std::string("n/a");
          };
          const auto summary="completed="+std::to_string(stats.completed)+" selected="+std::to_string(stats.selected)+
            " review="+std::to_string(stats.review)+" skipped="+std::to_string(stats.skipped)+
            " profit_candidates="+std::to_string(stats.profit_candidates)+" profit_selected="+std::to_string(stats.profit_selected)+
            " profit_rate="+rate(stats.profit_selected,stats.profit_candidates)+
            " loss_candidates="+std::to_string(stats.loss_candidates)+" loss_selected="+std::to_string(stats.loss_selected)+
            " loss_rate="+rate(stats.loss_selected,stats.loss_candidates);
          if(summary!=last_summary_){log(api_,"MT4 allocation "+summary);last_summary_=summary;}
          next_scan=clock+std::chrono::seconds(parameters_.poll_seconds);
        }
        if(!stopping_.load())WaitForSingleObject(wake_,1000);
      }
    }catch(const std::exception& error){faulted_.store(true);log(api_,std::string("MT4 allocator stopped safely: ")+error.what());}
    catch(...){faulted_.store(true);log(api_,"MT4 allocator stopped safely after an unknown failure");}
  }
  CServerInterface* api_;
  const Parameters parameters_;
  std::string server_identity_,last_summary_;
  HANDLE wake_=nullptr,lease_=nullptr;
  std::thread thread_;
  std::atomic<bool> stopping_{false},faulted_{false};
  std::mutex wake_mutex_,update_mutex_;
  std::deque<Parameters> updates_;
  std::map<int,Stable> stable_;
  std::unique_ptr<SdkPort> port_;
  std::unique_ptr<closedalloc::Engine> engine_;
};
std::mutex lifecycle;
CServerInterface* server=nullptr;
Values values=defaults();
std::shared_ptr<Instance> current;
void close() {
  auto previous=std::atomic_exchange(&current,std::shared_ptr<Instance>{});
  if(previous)previous->stop();
}
bool start(const Values& next) {
  try {
    auto instance=std::make_shared<Instance>(server,parse(next));
    std::atomic_store(&current,instance);
    try{instance->start();}catch(...){close();throw;}
    return true;
  }catch(const std::exception& error){log(server,std::string("MT4 allocator startup failed: ")+error.what());}
  catch(...){log(server,"MT4 allocator startup failed");}
  return false;
}
void wake() noexcept {const auto active=std::atomic_load(&current);if(active)active->wake();}
}
void APIENTRY MtSrvAbout(PluginInfo* info) {
  if(!info)return;std::memset(info,0,sizeof(*info));
  std::strcpy(info->name,"MT4 Closed Trade Allocator");info->version=100;
  std::strcpy(info->copyright,"Closed Trade Allocator project");
}
int APIENTRY MtSrvStartup(CServerInterface* api) {
  try{std::lock_guard<std::mutex> lock(lifecycle);if(!api || server)return FALSE;
    server=api;if(start(values))return TRUE;server=nullptr;}catch(...){}return FALSE;
}
void APIENTRY MtSrvCleanup() {
  try{std::lock_guard<std::mutex> lock(lifecycle);close();server=nullptr;}catch(...){}
}
int APIENTRY MtSrvPluginCfgSet(const PluginCfg* incoming,const int total) {
  try {
    if(total<0 || total>32 || (total && !incoming))return FALSE;
    auto next=defaults();std::set<std::string> seen;
    for(int i=0;i<total;++i){const auto name=fixed(incoming[i].name);const auto value=fixed(incoming[i].value);
      if(!next.count(name) || !seen.insert(name).second)return FALSE;next[name]=value;}
    const auto parameters=parse(next);
    std::lock_guard<std::mutex> lock(lifecycle);
    if(next==values)return TRUE;
    const auto active=std::atomic_load(&current);
    if(server && active && active->configured()) {
      // Identity and evidence settings are immutable until the DLL is unloaded.
      // Live Enabled/Armed/probability updates are journaled by the sole worker.
      if(!active->update(parameters)) {
        log(server,"MT4 allocator rejected immutable parameter change or faulted worker; unload/reload for identity changes");return FALSE;
      }
    }else if(server){close();if(!start(next)){start(values);return FALSE;}}
    values=std::move(next);return TRUE;
  }catch(const std::exception& error){
    // Startup/Cleanup may run concurrently after the earlier lock unwinds.
    try{std::lock_guard<std::mutex> lock(lifecycle);log(server,std::string("invalid MT4 allocator parameters: ")+error.what());}catch(...){}
  }
  catch(...){}return FALSE;
}
int APIENTRY MtSrvPluginCfgNext(const int index,PluginCfg* value) {
  try{std::lock_guard<std::mutex> lock(lifecycle);if(!value || index<0 || static_cast<std::size_t>(index)>=values.size())return FALSE;
    auto it=values.begin();std::advance(it,index);std::memset(value,0,sizeof(*value));
    std::memcpy(value->name,it->first.c_str(),it->first.size()+1);std::memcpy(value->value,it->second.c_str(),it->second.size()+1);return TRUE;
  }catch(...){}return FALSE;
}
int APIENTRY MtSrvPluginCfgTotal(){try{std::lock_guard<std::mutex> lock(lifecycle);return static_cast<int>(values.size());}catch(...){return 0;}}
void APIENTRY MtSrvTradesAdd(TradeRecord*,const UserInfo*,const ConSymbol*){wake();}
void APIENTRY MtSrvTradesAddExt(TradeRecord*,const UserInfo*,const ConSymbol*,const int){wake();}
void APIENTRY MtSrvTradesUpdate(TradeRecord*,UserInfo*,const int){wake();}
void APIENTRY MtSrvTradesRestore(const TradeRecord*){wake();}

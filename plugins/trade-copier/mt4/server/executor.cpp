// MT4 Server API 5, MSVC Win32. No Manager API or terminal EA is loaded.
#if !defined(_WIN32) || !defined(_MSC_VER) || !defined(_M_IX86)
#error MT4TradeCopierExecutor requires Windows MSVC Win32.
#endif
#include "sdk_broker.hpp"
#include "event_queue.hpp"
#include "deferred_admission.hpp"
#include "worker_wake.hpp"
#include "diagnostic_text.hpp"
#include "tradecopier/journal.hpp"
#include "local_pipe.hpp"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

using namespace tradecopier;
using namespace tradecopier::execution;
using mt4copier::Parameters;
using mt4copier::Fixed;
namespace {
struct RawRequest {
    bool present=false;
    int id=0, login=0;
    TradeTransInfo trade{};
};
struct Event {
    TradeRecord trade{};
    RawRequest request{};
    bool opening=false;
};
struct RequestDiagnostic {
    RequestInfo request{};
    int isdemo=-1; // Restore has no isdemo argument; -1 means unavailable.
    bool restored=false;
};
struct Watch { std::set<Id> sources,activity; };
void RawLog(CServerInterface* api,const std::string& message) noexcept {
    if (api) try { api->LogsOut(CmdAtt,"mt4-copier",message.c_str()); } catch (...) {}
}
CopierConfig ReadConfig(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path)>kMaxConfigBytes) throw std::runtime_error("config exceeds size limit");
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read ConfigPath");
    std::string text((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    if (stream.bad()) throw std::runtime_error("ConfigPath read failed");
    CopierConfig config; std::string error;
    if (!ParseConfigJson(text,config,error) || !config.enabled) throw std::runtime_error("MT4 config not enabled/valid: "+error);
    return config;
}
std::string Hex(const std::string& text) {
    constexpr char digits[]="0123456789abcdef";
    std::string out;
    for (unsigned char c:text) { out+=digits[c>>4]; out+=digits[c&15]; }
    return out;
}
class Executor {
public:
    Executor(CServerInterface* api,Parameters parameters) : api_(api),parameters_(std::move(parameters)),mask_(parameters_.symbol) {}
    ~Executor() { Stop(); }
    bool Armed() const noexcept { return parameters_.armed; }
    void Start() {
        if (!api_ || api_->Version()!=ServerApiVersion) throw std::runtime_error("MT4 Server API version mismatch");
        ConCommon common{}; api_->CommonGet(&common);
        RawLog(api_,"MT4TradeCopierExecutor 0.2.0: api="+std::to_string(api_->Version())+
            " server="+Fixed(common.name)+" build="+std::to_string(common.server_build)+
            " x86 request_diagnostics=2 dealer_execution=lock_confirm DealerLogin="+std::to_string(parameters_.dealer_login));
        if (!parameters_.armed) { RawLog(api_,"MT4TradeCopierExecutor DISARMED: no trading or management listener"); return; }
        if (common.server_version!=400 || common.server_build!=static_cast<int>(parameters_.build) ||
            Fixed(common.name)!=parameters_.server_name)
            throw std::runtime_error("MT4 Trade Server name/version/build mismatch");
        config_path_=std::filesystem::path(parameters_.config);
        journal_path_=std::filesystem::path(parameters_.journal);
        if (!config_path_.is_absolute() || !journal_path_.is_absolute() ||
            config_path_.lexically_normal()==journal_path_.lexically_normal())
            throw std::runtime_error("ConfigPath and JournalPath must be distinct absolute paths");
        const auto drive=journal_path_.root_name().native();
        if (drive.size()!=2 || drive[1]!=L':' || GetDriveTypeW(journal_path_.root_path().c_str())!=DRIVE_FIXED)
            throw std::runtime_error("JournalPath must be on a local fixed drive");
        // The lease is tied to the actual server, not operator-chosen IDs.
        const auto lease_name=L"Global\\MT4TradeCopierExecutor.server."+
            std::to_wstring(common.address)+L"."+std::to_wstring(common.port)+L"."+
            std::filesystem::path(Hex(Fixed(common.name))).wstring();
        lease_=CreateMutexW(nullptr,FALSE,lease_name.c_str());
        const auto lease_error=GetLastError();
        if (!lease_ || lease_error==ERROR_ALREADY_EXISTS) throw std::runtime_error("another MT4 executor owns this server");
        queue_=std::make_unique<EventQueue<Event,4096>>();
        request_diagnostics_=std::make_unique<EventQueue<RequestDiagnostic,256>>();
        journal_=std::make_unique<FileJournal>(journal_path_);
        api_gate_.Open();
        broker_=std::make_unique<mt4copier::SdkBroker>(api_,api_gate_,parameters_.dealer_login);
        port_=std::make_unique<mt4copier::Port>(*broker_,parameters_.app);
        const auto identity="mt4;server="+std::to_string(parameters_.server_key)+";api=5;app="+
            std::to_string(parameters_.app)+";build="+std::to_string(parameters_.build)+
            ";name="+mt4copier::Utf8(Fixed(common.name))+";address="+std::to_string(common.address)+
            ";port="+std::to_string(common.port);
        engine_=std::make_unique<Engine>([this]{return ReadConfig(config_path_);},identity,*port_,*journal_,parameters_.management);
        std::string error;
        if (!ParseConfigJson(engine_->state().config_json,config_,error)) throw std::runtime_error(error);
        SetWatch(config_);
        if (parameters_.management) {
            unsigned char nonce[16]{};
            if (BCryptGenRandom(nullptr,nonce,sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
                throw std::runtime_error("management session nonce generation failed");
            session_=Hex(std::string(reinterpret_cast<char*>(nonce),sizeof(nonce)));
            management_=std::make_unique<management::PipeServer>(parameters_.server_key,parameters_.app);
        }
        stopping_.store(false,std::memory_order_release);
        callbacks_.Open();
        worker_=std::thread([this]{Run();});
    }
    void Stop() noexcept {
        // No new SDK calls after closing. Admitted calls and callback copies
        // finish before any objects they access are freed.
        callbacks_.Close(); api_gate_.Close();
        stopping_.store(true,std::memory_order_release);
        try { wake_.Signal(); } catch (...) {}
        try { if (worker_.joinable()) worker_.join(); } catch (...) { std::terminate(); }
        callbacks_.Drain(); api_gate_.Drain();
        management_.reset(); engine_.reset(); port_.reset(); broker_.reset(); journal_.reset(); queue_.reset();
        request_diagnostics_.reset();
        if (lease_) { CloseHandle(lease_); lease_=nullptr; }
    }
    void Observe(const TradeRecord* trade,const RequestInfo* request,bool opening) noexcept {
        if (!callbacks_.Enter()) return;
        try {
            const auto watch=std::atomic_load(&watch_);
            if (trade && watch && trade->login>0 && watch->activity.count(static_cast<Id>(trade->login)) &&
                (trade->cmd==OP_BUY || trade->cmd==OP_SELL)) {
                Event event; event.trade=*trade; event.trade.next=nullptr;
                event.opening=opening && watch->sources.count(static_cast<Id>(trade->login));
                if (request) {
                    event.request.present=true; event.request.id=request->id;
                    event.request.login=request->login; event.request.trade=request->trade;
                }
                if (!queue_->Push(event)) callback_fault_.store(true,std::memory_order_release);
                wake_.Signal();
            }
        } catch (...) { callback_fault_.store(true,std::memory_order_release); }
        callbacks_.Leave();
    }
    void ObserveRequest(const RequestInfo* request,int isdemo,bool restored) noexcept {
        if (!callbacks_.Enter()) return;
        try {
            const auto watch=std::atomic_load(&watch_);
            if (request && watch && request->login>0 && watch->activity.count(static_cast<Id>(request->login)) &&
                (request->trade.cmd==OP_BUY || request->trade.cmd==OP_SELL)) {
                // Copy only; never call SDK, confirm/reset requests, or format
                // logs on the server callback thread. This queue is separate
                // from trade admission/evidence and cannot overflow that queue.
                RequestDiagnostic event; event.request=*request;
                event.request.prev=nullptr; event.request.next=nullptr;
                event.isdemo=isdemo; event.restored=restored;
                if (!request_diagnostics_->Push(event)) diagnostic_drops_.fetch_add(1,std::memory_order_relaxed);
                wake_.Signal();
            }
        } catch (...) { diagnostic_drops_.fetch_add(1,std::memory_order_relaxed); }
        callbacks_.Leave();
    }
private:
    void ProcessRequestDiagnostics() noexcept {
        // These observations are not execution evidence: Applied/Restored and
        // request.status do not establish a fill or a final rejection.
        RequestDiagnostic event; Id sequence=0;
        for (unsigned n=0;n<32 && request_diagnostics_->Pop(event,sequence);++n) {
            try {
                const auto& r=event.request;
                port_->Log(std::string(event.restored?"MT4_REQUEST_RESTORE":"MT4_REQUEST_APPLY")+
                    " request="+std::to_string(r.id)+" login="+std::to_string(r.login)+
                    " group="+mt4copier::DiagnosticText(mt4copier::Utf8(Fixed(r.group)))+" status="+std::to_string(r.status)+
                    " manager="+std::to_string(r.manager)+" native_time="+std::to_string(r.time)+
                    " isdemo="+std::to_string(event.isdemo)+" type="+std::to_string(r.trade.type)+
                    " cmd="+std::to_string(r.trade.cmd)+" symbol="+mt4copier::DiagnosticText(mt4copier::Utf8(Fixed(r.trade.symbol)))+
                    " volume="+std::to_string(r.trade.volume)+" order="+std::to_string(r.trade.order)+
                    " flags="+std::to_string(static_cast<unsigned char>(r.trade.flags))+
                    " comment="+mt4copier::DiagnosticText(mt4copier::Utf8(Fixed(r.trade.comment))));
            } catch (...) { diagnostic_drops_.fetch_add(1,std::memory_order_relaxed); }
        }
        const auto dropped=diagnostic_drops_.exchange(0,std::memory_order_relaxed);
        if (dropped) try {
            port_->Log("MT4_REQUEST_DIAGNOSTICS_DROPPED count="+std::to_string(dropped)+
                "; request trace incomplete; trading state unchanged");
        } catch (...) {} // Logging failure is not a trading fault.
    }
    void SetWatch(const CopierConfig& config) {
        auto watch=std::make_shared<Watch>();
        for (const auto& rule:config.relationships) if (rule.enabled) {
            watch->sources.insert(rule.source_login);
            watch->activity.insert(rule.source_login); watch->activity.insert(rule.follower_login);
        }
        for (const auto& link:engine_->state().links) if (link.opening_rule &&
            link.phase!=Phase::Closed && link.phase!=Phase::ManualClosed && link.phase!=Phase::Skipped) {
            watch->sources.insert(link.source.login); watch->activity.insert(link.source.login);
            watch->activity.insert(link.opening_rule->follower_login);
        }
        std::shared_ptr<const Watch> immutable=watch;
        std::atomic_store(&watch_,std::move(immutable));
    }
    BootSnapshot Baseline(const CopierConfig& config) {
        std::set<Id> logins;
        for (const auto& r:config.relationships) if (r.enabled) logins.insert(r.source_login);
        return port_->Snapshot({logins.begin(),logins.end()},queue_->Watermark());
    }
    void Evidence(const Event& event) {
        if (!event.request.present) return;
        const auto& request=event.request; const auto& trans=request.trade;
        if (request.id<=0 || request.login!=event.trade.login || !(trans.flags&TT_FLAG_API) ||
            (trans.type!=TT_ORDER_MK_OPEN && trans.type!=TT_ORDER_MK_CLOSE) ||
            (trans.cmd!=OP_BUY && trans.cmd!=OP_SELL) || trans.cmd!=event.trade.cmd ||
            Fixed(trans.symbol)!=Fixed(event.trade.symbol)) return;
        const auto token=Fixed(trans.comment);
        if (!mt4copier::Port::IsCopierToken(token)) return;
        const bool close=trans.type==TT_ORDER_MK_CLOSE;
        if (close) {
            if (trans.order<=0) return;
            const bool closed=event.trade.order==trans.order && event.trade.close_time>0 &&
                (event.trade.state==TS_CLOSED_NORMAL || event.trade.state==TS_CLOSED_PART);
            const bool remainder=event.trade.state==TS_OPEN_REMAND && !event.trade.close_time &&
                Fixed(event.trade.comment)=="from #"+std::to_string(trans.order);
            if (!closed && !remainder) return;
        } else if (event.trade.state!=TS_OPEN_NORMAL || event.trade.close_time || event.trade.order<=0 ||
                   Fixed(event.trade.comment)!=token || event.trade.volume!=trans.volume) return;
        mt4copier::RequestEvidence evidence;
        evidence.request_id=static_cast<std::uint32_t>(request.id);
        evidence.login=static_cast<Id>(request.login);
        evidence.ticket=static_cast<Id>(close?trans.order:event.trade.order);
        evidence.close=close; evidence.position_side=trans.cmd==OP_BUY?Side::Buy:Side::Sell;
        evidence.symbol=mt4copier::Utf8(Fixed(trans.symbol)); evidence.token=token; evidence.volume=trans.volume;
        evidence.time_ms=port_->NowMs();
        // This is a correlation hint. Port must independently prove matching
        // order identity and the actual filled/closed volume from SDK history.
        port_->ObserveRequest(evidence);
    }
    void ProcessEvents() {
        std::vector<std::pair<Deal,Id>> openings;
        openings.reserve(Engine::kAdmissionBatch);
        port_->BeginCycle();
        deferred_.Process(Engine::kAdmissionBatch,engine_->state().activation_sequence,
            [this](Event& event,Id& sequence) { return queue_->Pop(event,sequence); },
            [this](const Event& event) {
                // Any update to a descendant ticket wakes its durable root
                // mapping exactly once, including during unstable snapshots.
                for (const auto& link:engine_->state().links) if (link.source.login==static_cast<Id>(event.trade.login))
                    engine_->NotifySourceClose(link.source.login,link.source.position);
                try { Evidence(event); return event.opening; }
                catch (const mt4copier::Stopping&) { throw; }
                catch (const std::exception& error) {
                    engine_->ReportFault(std::string("MT4 callback evidence rejected: ")+error.what()); return false;
                }
            },
            [this,&openings](const Deferred::Item& item) {
                try {
                    const auto& event=item.event;
                    const auto opening=port_->Opening(static_cast<Id>(event.trade.login),static_cast<Id>(event.trade.order));
                    if (opening && mask_.Matches(opening->symbol)) openings.emplace_back(*opening,item.sequence);
                    return true;
                } catch (const mt4copier::IncompleteSnapshot&) { return false; }
                catch (const mt4copier::Stopping&) { throw; }
                catch (const std::exception& error) {
                    engine_->ReportFault(std::string("MT4 source opening rejected: ")+error.what()); return true;
                }
            },
            [this](const Deferred::Item& item,Deferred::Skip reason) {
                const auto text=reason==Deferred::Skip::Expired?"deferred_snapshot_timeout":
                    reason==Deferred::Skip::BeforeBaseline?"before_activation_baseline":"deferred_admission_capacity";
                try {
                    port_->Log(std::string("SKIPPED MT4 source admission reason=")+text+
                        " login="+std::to_string(item.event.trade.login)+" ticket="+std::to_string(item.event.trade.order)+
                        " callback_sequence="+std::to_string(item.sequence));
                } catch (const mt4copier::Stopping&) { throw; }
                catch (...) {} // Expected skips do not turn snapshot lag into a global fault.
            },
            [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(); });
        // Original callback sequence and source deal time also preserve every
        // per-subscription join barrier. Deferred events never become catch-up.
        engine_->AdmitBatch(openings,false);
    }
    void HandleManagement() {
        if (!management_) return;
        const auto message=management_->Poll(); if (!message) return;
        bool ok=true; std::string result;
        try {
            const auto request=management::DecodeRequest(*message);
            if (request.operation!=management::Operation::Status && request.session!=session_)
                throw std::runtime_error("执行器已重启，请刷新连接后重新操作");
            switch (request.operation) {
            case management::Operation::Status: break;
            case management::Operation::Pause:
                engine_->PauseOpenings(request.revision); result="已暂停新仓，已有仓位继续管理"; break;
            case management::Operation::Resume:
                engine_->ResumeOpenings(request.revision,Baseline(config_)); result="已恢复，只跟之后的新仓"; break;
            case management::Operation::Apply:
                throw std::runtime_error("请使用更新订阅功能");
            case management::Operation::UpdateSubscriptions: {
                CopierConfig candidate; std::string error;
                if (!ParseConfigJson(request.config_json,candidate,error)) throw std::runtime_error(error);
                SetWatch(candidate);
                try { engine_->UpdateSubscriptions(request.expected_config,candidate,Baseline(candidate)); }
                catch (...) { SetWatch(config_); throw; }
                config_=std::move(candidate);
                result="订阅已更新；新关系只跟新仓，退出关系只关闭其所属跟单仓位"; break;
            }
            case management::Operation::Inspect: result=engine_->InspectIntent(request.intent); break;
            }
        } catch (const mt4copier::Stopping&) { throw; }
        catch (const std::exception& error) { ok=false; result=error.what(); }
        auto response=management::Snapshot(*engine_);
        response.ok=ok; response.message=result; response.session=session_;
        response.identity=engine_->state().identity+";source-mask="+parameters_.symbol;
        response.config_path=config_path_.u8string(); response.journal_path=journal_path_.u8string();
        management_->Reply(management::EncodeResponse(response));
    }
    void Fault(const std::string& message) noexcept { try { engine_->ReportFault(message); } catch (...) {} }
    void Run() noexcept {
        bool started=false;
        while (!stopping_.load(std::memory_order_acquire)) {
            const auto token=wake_.Snapshot();
            try {
                if (!started) {
                    engine_->Start(Baseline(config_)); started=true;
                    port_->Log("READY: MT4 same-server copier; market execution; USD; strict ticket lineage; no retries on uncertain execution");
                }
                if (callback_fault_.exchange(false,std::memory_order_acq_rel))
                    engine_->ReportFault("MT4 callback queue overflow/invalid callback; some source openings were not admitted");
                ProcessEvents();
                engine_->Tick();
            } catch (const mt4copier::Stopping&) { break; }
            catch (const std::exception& error) { Fault(std::string("MT4 worker: ")+error.what()); }
            catch (...) { Fault("MT4 worker unknown exception"); }
            ProcessRequestDiagnostics();
            try { HandleManagement(); }
            catch (const mt4copier::Stopping&) { break; }
            catch (...) { management_.reset(); }
            wake_.WaitSince(token,std::chrono::milliseconds(50),stopping_);
        }
        callbacks_.Close();
    }
    CServerInterface* api_;
    Parameters parameters_;
    SymbolMask mask_;
    std::filesystem::path config_path_,journal_path_;
    HANDLE lease_=nullptr;
    mt4copier::CallGate api_gate_,callbacks_;
    WorkerWake wake_;
    std::atomic<bool> stopping_{true},callback_fault_{false};
    std::atomic<std::uint32_t> diagnostic_drops_{0};
    std::thread worker_;
    std::shared_ptr<const Watch> watch_;
    std::unique_ptr<EventQueue<Event,4096>> queue_;
    std::unique_ptr<EventQueue<RequestDiagnostic,256>> request_diagnostics_;
    using Deferred=mt4copier::DeferredAdmissions<Event,1024>;
    Deferred deferred_;
    std::unique_ptr<FileJournal> journal_;
    std::unique_ptr<mt4copier::SdkBroker> broker_;
    std::unique_ptr<mt4copier::Port> port_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<management::PipeServer> management_;
    CopierConfig config_;
    std::string session_;
};
std::mutex lifecycle;
mt4copier::ParameterValues values=mt4copier::DefaultParameters();
CServerInterface* server=nullptr;
std::shared_ptr<Executor> current;
void Observe(const TradeRecord* trade,const RequestInfo* request,bool opening) noexcept {
    const auto active=std::atomic_load(&current);
    if (active) active->Observe(trade,request,opening);
}
void ObserveRequest(const RequestInfo* request,int isdemo,bool restored) noexcept {
    const auto active=std::atomic_load(&current);
    if (active) active->ObserveRequest(request,isdemo,restored);
}
void Close() noexcept {
    auto previous=std::atomic_exchange(&current,std::shared_ptr<Executor>{});
    if (previous) previous->Stop();
}
bool Start(const mt4copier::ParameterValues& next) {
    try {
        auto instance=std::make_shared<Executor>(server,mt4copier::ParseParameters(next));
        // Publish before Start so callbacks arriving as the worker starts can
        // enter its already-open callback gate. Start failure closes everything.
        std::atomic_store(&current,instance);
        try { instance->Start(); } catch (...) { Close(); throw; }
        return true;
    } catch (const std::exception& error) { RawLog(server,std::string("MT4 copier startup failed: ")+error.what()); }
    catch (...) { RawLog(server,"MT4 copier startup failed with an unknown exception"); }
    return false;
}
}

void APIENTRY MtSrvAbout(PluginInfo* info) {
    if (!info) return;
    std::memset(info,0,sizeof(*info));
    std::strcpy(info->name,"MT4 Trade Copier"); info->version=200;
    std::strcpy(info->copyright,"Trade Copier project");
}
int APIENTRY MtSrvStartup(CServerInterface* api) {
    try {
        std::lock_guard<std::mutex> lock(lifecycle);
        if (!api || server) return FALSE;
        server=api;
        if (Start(values)) return TRUE;
        server=nullptr;
    } catch (...) {}
    return FALSE;
}
void APIENTRY MtSrvCleanup() {
    try { std::lock_guard<std::mutex> lock(lifecycle); Close(); server=nullptr; } catch (...) {}
}
int APIENTRY MtSrvPluginCfgSet(const PluginCfg* incoming,const int total) {
    try {
        if (total<0 || total>32 || (total && !incoming)) return FALSE;
        auto next=mt4copier::DefaultParameters(); std::set<std::string> seen;
        for (int i=0;i<total;++i) {
            const auto name=Fixed(incoming[i].name),value=Fixed(incoming[i].value);
            if (!next.count(name) || !seen.insert(name).second) return FALSE;
            next[name]=value;
        }
        const auto parsed=mt4copier::ParseParameters(next);
        std::lock_guard<std::mutex> lock(lifecycle);
        const auto active=std::atomic_load(&current);
        if (next==values && (!server || active)) return TRUE;
        // Invalid identity/path edits must never tear down the instance that
        // is still managing existing positions. Disarm explicitly first;
        // ordinary subscription updates use the management protocol instead.
        if (active && active->Armed() && parsed.armed) {
            RawLog(server,"MT4 copier: startup parameter change rejected while Armed=1; disarm first or use subscription management");
            return FALSE;
        }
        if (server) {
            Close();
            if (!Start(next)) {
                if (!Start(values)) RawLog(server,"MT4 copier: previous configuration could not be restored; executor is stopped");
                return FALSE;
            }
        }
        values=std::move(next); return TRUE;
    } catch (...) { return FALSE; }
}
int APIENTRY MtSrvPluginCfgNext(const int index,PluginCfg* value) {
    try {
        std::lock_guard<std::mutex> lock(lifecycle);
        if (!value || index<0 || static_cast<std::size_t>(index)>=values.size()) return FALSE;
        auto it=values.begin(); std::advance(it,index); std::memset(value,0,sizeof(*value));
        std::memcpy(value->name,it->first.c_str(),it->first.size()+1);
        std::memcpy(value->value,it->second.c_str(),it->second.size()+1); return TRUE;
    } catch (...) { return FALSE; }
}
int APIENTRY MtSrvPluginCfgTotal() {
    try { std::lock_guard<std::mutex> lock(lifecycle); return static_cast<int>(values.size()); } catch (...) { return 0; }
}
void APIENTRY MtSrvTradesAdd(TradeRecord* trade,const UserInfo*,const ConSymbol*) { Observe(trade,nullptr,true); }
void APIENTRY MtSrvTradesAddExt(TradeRecord* trade,const UserInfo*,const ConSymbol*,const int mode) {
    Observe(trade,nullptr,mode==OPEN_NEW || mode==OPEN_API);
}
void APIENTRY MtSrvTradesAddRequest(TradeRecord* trade,const RequestInfo* request,const UserInfo*,const ConSymbol*,const int mode) {
    Observe(trade,request,mode==OPEN_NEW || mode==OPEN_API);
}
void APIENTRY MtSrvTradesUpdate(TradeRecord* trade,UserInfo*,const int mode) { Observe(trade,nullptr,mode==UPDATE_ACTIVATE); }
void APIENTRY MtSrvTradesCloseBy(TradeRecord* first,TradeRecord* second,TradeRecord* remainder,ConSymbol*,UserInfo*) {
    Observe(first,nullptr,false); Observe(second,nullptr,false); Observe(remainder,nullptr,false);
}
void APIENTRY MtSrvTradeRequestApply(RequestInfo* request,const int isdemo) { ObserveRequest(request,isdemo,false); }
void APIENTRY MtSrvTradeRequestRestore(RequestInfo* request) { ObserveRequest(request,-1,true); }

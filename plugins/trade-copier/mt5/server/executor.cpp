// MT5 Server API 6182 executable copier. Separate from the read-only probe.
#if !defined(_WIN32) || !defined(_MSC_VER) || !defined(_M_X64)
#error TradeCopierExecutor requires MSVC targeting Windows x64.
#endif
#include "mt5_port.hpp"
#include "event_queue.hpp"
#include "worker_wake.hpp"
#include "performance_log.hpp"
#include "tradecopier/journal.hpp"
#include "local_pipe.hpp"
#include <bcrypt.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <set>

using namespace tradecopier;
using namespace tradecopier::execution;
static_assert(MTServerAPIVersion==6182);
static_assert(sizeof(void*)==8);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

namespace {
constexpr std::uint32_t kVersion=125;
struct WatchedAccounts { std::set<Id> sources, activity; };
struct Event {
    Id deal=0,login=0,order=0,position=0;
    Units volume=0;
    std::int64_t time_ms=0;
    std::uint32_t action=0;
    bool owned=false,opening=false;
    wchar_t symbol[128]{};
};
template<std::size_t N,std::size_t M> void Literal(wchar_t (&to)[N],const wchar_t (&from)[M]) {
    static_assert(M<=N); std::memcpy(to,from,sizeof(from));
}
void LogRaw(IMTServerAPI* api,const wchar_t* message) noexcept {
    if (api) try { (void)api->LoggerOut(MTLogErr,L"TradeCopierExecutor: %s",message); } catch (...) {}
}
Id Unsigned(const std::wstring& text) {
    if (text.empty() || text.size()>20) throw std::runtime_error("invalid numeric plugin parameter");
    Id value=0;
    for (wchar_t c:text) {
        if (c<L'0' || c>L'9' || value>(std::numeric_limits<Id>::max()-static_cast<Id>(c-L'0'))/10)
            throw std::runtime_error("invalid numeric plugin parameter");
        value=value*10+static_cast<Id>(c-L'0');
    }
    return value;
}
struct Parameters {
    bool armed=false, management=false;
    Id expected_server=0, dealer_login=0;
    std::uint16_t app=3101;
    std::filesystem::path config,journal;
    std::string symbol="*";
    SymbolMask symbol_mask{"*"};
};
Parameters ReadParameters(IMTServerAPI* api) {
    auto config=Own(api->PluginCreate()); auto param=Own(api->PluginParamCreate());
    Check(api->PluginCurrent(config.get()),"PluginCurrent");
    const auto read=[&](const wchar_t* name,std::uint32_t type,const wchar_t* fallback) {
        const auto ret=config->ParameterGet(name,param.get());
        if (ret==MT_RET_ERR_NOTFOUND) return std::wstring(fallback);
        Check(ret,"plugin parameter");
        if (param->Type()!=type || !param->Value()) throw std::runtime_error("invalid plugin parameter type");
        return std::wstring(param->Value());
    };
    Parameters p;
    const auto armed=Unsigned(read(L"Armed",IMTConParam::TYPE_INT,L"0"));
    if (armed>1) throw std::runtime_error("Armed must be 0 or 1");
    p.armed=armed==1;
    const auto management=Unsigned(read(L"ManagementEnabled",IMTConParam::TYPE_INT,L"0"));
    if (management>1) throw std::runtime_error("ManagementEnabled must be 0 or 1");
    p.management=management==1;
    p.dealer_login=Unsigned(read(L"DealerLogin",IMTConParam::TYPE_STRING,L"0"));
    if (!p.armed) return p;
    p.expected_server=Unsigned(read(L"ExpectedServerId",IMTConParam::TYPE_STRING,L"0"));
    const auto app=Unsigned(read(L"ApiAppId",IMTConParam::TYPE_INT,L"3101"));
    if (!app || app>4095 || !p.expected_server) throw std::runtime_error("invalid server/API application id");
    p.app=static_cast<std::uint16_t>(app);
    p.config=read(L"ConfigPath",IMTConParam::TYPE_STRING,L"");
    p.journal=read(L"JournalPath",IMTConParam::TYPE_STRING,L"");
    if (!p.config.is_absolute() || !p.journal.is_absolute() ||
        p.config.lexically_normal()==p.journal.lexically_normal())
        throw std::runtime_error("ConfigPath and JournalPath must be different absolute paths");
    const auto journal_drive=p.journal.root_name().native();
    if (journal_drive.size()!=2 || journal_drive[1]!=L':' ||
        GetDriveTypeW(p.journal.root_path().c_str())!=DRIVE_FIXED)
        throw std::runtime_error("JournalPath must be on a local fixed drive");
    const auto symbol=read(L"AllowedSymbol",IMTConParam::TYPE_STRING,L"*");
    p.symbol=Utf8(symbol.c_str());
    p.symbol_mask=SymbolMask(p.symbol);
    return p;
}
CopierConfig ReadConfig(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path)>kMaxConfigBytes) throw std::runtime_error("config exceeds size limit");
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read ConfigPath");
    const std::string text((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    if (stream.bad()) throw std::runtime_error("config read failed");
    CopierConfig config; std::string error;
    if (!ParseConfigJson(text,config,error) || !config.enabled) throw std::runtime_error("config not enabled/valid: "+error);
    return config;
}

class Executor final : public IMTServerPlugin,public IMTTradeSink {
public:
    explicit Executor(std::uint32_t runtime):runtime_(runtime) {}
    void Release() noexcept override {
        try { std::lock_guard<std::mutex> lock(lifecycle_); Close(); }
        catch (...) { return; }
        delete this;
    }
    MTAPIRES Start(IMTServerAPI* api) noexcept override {
        try {
            std::lock_guard<std::mutex> lock(lifecycle_);
            return StartLocked(api);
        } catch (...) { return MT_RET_ERROR; }
    }
    MTAPIRES StartLocked(IMTServerAPI* api) {
        if (!api) return MT_RET_ERR_PARAMS;
        if (api_) return MT_RET_ERR_DUPLICATE;
        bool subscribed=false;
        try {
            MTServerInfo info{}; Check(api->About(info),"About");
            api->LoggerOut(MTLogOK,L"TradeCopierExecutor: server-id=%I64u server-build=%u API=%u compiled-api=%u",
                            info.server_id,info.server_build,runtime_,MTServerAPIVersion);
            parameters_=ReadParameters(api);
            api_=api;
            if (!parameters_.armed) {
                api->LoggerOut(MTLogOK,L"TradeCopierExecutor: DISABLED Armed=0; no subscription or trade requests");
                return MT_RET_OK;
            }
            if (runtime_!=MTServerAPIVersion || info.server_build!=6182 ||
                info.server_id!=parameters_.expected_server ||
                (info.server_type!=IMTConServer::NET_MAIN_TRADE_SERVER && info.server_type!=IMTConServer::NET_TRADE_SERVER))
                throw std::runtime_error("server identity/build/type mismatch");
            // Presence of this named kernel object is the lease. It has no
            // thread ownership, so Start and Stop may run on different threads.
            const auto lease_name=L"Global\\TradeCopierExecutor.server."+std::to_wstring(info.server_id);
            lease_=CreateMutexW(nullptr,FALSE,lease_name.c_str());
            const auto lease_error=GetLastError();
            if (!lease_ || lease_error==ERROR_ALREADY_EXISTS)
                throw std::runtime_error("another executor instance holds the server lease");
            queue_=std::make_unique<EventQueue<Event,4096>>();
            journal_=std::make_unique<FileJournal>(parameters_.journal);
            rotations_logged_=0;
            api_gate_.Open();
            port_=std::make_unique<Mt5Port>(api,api_gate_,callback_fault_,info.server_id,parameters_.app,parameters_.dealer_login);
            const auto identity="server="+std::to_string(info.server_id)+";api=6182;app="+
                std::to_string(parameters_.app);
            engine_=std::make_unique<Engine>([this] { return ReadConfig(parameters_.config); },identity,*port_,*journal_,parameters_.management);
            std::string config_error;
            if (!ParseConfigJson(engine_->state().config_json,config_,config_error)) throw std::runtime_error(config_error);
            SetWatch(config_);
            api->LoggerOut(MTLogOK,L"TradeCopierExecutor: BUILD version=0.7.4 api=6182; batched expiry cleanup; journal delta optimization; prune and byte metrics (0.7.4)");
            if (parameters_.management) {
                unsigned char nonce[16]{};
                if (BCryptGenRandom(nullptr,nonce,sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
                    throw std::runtime_error("management session generation failed");
                const char* hex="0123456789abcdef";
                session_.clear(); for (auto c:nonce) { session_+=hex[c>>4]; session_+=hex[c&15]; }
                management_=std::make_unique<management::PipeServer>(info.server_id,parameters_.app);
                api->LoggerOut(MTLogOK,L"TradeCopierExecutor: MANAGEMENT v2 local pipe ready; journal configuration authoritative; account pipeline ready, max-inflight=512; per-account=8; pretrade no-money refusal callback enabled; settled no-money openings retired; runtime faults isolate individual mappings; no automatic global halt; live subscriptions join new positions and drain their own exits; legacy unsubmitted review cleanup enabled; incremental journal frame=4; live checkpoint=64MiB; max-managed-mappings=10000; opening capacity guards enabled; frozen opening rules supported; minimum opening skips retired; source close callbacks and sibling priority enabled; subscription queue watermarks rebased on restart; per-relationship forward/reverse directions frozen in journal schema=7; readonly follower dealer execution supported; dealer group masks enabled; indexed history; single-pass confirmation reuse; journal phase metrics; target deal timestamps; batched expiry cleanup; journal delta optimization; prune and byte metrics (0.7.4)");
            }
            stopping_.store(false,std::memory_order_release);
            callback_fault_.store(false,std::memory_order_release);
            callbacks_.Open();
            // Mark before the call, so an exception can also roll back a
            // subscription that the host may already have accepted.
            subscribed=true;
            Check(api->TradeSubscribe(this),"TradeSubscribe");
            worker_=std::thread([this] { Run(); });
            return MT_RET_OK;
        } catch (const std::exception& error) {
            try { LogRaw(api,Wide(error.what()).c_str()); } catch (...) { LogRaw(api,L"Start failed"); }
        } catch (...) { LogRaw(api,L"Start failed with an exception"); }
        callbacks_.Close();
        if (subscribed) try { (void)api->TradeUnsubscribe(this); } catch (...) {}
        try { Close(); } catch (...) {}
        return MT_RET_ERROR;
    }
    MTAPIRES Stop() noexcept override {
        try {
            std::lock_guard<std::mutex> lock(lifecycle_);
            // No LoggerOut/TradeUnsubscribe/API query here: the SDK has already
            // removed subscriptions and forbids new server access during Stop.
            Close(); return MT_RET_OK;
        } catch (...) { return MT_RET_ERROR; }
    }
    void OnTradeRequestProcess(const IMTRequest* request,const IMTConfirm*,const IMTConGroup*,
        const IMTConSymbol*,const IMTPosition*,const IMTOrder*,const IMTDeal* deal) noexcept override {
        ObserveRefusal(request);
        Observe(deal);
    }
    void OnTradeRequestRefuse(const IMTRequest* request) noexcept override { ObserveRefusal(request); }
    void OnTradeExecution(const IMTConGateway*,const IMTExecution*,const IMTConGroup*,
        const IMTConSymbol*,const IMTPosition*,const IMTOrder*,const IMTDeal* deal) noexcept override {
        Observe(deal);
    }
private:
    ~Executor() noexcept = default;
    void ObserveRefusal(const IMTRequest* request) noexcept {
        if (!callbacks_.Enter()) return;
        try {
            if (request && request->ResultRetcode()==MT_RET_REQUEST_NO_MONEY &&
                ApiValue(request,parameters_.app,1)==kCopierMarker &&
                ApiValue(request,parameters_.app,3)==parameters_.expected_server) {
                RequestRefusal event;
                event.intent=ApiValue(request,parameters_.app,2); event.request=request->ID();
                event.login=request->Login(); event.source_login=request->SourceLogin(); event.position=request->Position();
                event.requested=request->VolumeExt(); event.action=request->Action();
                event.type=request->Type(); event.flags=request->Flags(); event.retcode=request->ResultRetcode();
                event.result_order=request->ResultOrder(); event.result_deal=request->ResultDeal();
                event.result_volume=request->ResultVolumeExt();
                const auto* symbol=request->Symbol(); const auto length=symbol?wcsnlen_s(symbol,128):128;
                if (!event.intent || !event.request || length>=128) throw std::runtime_error("invalid refusal callback identity");
                std::memcpy(event.symbol.data(),symbol,(length+1)*sizeof(wchar_t));
                if (!port_->RecordRefusal(event)) callback_fault_.store(true,std::memory_order_release);
                wake_.Signal();
            }
        } catch (...) { callback_fault_.store(true,std::memory_order_release); wake_.Signal(); }
        callbacks_.Leave();
    }
    void Observe(const IMTDeal* deal) noexcept {
        if (!callbacks_.Enter()) return;
        try {
            // Borrowed SDK objects never leave this callback. Account/history
            // queries and journaling belong exclusively to the worker.
            const auto watched=std::atomic_load(&watched_);
            if (!deal || !watched || !watched->activity.count(deal->Login())) {
                callbacks_.Leave(); return;
            }
            if (watched->sources.count(deal->Login()) &&
                (deal->Action()==IMTDeal::DEAL_BUY || deal->Action()==IMTDeal::DEAL_SELL) &&
                (deal->Entry()==IMTDeal::ENTRY_IN || deal->Entry()==IMTDeal::ENTRY_OUT ||
                 deal->Entry()==IMTDeal::ENTRY_OUT_BY || deal->Entry()==IMTDeal::ENTRY_INOUT)) {
                Event event;
                event.deal=deal->Deal(); event.login=deal->Login(); event.order=deal->Order();
                event.position=deal->PositionID(); event.volume=deal->VolumeExt();
                event.time_ms=deal->TimeMsc(); event.action=deal->Action();
                event.opening=deal->Entry()==IMTDeal::ENTRY_IN;
                event.owned=ApiValue(deal,parameters_.app,1)==kCopierMarker;
                const auto* symbol=deal->Symbol();
                const auto length=symbol?wcsnlen_s(symbol,128):128;
                if (length>=128) throw std::runtime_error("callback symbol too long");
                std::memcpy(event.symbol,symbol,(length+1)*sizeof(wchar_t));
                if (!queue_->Push(event)) callback_fault_.store(true,std::memory_order_release);
            }
            wake_.Signal();
        } catch (...) { callback_fault_.store(true,std::memory_order_release); wake_.Signal(); }
        callbacks_.Leave();
    }
    void SetWatch(const CopierConfig& config) {
        auto accounts=std::make_shared<WatchedAccounts>();
        for (const auto& rule:config.relationships) if (rule.enabled) {
            accounts->sources.insert(rule.source_login);
            accounts->activity.insert(rule.source_login); accounts->activity.insert(rule.follower_login);
        }
        // Retired subscriptions still need source-close and target-fill wakeups.
        for (const auto& link:engine_->state().links) if (link.opening_rule &&
            link.phase!=Phase::Closed && link.phase!=Phase::ManualClosed && link.phase!=Phase::Skipped) {
            accounts->sources.insert(link.source.login);
            accounts->activity.insert(link.source.login);
            accounts->activity.insert(link.opening_rule->follower_login);
        }
        std::shared_ptr<const WatchedAccounts> frozen=accounts;
        std::atomic_store(&watched_,std::move(frozen));
    }
    BootSnapshot Baseline(const CopierConfig& config) {
        (void)config;
        BootSnapshot boot; boot.time_ms=port_->NowMs();
        boot.sequence=queue_->Watermark();
        return boot;
    }
    void HandleManagement() {
        if (!management_) return;
        const auto message=management_->Poll();
        if (!message) return;
        bool ok=true; std::string result;
        try {
            const auto request=management::DecodeRequest(*message);
            if (request.operation!=management::Operation::Status && request.session!=session_)
                throw std::runtime_error("执行器已重启，请刷新连接后重新操作");
            switch (request.operation) {
            case management::Operation::Status:
                if (engine_->state().openings_paused && !engine_->state().capacity_pause.empty())
                    result="新仓因容量自动暂停："+engine_->state().capacity_pause+"；已有仓位继续管理";
                break;
            case management::Operation::Pause:
                engine_->PauseOpenings(request.revision); result="已暂停新仓跟随；已有仓位继续管理"; break;
            case management::Operation::Resume:
                engine_->ResumeOpenings(request.revision,Baseline(config_));
                result="已恢复新仓跟随；暂停期间的源新仓不会补跟"; break;
            case management::Operation::Apply:
                throw std::runtime_error("不再支持旧版暂停式配置提交，请使用 0.6.0 或更新版管理界面的应用配置");
            case management::Operation::UpdateSubscriptions: {
                CopierConfig candidate; std::string error;
                if (!ParseConfigJson(request.config_json,candidate,error)) throw std::runtime_error(error);
                SetWatch(candidate);
                try { engine_->UpdateSubscriptions(request.expected_config,candidate,Baseline(candidate)); }
                catch (...) { SetWatch(config_); throw; }
                config_=std::move(candidate);
                result="订阅配置已生效。新增关系只跟之后的新仓；退出关系正在关闭其已有跟单仓位。其他订阅继续运行。待核对请求不会盲目重发，请查看退出中映射的原因。";
                break;
            }
            case management::Operation::Inspect:
                result=engine_->InspectIntent(request.intent); break;
            }
        } catch (const Stopping&) { throw; }
        catch (const std::exception& error) { ok=false; result=error.what(); }
        auto response=management::Snapshot(*engine_);
        response.ok=ok; response.message=result; response.session=session_;
        response.identity=StableExecutorIdentity(engine_->state().identity)+";source-mask="+parameters_.symbol;
        response.config_path=Utf8(parameters_.config.c_str());
        response.journal_path=Utf8(parameters_.journal.c_str());
        management_->Reply(management::EncodeResponse(response));
    }
    void FailWorker(const std::string& reason) noexcept {
        try { engine_->ReportFault(reason); } catch (...) {}
    }
    void Run() noexcept {
        bool started=false;
        const auto monotonic_ms=[] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        };
        PerformanceLog performance_log;
        performance_log.Reset(monotonic_ms());
        while (!stopping_.load(std::memory_order_acquire)) {
            const auto wake_token=wake_.Snapshot();
            try {
                if (!started) { engine_->Start(Baseline(config_)); started=true; }
                const auto confirmation_token=wake_.Snapshot();
                engine_->ConfirmPending(); // Never queue admissions ahead of known fills.
                const auto confirmed_at=std::chrono::steady_clock::now();
                {
                    if (callback_fault_.exchange(false,std::memory_order_acq_rel))
                        engine_->ReportFault("callback_queue_full_or_invalid_callback");
                    Event event; Id sequence=0;
                    std::vector<std::pair<Deal,Id>> admissions;
                    for (unsigned n=0;n<Engine::kAdmissionBatch && queue_->Pop(event,sequence);++n) {
                        if (!event.opening) {
                            engine_->NotifySourceClose(event.login,event.position);
                            continue;
                        }
                        Deal deal;
                        deal.id=event.deal; deal.login=event.login; deal.order=event.order;
                        deal.position=event.position; deal.units=event.volume; deal.time_ms=event.time_ms;
                        deal.symbol=Utf8(event.symbol); deal.side=event.action==IMTDeal::DEAL_BUY?Side::Buy:Side::Sell;
                        deal.opening=true; deal.copier_owned=event.owned;
                        if (parameters_.symbol_mask.Matches(deal.symbol)) admissions.emplace_back(std::move(deal),sequence);
                    }
                    engine_->AdmitBatch(admissions,false);

                }
                // Reuse only this worker pass. A callback during confirmation or
                // admission requests fresh evidence; a callback after Snapshot
                // still wakes the next loop through wake_token below.
                const bool reuse_confirmation=wake_.Snapshot()==confirmation_token &&
                    std::chrono::steady_clock::now()-confirmed_at<std::chrono::milliseconds(10);
                engine_->Tick(reuse_confirmation);
                if (journal_->rotations()!=rotations_logged_) {
                    port_->Log("JOURNAL live checkpoint replaced; checkpoints_this_run="+
                        std::to_string(journal_->rotations())+" active_bytes="+std::to_string(journal_->bytes()));
                    rotations_logged_=journal_->rotations();
                }
            } catch (const Stopping&) { break; }
            catch (const std::exception& error) { FailWorker(std::string("worker_exception: ")+error.what()); }
            catch (...) { FailWorker("worker_exception_unknown"); }
            // Per-request faults never close the callback gate.
            try { HandleManagement(); }
            catch (const Stopping&) { break; }
            catch (...) { management_.reset(); } // channel failure does not interrupt existing copying
            try {
                for (const auto& line:performance_log.Poll(monotonic_ms(),engine_->performance(),journal_->performance(),
                        port_->HistoryStats(),engine_->state().links.size(),engine_->state().intents.size(),journal_->bytes(),
                        journal_->written_bytes(),journal_->logical_bytes()))
                    port_->Log(line);
            } catch (const Stopping&) { break; }
            catch (...) { /* Diagnostics cannot halt or change trade management. */ }
            wake_.WaitSince(wake_token,std::chrono::milliseconds(10),stopping_);
        }
        callbacks_.Close();
    }
    void Close() {
        api_gate_.Close();
        callbacks_.Close();
        stopping_.store(true,std::memory_order_release); wake_.Signal();
        if (worker_.joinable()) worker_.join();
        callbacks_.Drain(); api_gate_.Drain();
        management_.reset();
        engine_.reset(); port_.reset(); journal_.reset(); queue_.reset();
        if (lease_) { CloseHandle(lease_); lease_=nullptr; }
        api_=nullptr;
    }
    std::uint32_t runtime_;
    std::uint64_t rotations_logged_=0;
    std::mutex lifecycle_;
    WorkerWake wake_;
    std::thread worker_;
    LifetimeGate callbacks_,api_gate_;
    std::atomic<bool> stopping_{true},callback_fault_{false};
    IMTServerAPI* api_=nullptr;
    HANDLE lease_=nullptr;
    Parameters parameters_;
    CopierConfig config_;
    std::shared_ptr<const WatchedAccounts> watched_;
    std::string session_;
    std::unique_ptr<management::PipeServer> management_;
    std::unique_ptr<EventQueue<Event,4096>> queue_;
    std::unique_ptr<FileJournal> journal_;
    std::unique_ptr<Mt5Port> port_;
    std::unique_ptr<Engine> engine_;
};
}

MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info) {
    try {
        std::memset(&info,0,sizeof(info));
        info.version=kVersion; info.version_api=MTServerAPIVersion;
        Literal(info.name,L"TradeCopierExecutor"); Literal(info.copyright,L"Trade Copier project");
        Literal(info.description,L"Executes ordinary market copies when Armed=1. Server API 6182 validation stage.");
        info.defaults[0].type=MTPluginParam::TYPE_INT;
        Literal(info.defaults[0].name,L"Armed"); Literal(info.defaults[0].value,L"0");
        info.defaults[1].type=MTPluginParam::TYPE_STRING;
        Literal(info.defaults[1].name,L"ExpectedServerId"); Literal(info.defaults[1].value,L"0");
        info.defaults[2].type=MTPluginParam::TYPE_INT;
        Literal(info.defaults[2].name,L"ApiAppId"); Literal(info.defaults[2].value,L"3101");
        info.defaults[3].type=MTPluginParam::TYPE_STRING;
        Literal(info.defaults[3].name,L"ConfigPath"); Literal(info.defaults[3].value,L"");
        info.defaults[4].type=MTPluginParam::TYPE_STRING;
        Literal(info.defaults[4].name,L"JournalPath"); Literal(info.defaults[4].value,L"");
        info.defaults[5].type=MTPluginParam::TYPE_STRING;
        Literal(info.defaults[5].name,L"AllowedSymbol"); Literal(info.defaults[5].value,L"*");
        info.defaults[6].type=MTPluginParam::TYPE_INT;
        Literal(info.defaults[6].name,L"ManagementEnabled"); Literal(info.defaults[6].value,L"0");
        info.defaults[7].type=MTPluginParam::TYPE_STRING;
        Literal(info.defaults[7].name,L"DealerLogin"); Literal(info.defaults[7].value,L"0");
        info.defaults_total=8;
        return MT_RET_OK;
    } catch (...) { return MT_RET_ERROR; }
}
MTAPIENTRY MTAPIRES MTServerCreate(const std::uint32_t apiversion,IMTServerPlugin** plugin) {
    if (!plugin) return MT_RET_ERR_PARAMS;
    *plugin=nullptr;
    try {
        if (apiversion!=MTServerAPIVersion) return MT_RET_ERR_NOTSUPPORTED;
        *plugin=new(std::nothrow) Executor(apiversion);
        return *plugin?MT_RET_OK:MT_RET_ERR_MEM;
    } catch (...) { return MT_RET_ERROR; }
}

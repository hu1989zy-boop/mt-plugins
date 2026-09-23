#include "management_requests.hpp"
#include <iostream>
#include <stdexcept>

using namespace tradecopier;
using namespace tradecopier::manager;
using management::Operation;
using Accepted=Requests::Accepted;

namespace {
unsigned checks=0;
void Check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
Action Make(Operation op=Operation::Status, bool background=false, bool load=false) {
    Action action; action.server=1; action.app=3101;
    action.request.operation=op; action.request.session="process-A"; action.request.revision=25;
    action.background=background; action.load_config=load; return action;
}
Connection Live() { return {true,1,3101,"process-A",25}; }

void ClickDuringRefresh() {
    for (auto op:{Operation::Pause,Operation::Resume,Operation::Apply,Operation::UpdateSubscriptions,Operation::Inspect,Operation::Status}) {
        Requests requests;
        Check(requests.Schedule(Make(Operation::Status,true))==Accepted::Start,"start background poll");
        Check(!requests.busy() && !requests.loading_config(),"poll must leave commands and draft editable");
        Check(requests.Schedule(Make(Operation::Status,true))==Accepted::Busy,"never overlap polls");
        auto clicked=Make(op,false,op==Operation::Status);
        clicked.request.intent=2; clicked.request.config_json="reviewed configuration";
        Check(requests.Schedule(clicked)==Accepted::Deferred,"retain click during poll");
        Check(requests.busy(),"lock commands only after accepting a click");
        Check(requests.loading_config()==(op==Operation::Status),"protect draft while read-active waits");
        Check(requests.Schedule(Make(Operation::Apply))==Accepted::Busy,"ignore double click without replacing queued action");
        // Simulate selection/draft changes while the response refreshes the UI.
        clicked.request.intent=99; clicked.request.config_json="later draft";
        const auto next=requests.Complete();
        Check(next && next->request.operation==op && next->request.intent==2,"keep clicked request ID");
        Check(next->request.config_json=="reviewed configuration","keep confirmed configuration payload");
        Check(DispatchError(*next,Live()).empty(),"dispatch unchanged session and revision");
        Check(requests.Schedule(*next)==Accepted::Start,"send retained action once after poll");
        Check(requests.busy(),"foreground action is busy");
        Check(requests.Schedule(*next)==Accepted::Busy,"never overlap or duplicate foreground action");
        Check(!requests.Complete(),"action is removed after one completion");
        Check(!requests.active() && !requests.busy() && !requests.loading_config(),"unlock after completion");
    }
}

void ChangedStateAndFailures() {
    for (auto op:{Operation::Pause,Operation::Resume,Operation::Apply,Operation::UpdateSubscriptions,Operation::Inspect}) {
        Requests requests; requests.Schedule(Make(Operation::Status,true));
        auto clicked=Make(op); clicked.request.intent=2;
        requests.Schedule(clicked);
        const auto next=requests.Complete();
        auto live=Live(); live.fresh=false;
        Check(!DispatchError(*next,live).empty(),"failed/expired refresh must not dispatch waiting command");
        Check(!requests.active() && !requests.Complete(),"failed deferred operation must not be retried");
        live=Live(); live.server=2;
        Check(!DispatchError(*next,live).empty(),"never dispatch against another server");
        live=Live(); live.app=3102;
        Check(!DispatchError(*next,live).empty(),"never dispatch against another app");
        live=Live(); live.session="process-B";
        Check(!DispatchError(*next,live).empty(),"restart must invalidate even read-only intent IDs");
        live=Live(); ++live.revision;
        Check(DispatchError(*next,live).empty()==(op==Operation::Inspect || op==Operation::UpdateSubscriptions),"never silently rebase mutations; inspect keeps immutable intent ID");
        Check(next->request.revision==25 && next->request.session=="process-A","fresh poll must not rewrite authorization context");
    }
    auto load=Make(Operation::Status,false,true);
    auto offline=Live(); offline.fresh=false;
    Check(!DispatchError(load,offline).empty(),"read-active requires a current connection");
    auto connect=Make(); connect.server=2;
    Check(DispatchError(connect,offline).empty(),"explicit connect may establish a new connection");
    Check(!SameTarget(connect,1,3101) && SameTarget(connect,2,3101),"discard a response after endpoint edit");
    Requests requests;
    Check(requests.Schedule(connect)==Accepted::Start && requests.busy(),"explicit connect is foreground work");
    Check(requests.Schedule(load)==Accepted::Busy,"do not queue behind foreground connection attempt");
    Check(!requests.Complete() && !requests.busy(),"connection failure has no automatic retry");
    bool rejected=false;
    try { requests.Schedule(Make(Operation::Pause,true)); }
    catch (const std::logic_error&) { rejected=true; }
    Check(rejected && !requests.active(),"a write can never be disguised as background work");
}
void RuntimeBlockingReasons() {
    management::Response snapshot; snapshot.paused=true; snapshot.review=1360;
    snapshot.apply_block="存在待核对映射，请先核对交易证据";
    const auto hint=RuntimeGuardHint(snapshot,true);
    Check(hint.find(snapshot.apply_block)!=std::string::npos,"paused legacy reviews show the actual blocking reason and count");
    snapshot.message="已读取执行器状态。";
    Check(RuntimeGuardHint(snapshot,true)==hint,"last read result cannot hide the recovery blocker");
    snapshot.review=0; snapshot.apply_block.clear();
    Check(RuntimeGuardHint(snapshot,true).find("订阅仍可")!=std::string::npos,"cleaned snapshot offers normal paused recovery");
    snapshot.halt="restart_unresolved_request";
    Check(RuntimeGuardHint(snapshot,true).find(snapshot.halt)!=std::string::npos,"HALTED reason takes priority over paused state");
    Check(RuntimeGuardHint(snapshot,false).find("状态已过期")!=std::string::npos,"stale snapshot cannot present live recovery instructions");
}
}

int main() {
    try {
        ClickDuringRefresh(); ChangedStateAndFailures(); RuntimeBlockingReasons();
        std::cout << "Management UI scheduling: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

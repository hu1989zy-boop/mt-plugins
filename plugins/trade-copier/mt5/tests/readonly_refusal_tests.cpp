#include "../server/request_refusals.hpp"
#include <iostream>
#include <stdexcept>

using namespace tradecopier;
using namespace tradecopier::execution;
namespace {
unsigned checks=0;
void Check(bool condition,const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void Throws(F&& function,const char* message) {
    bool threw=false;
    try { function(); } catch (const std::runtime_error&) { threw=true; }
    Check(threw,message);
}
Intent Request(Side side,bool close) {
    Intent intent;
    intent.id=7;
    intent.request_id=123;
    intent.follower_login=50002;
    intent.follower_position=close?900:0;
    intent.units=kUnitsPerLot/100;
    intent.side=side;
    intent.close=close;
    intent.symbol="XAUUSDp";
    return intent;
}
RequestRefusal NoMoney(const Intent& intent,std::uint32_t action=3,Id dealer=0) {
    RequestRefusal event;
    event.intent=intent.id;
    event.login=intent.follower_login;
    event.position=intent.follower_position;
    event.requested=intent.units;
    event.request=intent.request_id;
    event.action=action;
    event.source_login=dealer;
    event.type=intent.side==Side::Buy?0:1;
    event.flags=(intent.close?1U:0U)|(action==3?2U:0U);
    event.retcode=kRequestNoMoney;
    const wchar_t symbol[]=L"XAUUSDp";
    std::copy(std::begin(symbol),std::end(symbol),event.symbol.begin());
    return event;
}
void Rejected(const std::optional<Result>& result) {
    Check(result && result->status==Result::Status::Rejected && result->retcode==10019 &&
        !result->position && !result->units && result->deals.empty() &&
        result->reason=="server_pretrade_no_money_zero_fill",
        "matching server refusal preserves exact no-money, zero-fill evidence");
}
void Uncertain(const std::optional<Result>& result) {
    Check(result && result->status==Result::Status::Uncertain &&
        result->reason=="conflicting_or_mismatched_refusal_callback",
        "ambiguous callback never proves zero execution");
}
void Profiles() {
    for (auto action:{3U,200U}) for (auto side:{Side::Buy,Side::Sell}) for (bool close:{false,true}) {
        const Id dealer=action==200?1000:0;
        const auto intent=Request(side,close);
        const auto valid=NoMoney(intent,action,dealer);
        RequestRefusals inbox;
        if (action==3) inbox.Register(intent.id); // Existing market API remains source compatible.
        else inbox.Register(intent.id,action,dealer);
        Check(!inbox.Read(intent,L"XAUUSDp"),"registration alone does not fabricate a refusal");
        Check(inbox.Push(valid) && inbox.Push(valid),"identical callbacks may arrive more than once");
        Rejected(inbox.Read(intent,L"XAUUSDp"));
        Rejected(inbox.Read(intent,L"XAUUSDp"));
        auto changed=valid;
        ++changed.source_login;
        Check(!(valid==changed),"callback equality includes the originating dealer login");
        Check(inbox.Push(changed),"conflicting dealer evidence is queued");
        Uncertain(inbox.Read(intent,L"XAUUSDp"));
        Check(inbox.Push(valid),"original callback may arrive after conflict");
        Uncertain(inbox.Read(intent,L"XAUUSDp"));

        // Correlation is exact for both dealer and ordinary market requests.
        for (unsigned mismatch=0;mismatch<17;++mismatch) {
            if (mismatch==13 && action==3) continue; // Market callbacks preserve legacy SourceLogin handling.
            RequestRefusals isolated;
            isolated.Register(intent.id,action,dealer);
            auto event=valid;
            switch (mismatch) {
            case 0: ++event.request; break;
            case 1: ++event.login; break;
            case 2: ++event.position; break;
            case 3: ++event.requested; break;
            case 4: event.type=side==Side::Buy?1:0; break;
            case 5: event.action=action==3?200:3; break;
            case 6: event.flags^=1; break; // Closing flag.
            case 7: event.flags^=2; break; // TA_MARKET-only flag is invalid for dealer requests.
            case 8: event.flags|=kRequestSkipMarginCheck; break;
            case 9: event.retcode=10012; break; // Timeout is not definite zero fill.
            case 10: event.result_deal=88; break;
            case 11: event.result_order=89; break;
            case 12: event.result_volume=1; break;
            case 13: ++event.source_login; break;
            case 14: event.symbol[0]=L'E'; break;
            case 15: event.symbol.fill(L'x'); break; // Unterminated callback buffer.
            case 16: event.type=2; break; // Pending order is not a copier market request.
            }
            Check(isolated.Push(event),"mismatched callback accepted into bounded queue");
            Uncertain(isolated.Read(intent,L"XAUUSDp"));
        }
        RequestRefusals skip_conflict;
        skip_conflict.Register(intent.id,action,dealer);
        auto skipped=valid;
        skipped.flags|=kRequestSkipMarginCheck;
        Check(!(valid==skipped),"callback equality includes skipped-margin flag");
        Check(skip_conflict.Push(skipped) && skip_conflict.Push(valid),"conflicting skip-margin callbacks queued");
        Uncertain(skip_conflict.Read(intent,L"XAUUSDp"));

        RequestRefusals no_identity;
        no_identity.Register(intent.id,action,dealer);
        Check(no_identity.Push(valid),"callback can precede submission return");
        auto pending=intent;
        pending.request_id=0;
        Uncertain(no_identity.Read(pending,L"XAUUSDp"));
        Rejected(no_identity.Read(intent,L"XAUUSDp"));
    }
}
void RegistrationAndLifetime() {
    RequestRefusals inbox;
    Throws([&]{inbox.Register(0);},"zero intent is not registrable");
    Throws([&]{inbox.Register(1,0,0);},"unknown action is not registrable");
    Throws([&]{inbox.Register(1,3,1000);},"market registration cannot claim a dealer profile");
    Throws([&]{inbox.Register(1,200,0);},"dealer registration requires a dealer login");
    Check(!inbox.size(),"invalid profiles do not consume a registration");
    auto intent=Request(Side::Buy,false);
    auto event=NoMoney(intent,200,1000);
    inbox.Register(intent.id,200,1000);
    Throws([&]{inbox.Register(intent.id,3,0);},"duplicate registration cannot replace the expected profile");
    Check(inbox.Push(event),"original registered profile still receives its callback");
    Rejected(inbox.Read(intent,L"XAUUSDp"));
    inbox.Forget(intent.id);
    Check(inbox.Push(event),"late callback may be queued after durable settlement");
    Check(!inbox.Read(intent,L"XAUUSDp") && !inbox.size(),"late callback cannot resurrect a settled registration");
    auto foreign=event;
    foreign.intent=100000;
    Check(inbox.Push(foreign),"foreign callback may be queued");
    Check(!inbox.Read(intent,L"XAUUSDp") && !inbox.size(),"foreign callback cannot grow the evidence cache");
    for (Id id=100;id<700;++id) {
        intent.id=id;
        inbox.Register(id,200,1000);
        Check(inbox.Push(NoMoney(intent,200,1000)),"new dealer request queues its own evidence");
        Rejected(inbox.Read(intent,L"XAUUSDp"));
        inbox.Forget(id);
    }
    Check(!inbox.size(),"evidence lifetime follows in-flight requests rather than historical volume");
    for (Id id=1;id<=Engine::kMaxInFlight;++id) inbox.Register(id,200,1000);
    Throws([&]{inbox.Register(Engine::kMaxInFlight+1,200,1000);},"global registration capacity is enforced");
    Check(inbox.size()==Engine::kMaxInFlight,"capacity refusal does not change existing registrations");
    inbox.Forget(1);
    inbox.Register(Engine::kMaxInFlight+1,200,1000);
    Check(inbox.size()==Engine::kMaxInFlight,"settlement reclaims one registration slot");
    RequestRefusals full;
    for (unsigned n=0;n<4096;++n) Check(full.Push(event),"callback queue admits its bounded capacity");
    Check(!full.Push(event),"callback overflow is surfaced rather than silently replacing evidence");
}
}
int main() {
    try {
        Profiles();
        RegistrationAndLifetime();
        std::cout<<"MT5 market/dealer refusal checks: "<<checks<<" passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAILED: "<<error.what()<<'\n';
        return 1;
    }
}

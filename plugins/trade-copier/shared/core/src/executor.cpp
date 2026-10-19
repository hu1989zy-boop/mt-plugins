#include "tradecopier/executor.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace tradecopier::execution {
namespace {
using Clock=std::chrono::steady_clock;
std::int64_t Milliseconds(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to-from).count();
}
struct PerformanceTimer {
    std::uint64_t& total;
    std::uint64_t& peak;
    Clock::time_point start = Clock::now();
    ~PerformanceTimer() {
        const auto elapsed=static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
        total+=elapsed; peak=std::max(peak,elapsed);
    }
};
constexpr std::int64_t kWaitMs = 30000;
constexpr std::int64_t kExpiryPruneBatchMs = 1000;
static_assert(std::string_view(kPlatformName)=="mt4" || std::string_view(kPlatformName)=="mt5",
              "unsupported execution platform");
constexpr std::uint32_t kNoMoneyRetcode = std::string_view(kPlatformName)=="mt4" ? 134 : 10019;
constexpr std::size_t kMaxLinks = Engine::kOpeningLinkLimit, kMaxIntents = 100000;
bool Contains(const std::vector<Id>& ids, Id id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
bool Terminal(Phase phase) {
    return phase==Phase::Closed || phase==Phase::ManualClosed || phase==Phase::Skipped;
}
bool SameRule(const RuleConfig& a, const RuleConfig& b) {
    Rational x, y;
    return a.id==b.id && a.enabled==b.enabled && a.source_login==b.source_login &&
        a.follower_login==b.follower_login && a.mode==b.mode &&
        ParsePositiveDecimal(a.parameter,x) && ParsePositiveDecimal(b.parameter,y) &&
        x.numerator==y.numerator && x.denominator==y.denominator && a.symbol_mask==b.symbol_mask &&
        a.auto_symbol==b.auto_symbol && a.source_suffix==b.source_suffix && a.direction==b.direction;
}
Side Opposite(Side side) { return side == Side::Buy ? Side::Sell : Side::Buy; }
bool Expired(std::int64_t now, std::int64_t since) {
    return now < since || now - since >= kWaitMs;
}
bool ValidSource(const SourceView& v) {
    if (v.state != ReadState::Ready || !v.opening_units || v.first_fill_ms <= 0) return false;
    Units left = v.opening_units;
    std::unordered_set<Id> seen;
    for (const auto& c : v.closes) {
        if (!c.deal || !c.units || c.units > left || c.time_ms < v.first_fill_ms ||
            !seen.insert(c.deal).second) return false;
        left -= c.units;
    }
    return left == v.remaining;
}
std::size_t RetireLegacyUnsubmittedReviews(State& state) {
    // 0.4.x classified these pre-submission skips as Review. A zero target
    // ticket alone is NOT proof: even a Prepared/Unknown request may have
    // reached the server. Require the exact old branch and no intent at all.
    std::vector<bool> has_intent(state.links.size(),false);
    for (const auto& intent:state.intents) has_intent.at(intent.link)=true;
    std::size_t retired=0;
    for (std::size_t n=0;n<state.links.size();++n) {
        auto& link=state.links[n];
        if (link.phase!=Phase::Review || has_intent[n] ||
            (link.reason!="source_opening_predates_activation_or_already_closed" &&
             link.reason!="source_opening_expired") ||
            link.source_initial || link.source_remaining || link.follower_position ||
            link.follower_remaining || !link.processed_closes.empty()) continue;
        link.phase=Phase::Skipped;
        ++retired;
    }
    return retired;
}
std::size_t RetireKnownNoMoneyReviews(State& state) {
    std::vector<unsigned> counts(state.links.size(),0);
    for (const auto& i:state.intents) ++counts.at(i.link);
    std::size_t retired=0;
    for (const auto& i:state.intents) {
        auto& link=state.links.at(i.link);
        if (counts[i.link]!=1 || link.phase!=Phase::Review) continue;
        link.phase=Phase::Skipped;
        if (SettledNoMoneyOpening(link,i)) ++retired;
        else link.phase=Phase::Review;
    }
    return retired;
}
std::size_t RetireMinimumOpeningReviews(State& state) {
    std::vector<bool> has_intent(state.links.size(),false);
    for (const auto& i:state.intents) has_intent.at(i.link)=true;
    std::size_t retired=0;
    for (std::size_t n=0;n<state.links.size();++n) {
        auto& link=state.links[n];
        // Exact pre-Prepare branch only. Never infer absence of a trade from a
        // zero target ticket, nor clear a close-dust/ambiguous execution review.
        if (link.phase!=Phase::Review || link.reason!="skip_below_minimum" || has_intent[n] ||
            !link.opening_rule || link.follower_symbol.empty() || !link.source_initial ||
            link.source_remaining!=link.source_initial || link.follower_position ||
            link.follower_remaining || !link.processed_closes.empty()) continue;
        link.phase=Phase::Skipped; ++retired;
    }
    return retired;
}
}

Side FollowerSide(const Link& link) {
    // Use the admitted opening rule, never a later live configuration edit.
    // Legacy terminal mappings without a binding were forward-only.
    if (!link.opening_rule) return link.source.side;
    if (!ValidDirection(link.opening_rule->direction))
        throw std::runtime_error("invalid mapping copy direction");
    return link.opening_rule->direction==CopyDirection::Reverse ? Opposite(link.source.side) : link.source.side;
}

std::string ActiveRuleChangeReason(const State& state, const CopierConfig& current, const CopierConfig& next) {
    std::unordered_map<Id,const RuleConfig*> existing, proposed;
    for (const auto& rule:current.relationships) existing.emplace(rule.id,&rule);
    for (const auto& rule:next.relationships) proposed.emplace(rule.id,&rule);
    std::unordered_set<Id> checked;
    for (const auto& link:state.links) {
        if (Terminal(link.phase) || !checked.insert(link.rule).second) continue;
        const auto original=existing.find(link.rule), found=proposed.find(link.rule);
        if (original==existing.end() || found==proposed.end() || !SameRule(*original->second,*found->second))
            return "关系 " + std::to_string(link.rule) +
                " 仍有在管仓位，请保留原关系 ID、账号、启用状态和手数设置；新增其他订阅无需平仓";
    }
    return {};
}

bool UnsubmittedSymbolBlock(const State& state,std::size_t index) {
    const auto& link=state.links.at(index);
    return link.phase==Phase::Review && link.reason.rfind("symbol_resolution: ",0)==0 &&
        link.opening_rule && link.opening_rule->auto_symbol && link.follower_symbol.empty() &&
        !link.follower_position && !link.follower_remaining &&
        std::none_of(state.intents.begin(),state.intents.end(),[&](const Intent& i) { return i.link==index; });
}

bool SettledNoMoneyOpening(const Link& link,const Intent& intent) {
    // The platform's no-money result (MT5: 10019; MT4: 134) is accepted only by
    // its own compiled engine. The adapter checks the complete execution
    // identity before emitting server_pretrade_no_money_zero_fill; retain its
    // exact durable reason as well as the result and mapping invariants.
    return link.phase==Phase::Skipped && link.reason=="server_rejected_no_money" &&
        intent.state==IntentState::Rejected && intent.retcode==kNoMoneyRetcode && intent.request_id &&
        !intent.close && intent.units && !intent.filled && !intent.result_position && intent.deals.empty() &&
        !intent.follower_position && !intent.follower_before && intent.source_event==link.source.id &&
        intent.side==FollowerSide(link) && !link.follower_position && !link.follower_remaining &&
        link.processed_closes.empty() && link.source_initial && link.source_remaining==link.source_initial &&
        link.opening_rule && link.opening_rule->follower_login==intent.follower_login &&
        !link.follower_symbol.empty() && link.follower_symbol==intent.symbol;
}

bool SettledUnsubmittedNoMoneyOpening(const Link& link,const Intent& intent) {
    return std::string_view(kPlatformName)=="mt4" && link.phase==Phase::Skipped &&
        link.reason=="mt4_not_submitted_no_money" && intent.state==IntentState::Rejected &&
        intent.retcode==134 && !intent.request_id && !intent.close && intent.units &&
        !intent.filled && !intent.result_position && intent.deals.empty() &&
        !intent.follower_position && !intent.follower_before && intent.source_event==link.source.id &&
        intent.side==FollowerSide(link) && !link.follower_position && !link.follower_remaining &&
        link.processed_closes.empty() && link.source_initial && link.source_remaining==link.source_initial &&
        link.opening_rule && link.opening_rule->follower_login==intent.follower_login &&
        !link.follower_symbol.empty() && link.follower_symbol==intent.symbol;
}

std::vector<Id> ExecutionReviewAccounts(const State& state) {
    std::vector<Id> accounts;
    for (const auto& intent:state.intents) if (intent.state==IntentState::Review)
        accounts.push_back(intent.follower_login);
    std::sort(accounts.begin(),accounts.end());
    accounts.erase(std::unique(accounts.begin(),accounts.end()),accounts.end());
    return accounts;
}

State PlanSubscriptionUpdate(const State& current,const CopierConfig& before,const CopierConfig& after,const BootSnapshot& boot) {
    if (boot.time_ms<=0) throw std::runtime_error("invalid subscription activation barrier");
    State next=current; next.config_json=WriteConfigJson(after); next.subscriptions.clear();
    for (const auto& rule:after.relationships) if (after.enabled && rule.enabled) {
        const auto old=std::find_if(before.relationships.begin(),before.relationships.end(),[&](const RuleConfig& r){
            return before.enabled && r.enabled && r.id==rule.id && r.source_login==rule.source_login && r.follower_login==rule.follower_login;
        });
        const auto barrier=std::find_if(current.subscriptions.begin(),current.subscriptions.end(),[&](const SubscriptionBarrier& b){return b.rule==rule.id;});
        next.subscriptions.push_back(old==before.relationships.end()?SubscriptionBarrier{rule.id,boot.time_ms,boot.sequence}:
            (barrier==current.subscriptions.end()?SubscriptionBarrier{rule.id,current.activation_ms,current.activation_sequence}:*barrier));
    }
    std::vector<bool> has_intent(next.links.size(),false), unresolved(next.links.size(),false);
    for (const auto& i:next.intents) {
        has_intent.at(i.link)=true;
        if (i.state==IntentState::Prepared || i.state==IntentState::Submitted || i.state==IntentState::Review) unresolved.at(i.link)=true;
    }
    for (auto& link:next.links) {
        if (Terminal(link.phase) || !link.opening_rule) continue;
        const auto found=std::find_if(after.relationships.begin(),after.relationships.end(),[&](const RuleConfig& r){
            return after.enabled && r.enabled && r.id==link.rule && r.source_login==link.source.login && r.follower_login==link.opening_rule->follower_login;
        });
        if (found!=after.relationships.end()) continue;
        link.exit_requested=true;
        const auto index=static_cast<std::size_t>(&link-next.links.data());
        if (link.phase==Phase::Candidate || (link.phase==Phase::Review && !has_intent[index] && !link.follower_position)) {
            link.phase=Phase::Skipped; link.reason="subscription_exit_before_submission";
        } else if (link.phase==Phase::Review && link.follower_position && !unresolved[index] && link.reason!="follower_position_already_mapped") {
            link.phase=Phase::Active; link.reason="subscription_exit_recheck_target";
        }
    }
    return next;
}

void ValidateRuleBindings(const State& state) {
    for (const auto& link:state.links) {
        if ((link.phase==Phase::Active || link.phase==Phase::Opening || link.phase==Phase::Closing ||
             link.follower_position || link.follower_remaining) && link.follower_symbol.empty())
            throw std::runtime_error("live mapping has no frozen target symbol");
        if (!link.opening_rule) {
            if (!Terminal(link.phase)) throw std::runtime_error("mapping has no opening rule");
            continue;
        }
        const auto& rule=*link.opening_rule;
        std::string error;
        VolumePolicy policy;
        if (rule.id!=link.rule || rule.source_login!=link.source.login || !rule.enabled ||
            !rule.id || !rule.source_login || !rule.follower_login || rule.source_login==rule.follower_login ||
            !ValidDirection(rule.direction) || !MakePolicy(rule,policy,error) || !ValidateSymbolRule(rule.symbol_mask,rule.source_suffix,error))
            throw std::runtime_error("invalid mapping opening rule: " + error);
    }
    for (const auto& intent:state.intents) {
        if (intent.link>=state.links.size()) throw std::runtime_error("invalid intent link");
        const auto& link=state.links[intent.link];
        if (link.follower_symbol.empty() || intent.symbol!=link.follower_symbol)
            throw std::runtime_error("intent symbol differs from frozen target symbol");
        const auto& rule=link.opening_rule;
        if (rule && intent.follower_login!=rule->follower_login)
            throw std::runtime_error("intent account differs from mapping opening rule");
        if (intent.side!=(intent.close?Opposite(FollowerSide(link)):FollowerSide(link)))
            throw std::runtime_error("intent side differs from mapping opening direction");
    }
}

Engine::Engine(const CopierConfig& config, std::string identity, Port& port, Store& store, bool managed_config)
    : Engine([config] { return config; },std::move(identity),port,store,managed_config) {}

Engine::Engine(const std::function<CopierConfig()>& bootstrap, std::string identity,
               Port& port, Store& store, bool managed_config)
    : port_(port), store_(store) {
    pending_.reserve(kMaxInFlight); // No allocation after a successful SDK submission.
    std::string error;
    restored_ = store_.Load(state_);
    if (restored_ && managed_config) {
        if (!ParseConfigJson(state_.config_json, config_, error))
            throw std::runtime_error("invalid journal configuration: " + error);
    } else config_ = bootstrap();
    if (!ValidateConfig(config_, error) || identity.empty())
        throw std::runtime_error("executor configuration disabled or invalid: " + error);
    const auto canonical = WriteConfigJson(config_);
    for (const auto& rule:config_.relationships) symbol_masks_.emplace(rule.id,SymbolMask(rule.symbol_mask));
    if (restored_) {
        if (StableExecutorIdentity(state_.identity) != StableExecutorIdentity(identity) || state_.config_json != canonical)
            throw std::runtime_error("journal identity/config mismatch; migration required");
        if (state_.links.size() > kMaxLinks || state_.intents.size() > kMaxIntents)
            throw std::runtime_error("journal exceeds executor limits");
        ValidateRuleBindings(state_);
        Id previous=0;
        for (const auto& intent:state_.intents) {
            if (!intent.id || intent.id<=previous || intent.id>=state_.next_intent_id || intent.link>=state_.links.size())
                throw std::runtime_error("invalid intent references");
            previous=intent.id;
        }
    } else {
        state_.identity = std::move(identity);
        state_.config_json = canonical;
    }
}

const RuleConfig& Engine::Rule(const Link& link) const {
    if (!link.opening_rule) throw std::runtime_error("mapping has no opening rule");
    return *link.opening_rule;
}

void Engine::Save() {
    if (storage_failed_) throw std::runtime_error("journal unavailable; new durable writes blocked");
    if (state_.revision == std::numeric_limits<Id>::max())
        throw std::runtime_error("journal revision exhausted");
    ++state_.revision;
    try { store_.Save(state_); dirty_=false; }
    catch (...) { storage_failed_ = true; warning_ = "journal_write_failed; existing positions remain managed in memory"; throw; }
}

void Engine::Log(const std::string& text) noexcept { try { port_.Log(text); } catch (...) {} }
void Engine::SaveProgress() {
    if (!storage_failed_) try { Save(); return; } catch (...) { Log("STORAGE_DEGRADED "+warning_); }
    dirty_=false;
}

void Engine::Review(Link& link, const std::string& reason) {
    link.phase = Phase::Review;
    link.reason = reason;
    dirty_=true;
    Log("REVIEW rule=" + std::to_string(link.rule) + " source_position=" +
              std::to_string(link.source.position) + " reason=" + reason);
}

void Engine::ReportFault(const std::string& reason) {
    warning_=reason;
    Log("RUNTIME_WARNING "+reason+"; worker continues; no global halt");
}

void Engine::Start(const BootSnapshot& boot) {
    if (started_ || boot.time_ms<=0) throw std::runtime_error("invalid activation barrier");
    port_.BeginCycle();
    if (!state_.halt_reason.empty()) warning_="legacy runtime fault: "+state_.halt_reason;
    state_.halt_reason.clear();
    std::size_t retired_legacy=0,retired_refusals=0,retired_minimum=0;
    if (restored_) {
        for (auto& i:state_.intents) if (i.state==IntentState::Prepared || i.state==IntentState::Submitted || i.state==IntentState::Review) {
            i.state=IntentState::Review;
            auto& link=state_.links.at(i.link); link.phase=Phase::Review; link.reason="restart_unresolved_request";
        }
        for (auto& link:state_.links) try {
            if (link.phase==Phase::Candidate) { link.phase=Phase::Skipped; link.reason="restart_unsubmitted_opening"; }
            else if (link.phase==Phase::Opening || link.phase==Phase::Closing) Review(link,"restart_missing_request");
            else if (link.phase==Phase::Active) {
                if (!link.exit_requested) {
                    const auto& source=ReadSource(link.source);
                    if (!ValidSource(source) || source.opening_units!=link.source_initial || source.remaining!=link.source_remaining ||
                        source.closes.size()!=link.processed_closes.size() || std::any_of(source.closes.begin(),source.closes.end(),
                        [&](const SourceClose& c){return !Contains(link.processed_closes,c.deal);})) {
                        Review(link,"source_changed_while_stopped"); continue;
                    }
                }
                const auto actual=port_.FollowerRemaining(Rule(link).follower_login,link.follower_position,link.follower_symbol,FollowerSide(link));
                if (actual>link.follower_remaining) Review(link,"follower_volume_increased");
                else { link.follower_remaining=actual; if (!actual) link.phase=Phase::ManualClosed; }
            }
        } catch (const std::exception& e) { Review(link,std::string("restart_mapping_read_failed: ")+e.what()); }
          catch (...) { Review(link,"restart_mapping_read_failed"); }
        retired_legacy=RetireLegacyUnsubmittedReviews(state_); retired_refusals=RetireKnownNoMoneyReviews(state_);
        retired_minimum=RetireMinimumOpeningReviews(state_);
    }
    state_.activation_ms=boot.time_ms; state_.activation_sequence=boot.sequence; state_.baseline=boot.positions;
    // The global startup barrier also protects all subscriptions from catch-up.
    // Callback sequences belong to one EventQueue lifetime. A persisted join
    // watermark from the previous process must not fence fresh callbacks in a
    // new queue. Keep the durable join time, and use this startup's watermark.
    std::size_t rebased_subscriptions=0;
    for (auto& barrier:state_.subscriptions) if (barrier.sequence!=boot.sequence) {
        barrier.sequence=boot.sequence; ++rebased_subscriptions;
    }
    SaveProgress(); started_=true; source_cache_.clear(); pending_.clear(); work_links_.clear();
    for (std::size_t n=0;n<state_.links.size();++n)
        if (!Terminal(state_.links[n].phase) && state_.links[n].phase!=Phase::Review) work_links_.push_back(n);
    PruneSettled();
    if (!storage_failed_ && retired_legacy) Log("MIGRATION cleared_legacy_unsubmitted_reviews="+std::to_string(retired_legacy)+"; no trades sent");
    if (!storage_failed_ && retired_refusals) Log("MIGRATION cleared_known_no_money_reviews="+std::to_string(retired_refusals)+"; no trades sent");
    if (!storage_failed_ && retired_minimum) Log("MIGRATION cleared_unsubmitted_minimum_reviews="+std::to_string(retired_minimum)+"; no trades sent");
    if (!storage_failed_ && rebased_subscriptions) Log("RESTART rebased_subscription_sequences="+std::to_string(rebased_subscriptions)+
        " queue_watermark="+std::to_string(boot.sequence)+"; join times preserved; no opening catch-up");
    Log(state_.openings_paused?"PAUSED by operator; existing mappings remain managed":"READY isolated runtime; no global trade halt; no opening catch-up");
}

void Engine::Admit(const Deal& deal, Id sequence) {
    AdmitBatch({{deal,sequence}});
}
void Engine::AdmitBatch(const std::vector<std::pair<Deal,Id>>& events,bool persist) {
    if (events.size()>kAdmissionBatch) throw std::runtime_error("admission batch exceeds budget");
    for (const auto& event:events) try { AdmitOne(event.first,event.second); }
        catch (const std::exception& e) { ReportFault(std::string("source_event_skipped: ")+e.what()); }
        catch (...) { ReportFault("source_event_skipped"); }
    if (dirty_ && persist) SaveProgress();
}

void Engine::AdmitOne(const Deal& deal, Id sequence) {
    if (!started_ || state_.openings_paused || storage_failed_ || !config_.enabled || !deal.opening ||
        deal.copier_owned || sequence <= state_.activation_sequence ||
        deal.time_ms <= state_.activation_ms) return;
    if (deal.time_ms<=state_.source_floor_ms) return;
    if (!deal.id || !deal.login || !deal.order || !deal.position || !deal.units ||
        deal.symbol.empty() || deal.symbol.size() > 255)
        throw std::runtime_error("invalid source callback");
    if (std::find(state_.baseline.begin(), state_.baseline.end(),
                  std::make_pair(deal.login, deal.position)) != state_.baseline.end()) return;
    const auto previous_size=state_.links.size();
    const auto admitted=[&](Id id) {
        const auto b=std::find_if(state_.subscriptions.begin(),state_.subscriptions.end(),[&](const SubscriptionBarrier& b){return b.rule==id;});
        return b==state_.subscriptions.end() || (deal.time_ms>b->time_ms && sequence>b->sequence);
    };
    std::size_t additions=0;
    for (const auto& rule:config_.relationships) if (rule.enabled && admitted(rule.id) && rule.source_login==deal.login && symbol_masks_.at(rule.id).Matches(deal.symbol) &&
        std::none_of(state_.links.begin(),state_.links.end(),[&](const Link& l) {
            return l.rule==rule.id && l.source.login==deal.login && l.source.position==deal.position;
        }) && std::none_of(state_.closed_sources.begin(),state_.closed_sources.end(),[&](const ClosedSource& c) {
            return c.rule==rule.id && c.login==deal.login && c.position==deal.position;
        })) ++additions;
    if (!additions) return;
    const auto capacity=OpeningCapacity(additions);
    if (!capacity.empty()) { PauseForCapacity(capacity); return; }
    for (const auto& rule : config_.relationships) {
        if (!rule.enabled || !admitted(rule.id) || rule.source_login != deal.login || !symbol_masks_.at(rule.id).Matches(deal.symbol)) continue;
        if (std::any_of(state_.closed_sources.begin(),state_.closed_sources.end(),[&](const ClosedSource& c) {
            return c.rule==rule.id && c.login==deal.login && c.position==deal.position;
        })) continue;
        const bool exists = std::any_of(state_.links.begin(), state_.links.end(), [&](const Link& link) {
            return link.rule == rule.id && link.source.login == deal.login && link.source.position == deal.position;
        });
        if (exists) continue;
        if (state_.links.size() >= kMaxLinks) { PauseForCapacity("mapping capacity reserve reached"); return; }
        Link link;
        link.rule = rule.id;
        link.opening_rule = rule;
        link.source = deal;
        if (!rule.auto_symbol) link.follower_symbol=deal.symbol;
        state_.links.push_back(std::move(link));
        work_links_.push_back(state_.links.size()-1);
    }
    if (state_.links.size()!=previous_size) dirty_=true; // One durable admission per bounded callback batch.
}

std::string Engine::OpeningCapacity(std::size_t additions) {
    if (state_.links.size()+additions>kOpeningLinkLimit)
        return "在管映射将超过 "+std::to_string(kOpeningLinkLimit)+" 条，保留已有仓位平仓空间";
    if (state_.intents.size()>=kMaxIntents/2) return "未完成请求记录接近容量阈值，保留平仓空间";
    return store_.OpeningBlockReason(state_,additions);
}
void Engine::PauseForCapacity(const std::string& reason) {
    state_.capacity_pause=reason;
    ReportFault("new source admission skipped: "+reason+"; existing mappings continue");
}

void Engine::ResolvePending() {
    bool changed=false;
    struct Confirmation { std::size_t index; std::int64_t first_ms, last_ms; };
    std::vector<Confirmation> confirmed;
    for (const auto index:pending_) {
        auto& intent=state_.intents.at(index);
        if (intent.state!=IntentState::Submitted) continue;
        try {
            const auto result=port_.Reconcile(intent);
            changed=ResolveOne(intent,&result) || changed;
            if (intent.state==IntentState::Confirmed)
                confirmed.push_back({index,result.first_deal_ms,result.last_deal_ms});
        } catch (const std::exception& e) {
            Result r; r.status=Result::Status::Uncertain; r.reason=e.what();
            IsolateExecution(intent,r,"reconcile_exception"); changed=true;
        } catch (...) {
            Result r; r.status=Result::Status::Uncertain;
            IsolateExecution(intent,r,"reconcile_exception"); changed=true;
        }
    }
    if (changed) {
        ++performance_.confirmation_saves;
        PerformanceTimer timer{performance_.confirmation_save_us,performance_.confirmation_save_max_us};
        SaveProgress();
    }
    for (const auto index:pending_) if (state_.intents[index].state!=IntentState::Submitted)
        port_.ForgetRequest(state_.intents[index].id);
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](std::size_t n){return state_.intents[n].state!=IntentState::Submitted;}),pending_.end());
    for (const auto& confirmation:confirmed) {
        const auto& i=state_.intents[confirmation.index];
        std::string timing;
        if (confirmation.first_ms>0 && confirmation.last_ms>=confirmation.first_ms)
            timing=" target_first_deal_ms="+std::to_string(confirmation.first_ms)+
                " target_last_deal_ms="+std::to_string(confirmation.last_ms);
        Log("CONFIRMED intent="+std::to_string(i.id)+" target_position="+std::to_string(i.result_position)+" volume_ext="+std::to_string(i.filled)+timing);
    }
}

void Engine::IsolateExecution(Intent& intent,const Result& result,const std::string& reason) {
    intent.state=IntentState::Review;
    auto& link=state_.links.at(intent.link); link.phase=Phase::Review; link.reason=reason; dirty_=true;
    Log("ISOLATED_MAPPING intent="+std::to_string(intent.id)+" request="+std::to_string(intent.request_id)+
        " source_position="+std::to_string(link.source.position)+" target="+std::to_string(intent.follower_login)+
        " rule="+std::to_string(link.rule)+" reason="+reason+" evidence_reason="+result.reason+
        "; this mapping requires review; other mappings and subscriptions continue; no retry");
}

bool Engine::ResolveOne(Intent& intent,const Result* evidence) {
    auto& link = state_.links.at(intent.link);
    Result queried;
    if (!evidence) { queried=port_.Reconcile(intent); evidence=&queried; }
    const auto& result = *evidence;
    if (result.status == Result::Status::Waiting && !Expired(port_.NowMs(), intent.created_ms))
        return false;
    intent.retcode = result.retcode;
    intent.filled = result.units;
    intent.result_position = result.position;
    intent.deals = result.deals;
    if (result.status == Result::Status::Rejected && !result.units) {
        intent.state = IntentState::Rejected;
        link.reason=result.reason=="server_pretrade_no_money_zero_fill" ? "server_rejected_no_money" : "request_rejected_no_retry";
        link.phase=Phase::Skipped;
        if (!SettledNoMoneyOpening(link,intent)) Review(link,link.reason);
        else dirty_=true;
        Log("REJECTED intent="+std::to_string(intent.id)+" target="+std::to_string(intent.follower_login)+
            " operation="+(intent.close?"close":"open")+" request="+std::to_string(intent.request_id)+
            " source_position="+std::to_string(link.source.position)+" symbol="+intent.symbol+
            " requested_ext="+std::to_string(intent.units)+" retcode="+std::to_string(result.retcode)+
            " reason="+result.reason+"; no retry; other openings and confirmed position closes remain managed");
        return true;
    }
    if (result.status != Result::Status::Filled || result.units != intent.units ||
        !result.position || result.deals.empty() ||
        (intent.close && result.position != intent.follower_position)) {
        const auto reason=result.status==Result::Status::Waiting ? "execution_confirmation_timeout" :
            (result.status==Result::Status::Filled && result.units<intent.units ? "partial_execution" : "uncertain_execution");
        IsolateExecution(intent,result,reason);
        return true;
    }
    const Units expected = intent.close ? intent.follower_before - intent.units : intent.units;
    const auto actual = port_.FollowerRemaining(intent.follower_login, result.position,
                                                 intent.symbol, FollowerSide(link));
    if (actual > expected) {
        intent.state = IntentState::Review;
        link.phase = Phase::Review;
        link.reason = "execution_position_mismatch";
        Log("ISOLATED_MAPPING execution_position_mismatch intent="+std::to_string(intent.id));
        return true;
    }
    if (!intent.close) {
        for (std::size_t i = 0; i < state_.links.size(); ++i) {
            if (i != intent.link && !Terminal(state_.links[i].phase) && state_.links[i].follower_position == result.position &&
                Rule(state_.links[i]).follower_login == intent.follower_login) {
                intent.state = IntentState::Review;
                link.phase = Phase::Review;
                link.reason = "follower_position_already_mapped";
                Review(state_.links[i],"follower_position_already_mapped");
                return true;
            }
        }
        link.follower_position = result.position;
    }
    link.follower_remaining = actual;
    link.phase = actual ? Phase::Active : Phase::Closed;
    intent.state = IntentState::Confirmed;
    return true;
}

void Engine::Prepare(std::size_t index, Intent intent, std::int64_t source_event_ms) {
    if (state_.intents.size() >= kMaxIntents) { ReportFault("intent_limit_reached"); return; }
    auto& link = state_.links.at(index);
    if (state_.next_intent_id==std::numeric_limits<Id>::max()) { ReportFault("intent_id_exhausted"); return; }
    intent.id = state_.next_intent_id++;
    intent.link = index;
    intent.follower_login = Rule(link).follower_login;
    intent.follower_position = intent.close ? link.follower_position : 0;
    intent.side = intent.close ? Opposite(FollowerSide(link)) : FollowerSide(link);
    intent.symbol = link.follower_symbol;
    intent.created_ms = port_.NowMs();
    batch_source_times_[intent.id]=source_event_ms;
    link.phase = intent.close ? Phase::Closing : Phase::Opening;
    state_.intents.push_back(intent);
}

void Engine::AbandonPrepared(std::size_t first, const std::string& reason) {
    for (std::size_t n=first;n<state_.intents.size();++n) {
        auto& intent=state_.intents[n];
        if (intent.state!=IntentState::Prepared) continue;
        intent.state=IntentState::Rejected; // Known not to have called the SDK.
        auto& link=state_.links[intent.link];
        link.phase=Phase::Review; link.reason=reason;
    }
}

void Engine::SubmitBatch(std::size_t first,std::int64_t planning_ms) {
    const auto before_flush=Clock::now(); SaveProgress(); const auto before_dispatch=Clock::now();
    std::size_t queued=0;
    for (std::size_t n=first;n<state_.intents.size();++n) {
        auto& intent=state_.intents[n]; auto& link=state_.links.at(intent.link);
        try {
            if (!intent.close && (storage_failed_ || Expired(port_.NowMs(),link.source.time_ms))) {
                intent.state=IntentState::Rejected; Review(link,storage_failed_?"opening_not_submitted_storage_unavailable":"opening_expired_before_submission"); continue;
            }
            Log("PREPARED intent="+std::to_string(intent.id)+" rule="+std::to_string(link.rule)+
                " target="+std::to_string(intent.follower_login)+" symbol="+intent.symbol+" source_symbol="+link.source.symbol+
                " source_login="+std::to_string(link.source.login)+" source_position="+std::to_string(link.source.position)+
                " source_deal="+std::to_string(intent.source_event)+" source_open_ms="+std::to_string(link.source.time_ms)+
                " queue_age_ms="+std::to_string(port_.NowMs()-batch_source_times_.at(intent.id))+
                " source_event_ms="+std::to_string(batch_source_times_.at(intent.id))+" close_position="+std::to_string(intent.follower_position)+
                " volume_ext="+std::to_string(intent.units)+(link.exit_requested?" subscription_exit=1":""));
            const auto submitted=port_.Submit(intent); intent.request_id=submitted.request_id; intent.retcode=submitted.retcode;
            if (submitted.status==Submission::Status::Queued && submitted.request_id) {
                intent.state=IntentState::Submitted; pending_.push_back(n); ++queued;
                Log("QUEUED intent="+std::to_string(intent.id)+" request="+std::to_string(submitted.request_id));
            } else if (submitted.status==Submission::Status::NotSubmitted) {
                intent.state=IntentState::Rejected;
                if (std::string_view(kPlatformName)=="mt4" && !intent.close &&
                    submitted.retcode==134 && !submitted.request_id) {
                    link.phase=Phase::Skipped; link.reason="mt4_not_submitted_no_money";
                    if (SettledUnsubmittedNoMoneyOpening(link,intent)) {
                        dirty_=true;
                        Log("NOT_SUBMITTED_NO_MONEY intent="+std::to_string(intent.id)+
                            " target="+std::to_string(intent.follower_login)+
                            " retcode=134; synchronous refusal without request ID; no order queued; no retry");
                    } else Review(link,"request_not_submitted");
                } else Review(link,"request_not_submitted");
            } else { Result r; r.status=Result::Status::Uncertain; IsolateExecution(intent,r,"submission_result_unknown"); }
        } catch (const std::exception& e) {
            Result r; r.status=Result::Status::Uncertain; r.reason=e.what(); IsolateExecution(intent,r,"submission_exception");
        } catch (...) { Result r; r.status=Result::Status::Uncertain; IsolateExecution(intent,r,"submission_exception"); }
    }
    const auto after_dispatch=Clock::now(); SaveProgress(); const auto after_flush=Clock::now();
    for (std::size_t n=first;n<state_.intents.size();++n)
        if (state_.intents[n].state!=IntentState::Submitted) port_.ForgetRequest(state_.intents[n].id);
    Log("BATCH first_intent="+std::to_string(state_.intents[first].id)+" prepared="+std::to_string(state_.intents.size()-first)+
        " queued="+std::to_string(queued)+" in_flight="+std::to_string(pending_.size())+" planning_ms="+std::to_string(planning_ms)+
        " prepare_flush_ms="+std::to_string(Milliseconds(before_flush,before_dispatch))+" dispatch_ms="+std::to_string(Milliseconds(before_dispatch,after_dispatch))+
        " result_flush_ms="+std::to_string(Milliseconds(after_dispatch,after_flush)));
}

const SourceView& Engine::ReadSource(const Deal& deal) {
    SourceKey key{deal.login,deal.position,deal.order,deal.id,deal.units,deal.time_ms,
                  deal.symbol,deal.side,deal.opening,deal.copier_owned};
    auto found=source_cache_.find(key);
    if (found!=source_cache_.end()) return found->second;

    return source_cache_.emplace(std::move(key),port_.ReadSource(deal)).first->second;
}

void Engine::PrioritizeSourceCloses(std::size_t index,const SourceView& source) {
    const auto& current=state_.links[index];
    if (source.remaining>=current.source_remaining) return;
    const auto group=active_sources_.find({current.source.login,current.source.position});
    if (group==active_sources_.end()) return;
    for (const auto n:group->second) {
        const auto& link=state_.links[n];
        if (n!=index && link.phase==Phase::Active && source.remaining<link.source_remaining &&
            queued_closes_.insert(n).second) ready_closes_.push_back(n);
    }
}

void Engine::Process(std::size_t index) {
    auto& link = state_.links.at(index);
    if (link.phase != Phase::Candidate && link.phase != Phase::Active) return;
    const auto& rule = Rule(link);
    if (link.exit_requested && link.phase==Phase::Active) {
        const auto actual=port_.FollowerRemaining(rule.follower_login,link.follower_position,link.follower_symbol,FollowerSide(link));
        if (actual>link.follower_remaining) { Review(link,"exit_position_volume_increased"); return; }
        link.follower_remaining=actual; dirty_=true;
        if (!actual) { link.phase=Phase::ManualClosed; return; }
        const auto limits=port_.Limits(rule,link.source.symbol,link.follower_symbol,Opposite(FollowerSide(link)),true);
        const auto plan=PlanClosing(1,1,actual,limits);
        if (plan.code!=Code::Ready) { Review(link,"exit_"+std::string(CodeName(plan.code))); return; }
        Intent intent; intent.close=true; intent.units=plan.units; intent.follower_before=actual;
        Prepare(index,intent,port_.NowMs()); return;
    }
    if (link.phase==Phase::Candidate && storage_failed_) {
        link.phase=Phase::Skipped; link.reason="opening_not_submitted_storage_unavailable"; dirty_=true; return;
    }
    if (link.phase == Phase::Candidate && Expired(port_.NowMs(), link.source.time_ms)) {
        link.phase=Phase::Skipped; link.reason="source_opening_expired"; dirty_=true; return;
    }
    const auto& source = ReadSource(link.source);
    if (source.state == ReadState::Waiting) {
        const auto now = port_.NowMs();
        const auto wait = source_wait_since_.emplace(index, now).first;
        if (Expired(now, wait->second)) Review(link, "source_history_not_ready");
        return;
    }
    source_wait_since_.erase(index);
    if (!ValidSource(source)) { Review(link, "source_evidence_invalid: " + source.reason); return; }
    if (link.phase == Phase::Candidate) {
        // A later fill callback for an old, pruned lifecycle must not reopen it.
        if (source.first_fill_ms<=state_.source_floor_ms || Expired(port_.NowMs(),source.first_fill_ms)) {
            link.phase=Phase::Skipped; link.reason="source_opening_outside_retention_window";
            dirty_=true; return;
        }
        // The entire entry order must have finished. One fixed amount per new
        // position, even if its source opening produces several deal callbacks.
        const auto barrier=std::find_if(state_.subscriptions.begin(),state_.subscriptions.end(),[&](const SubscriptionBarrier& b){return b.rule==link.rule;});
        if (source.first_fill_ms <= state_.activation_ms ||
            (barrier!=state_.subscriptions.end() && source.first_fill_ms<=barrier->time_ms) || !source.closes.empty()) {
            link.phase=Phase::Skipped; link.reason="source_opening_predates_activation_or_already_closed";
            dirty_=true;
            Log("SKIPPED rule="+std::to_string(link.rule)+" source_position="+
                std::to_string(link.source.position)+" reason="+link.reason);
            return;
        }
        link.source_initial = link.source_remaining = source.opening_units;
        VolumePolicy policy;
        std::string error;
        if (!MakePolicy(rule, policy, error)) throw std::runtime_error(error);
        if (link.follower_symbol.empty()) {
            const auto resolved=port_.ResolveSymbol(rule,link.source.symbol,FollowerSide(link));
            if (!resolved.error.empty() || resolved.symbol.empty()) {
                Review(link,"symbol_resolution: "+(resolved.error.empty()?"未返回目标品种":resolved.error)); return;
            }
            if (resolved.symbol.size()>255) throw std::runtime_error("target symbol too long");
            link.follower_symbol=resolved.symbol;
        }
        const auto limits = port_.Limits(rule, link.source.symbol, link.follower_symbol, FollowerSide(link), false);
        Intent intent;
        if (policy.mode == SizingMode::EquityRatio) intent.equity = port_.Equities(rule);
        const auto plan = PlanOpening(policy, {source.opening_units, intent.equity.source,
                                               intent.equity.follower}, limits);
        if (plan.code==Code::SkipBelowMinimum) {
            link.phase=Phase::Skipped; link.reason=CodeName(plan.code); dirty_=true;
            Log("SKIPPED rule="+std::to_string(link.rule)+" source_position="+
                std::to_string(link.source.position)+" reason="+link.reason);
            return;
        }
        if (plan.code != Code::Ready) { Review(link, CodeName(plan.code)); return; }
        intent.units = plan.units;
        intent.source_event = link.source.id;
        Prepare(index, intent, link.source.time_ms);
        return;
    }
    if (source.opening_units != link.source_initial) { Review(link, "source_entry_volume_changed"); return; }
    Units known_closed = 0;
    std::size_t known_count = 0;
    for (const auto& close : source.closes) if (Contains(link.processed_closes, close.deal)) {
        known_closed += close.units; // ValidSource has already checked the total.
        ++known_count;
    }
    if (known_count != link.processed_closes.size() ||
        link.source_initial - known_closed != link.source_remaining) {
        Review(link, "source_history_changed"); return;
    }
    PrioritizeSourceCloses(index,source);
    const Units actual = port_.FollowerRemaining(rule.follower_login, link.follower_position,
                                                link.follower_symbol, FollowerSide(link));
    if (actual > link.follower_remaining) { Review(link, "follower_volume_increased"); return; }
    if (!actual) {
        link.follower_remaining = 0;
        link.phase = Phase::ManualClosed;
        dirty_=true;
        Log("MANUAL_CLOSED target_position=" + std::to_string(link.follower_position));
        return;
    }
    if (actual != link.follower_remaining) { link.follower_remaining = actual; dirty_=true; }
    for (const auto& close : source.closes) {
        if (Contains(link.processed_closes, close.deal)) continue;
        const auto limits = port_.Limits(rule, link.source.symbol, link.follower_symbol, Opposite(FollowerSide(link)), true);
        const auto plan = PlanClosing(link.source_remaining, close.units, actual, limits);
        if (plan.code != Code::Ready && plan.code != Code::SkipBelowMinimum) {
            Review(link, CodeName(plan.code)); return;
        }
        link.source_remaining -= close.units;
        link.processed_closes.push_back(close.deal);
        if (plan.code == Code::SkipBelowMinimum) { dirty_=true; continue; }
        Intent intent;
        intent.close = true;
        intent.source_event = close.deal;
        intent.units = plan.units;
        intent.follower_before = actual;
        Prepare(index, intent, close.time_ms);
        return;
    }
}

void Engine::Tick(bool reuse_confirmation) {
    if (!started_) return;
    const bool reuse=std::string_view(kPlatformName)=="mt5" && reuse_confirmation && confirmation_ready_;
    confirmation_ready_=false;
    TickWork(reuse);
    PruneSettled();
}

void Engine::PruneSettled() {
    if (!started_) return;
    const auto now=port_.NowMs();
    const auto rolling_floor=now>kWaitMs?now-kWaitMs:0;
    auto floor=std::max({state_.source_floor_ms,state_.activation_ms,rolling_floor});
    const bool expired=std::any_of(state_.closed_sources.begin(),state_.closed_sources.end(),
        [&](const ClosedSource& c) { return c.opening_ms<=floor; });
    std::vector<bool> blocked(state_.links.size(),false);
    std::vector<unsigned> intent_counts(state_.links.size(),0);
    for (const auto& intent:state_.intents) ++intent_counts.at(intent.link);
    std::vector<Id> latest_close(state_.links.size(),0);
    bool superseded_close=false;
    for (const auto& intent:state_.intents)
        if (intent.state!=IntentState::Confirmed &&
            !(intent_counts[intent.link]==1 && (SettledNoMoneyOpening(state_.links.at(intent.link),intent) ||
              SettledUnsubmittedNoMoneyOpening(state_.links.at(intent.link),intent)))) blocked.at(intent.link)=true;
        else if (intent.close) {
            superseded_close=superseded_close || latest_close.at(intent.link)!=0;
            latest_close.at(intent.link)=intent.id;
        }
    bool removable=false;
    for (std::size_t n=0;n<state_.links.size() && !removable;++n) {
        const auto& link=state_.links[n];
        removable=Terminal(link.phase) && !link.follower_remaining && !blocked[n] &&
            (link.source.time_ms<=floor || state_.closed_sources.size()<100000 || expired);
    }
    const bool expiry_only=!removable && !superseded_close;
    if (expiry_only) {
        if (!expired) return;
        if constexpr (std::string_view(kPlatformName)=="mt5") {
            // Expiry keys have no trade details and can remain up to 999 ms
            // longer. Batch only this housekeeping: terminal retirement,
            // capacity release and partial-close evidence cleanup stay prompt.
            // A deterministic cutoff also survives restarts and clock rollback;
            // never move behind either durable admission barrier.
            floor=std::max({state_.source_floor_ms,state_.activation_ms,
                           rolling_floor/kExpiryPruneBatchMs*kExpiryPruneBatchMs});
            if (std::none_of(state_.closed_sources.begin(),state_.closed_sources.end(),
                [&](const ClosedSource& c) { return c.opening_ms<=floor; })) {
                ++performance_.prune_expiry_deferred_passes;
                return;
            }
        }
    }
    // Idle polls must not deep-copy every live position merely to find no work.
    State next=state_;
    next.closed_sources.erase(std::remove_if(next.closed_sources.begin(),next.closed_sources.end(),
        [&](const ClosedSource& c) { return c.opening_ms<=floor; }),next.closed_sources.end());
    const auto expired_removed=state_.closed_sources.size()-next.closed_sources.size();
    const auto absent=std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> remap(state_.links.size(),absent);
    next.links.clear(); next.intents.clear();
    for (std::size_t n=0;n<state_.links.size();++n) {
        const auto& link=state_.links[n];
        const bool recent=link.source.time_ms>floor;
        if (Terminal(link.phase) && !link.follower_remaining && !blocked[n] &&
            (!recent || next.closed_sources.size()<100000)) {
            if (recent) next.closed_sources.push_back({link.rule,link.source.login,link.source.position,link.source.time_ms});
        } else { remap[n]=next.links.size(); next.links.push_back(link); }
    }
    for (auto intent:state_.intents) if (remap.at(intent.link)!=absent) {
        if (intent.close && intent.state==IntentState::Confirmed && latest_close[intent.link]!=intent.id) continue;
        intent.link=remap[intent.link]; next.intents.push_back(std::move(intent));
    }
    if (next.links.size()==state_.links.size() && next.intents.size()==state_.intents.size() &&
        next.closed_sources.size()==state_.closed_sources.size()) return;
    next.source_floor_ms=floor;
    if (next.revision==std::numeric_limits<Id>::max()) throw std::runtime_error("journal revision exhausted");
    ++next.revision;
    if (!storage_failed_) try {
        store_.Prune(next);
        ++performance_.prune_commits;
        if (expiry_only) ++performance_.prune_expiry_only_commits;
        performance_.prune_closed_sources_removed+=expired_removed;
    }
    catch (...) { storage_failed_=true; ReportFault("journal_compaction_failed; continuing known positions in memory"); }
    const auto next_retained=[&](std::size_t cursor) {
        for (std::size_t n=0;n<work_links_.size();++n) {
            const auto old=work_links_[(cursor+n)%work_links_.size()];
            if (remap[old]!=absent && !Terminal(next.links[remap[old]].phase) && next.links[remap[old]].phase!=Phase::Review)
                return remap[old];
        }
        return absent;
    };
    const auto active_next=next_retained(next_link_), candidate_next=next_retained(next_candidate_);
    state_=std::move(next);
    pending_.clear(); work_links_.clear();
    for (std::size_t n=0;n<state_.intents.size();++n)
        if (state_.intents[n].state==IntentState::Submitted) pending_.push_back(n);
    for (std::size_t n=0;n<state_.links.size();++n)
        if (!Terminal(state_.links[n].phase) && state_.links[n].phase!=Phase::Review) work_links_.push_back(n);
    const auto cursor_for=[&](std::size_t index) {
        const auto found=std::find(work_links_.begin(),work_links_.end(),index);
        return found==work_links_.end()?std::size_t{0}:static_cast<std::size_t>(found-work_links_.begin());
    };
    next_link_=cursor_for(active_next); next_candidate_=cursor_for(candidate_next);
    decltype(source_wait_since_) waits;
    for (const auto& entry:source_wait_since_) if (entry.first<remap.size() && remap[entry.first]!=absent)
        waits.emplace(remap[entry.first],entry.second);
    source_wait_since_=std::move(waits); source_cache_.clear();
    decltype(ready_closes_) ready;
    queued_closes_.clear();
    for (const auto index:ready_closes_) if (index<remap.size() && remap[index]!=absent &&
        state_.links[remap[index]].phase==Phase::Active && queued_closes_.insert(remap[index]).second)
        ready.push_back(remap[index]);
    ready_closes_=std::move(ready); active_sources_.clear();

}

void Engine::ConfirmPending() {
    if (!started_) return;
    confirmation_ready_=false;
    ++performance_.confirmation_passes;
    PerformanceTimer timer{performance_.confirmation_us,performance_.confirmation_max_us};
    port_.BeginCycle();
    ResolvePending();
    confirmation_ready_=true;
}
void Engine::NotifySourceClose(Id login,Id position) {
    if (login && position && close_hints_.size()<kOpeningLinkLimit)
        close_hints_.emplace(std::make_pair(login,position),true);
}
void Engine::RefreshExitingReviews() {
    // Read-only history reconciliation for a specifically requested exit. Never
    // resubmit an ambiguous request; dispatch a close only after its full fill
    // and exact target position have been independently verified.
    const auto now=port_.NowMs();
    if (now>=last_exit_review_ms_ && now-last_exit_review_ms_<250) return;
    last_exit_review_ms_=now;
    const auto total=state_.intents.size();
    if (!total) return;
    const auto start=next_exit_review_%total;
    unsigned reads=0; bool changed=false;
    for (std::size_t n=0;n<total;++n) {
        const auto index=(start+n)%total; auto& intent=state_.intents[index];
        auto& link=state_.links.at(intent.link);
        if (!link.exit_requested || link.phase!=Phase::Review || intent.state!=IntentState::Review) continue;
        next_exit_review_=index+1;
        try {
            const auto result=port_.Reconcile(intent);
            if (result.status==Result::Status::Filled && result.units==intent.units) {
                changed=ResolveOne(intent,&result) || changed;
                if (link.phase==Phase::Active && std::find(work_links_.begin(),work_links_.end(),intent.link)==work_links_.end())
                    work_links_.push_back(intent.link);
            }
        } catch (...) { /* Keep this exit pending; another relationship still runs. */ }
        if (++reads>=8) break;
    }
    if (changed) SaveProgress();
}

void Engine::TickWork(bool reuse_confirmation) {
    if (reuse_confirmation) ++performance_.confirmation_reuses;
    else ConfirmPending();
    confirmation_ready_=false;
    RefreshExitingReviews();
    if (pending_.size()>=kMaxInFlight) { ++performance_.global_limit_hits; if (dirty_) SaveProgress(); return; }
    if (!dirty_) PruneSettled();
    if (!state_.openings_paused && std::any_of(state_.links.begin(),state_.links.end(),
        [](const Link& l) { return l.phase==Phase::Candidate; })) {
        const auto capacity=OpeningCapacity(0);
        if (!capacity.empty()) {
            PauseForCapacity(capacity);
            for (auto& link:state_.links) if (link.phase==Phase::Candidate) {
                link.phase=Phase::Skipped; link.reason="opening_capacity_exceeded"; dirty_=true;
            }
        }
    }
    source_cache_.clear(); batch_source_times_.clear(); active_sources_.clear();
    for (const auto n:work_links_) if (state_.links[n].phase==Phase::Active) {
        const auto& source=state_.links[n].source;
        active_sources_[{source.login,source.position}].push_back(n);
    }
    for (const auto& hint:close_hints_) {
        const auto found=active_sources_.find(hint.first);
        if (found!=active_sources_.end()) for (const auto n:found->second)
            if (queued_closes_.insert(n).second) ready_closes_.push_back(n);
    }
    close_hints_.clear();
    std::unordered_map<Id,std::size_t> busy, closing;
    std::unordered_set<Id> opening_accounts;
    for (const auto& link:state_.links) if (link.phase==Phase::Candidate) opening_accounts.insert(Rule(link).follower_login);
    for (const auto& link:state_.links)
        if (link.phase==Phase::Opening || link.phase==Phase::Closing) {
            ++busy[Rule(link).follower_login];
            if (link.phase==Phase::Closing) ++closing[Rule(link).follower_login];
        }
    const auto first=state_.intents.size();
    const auto planning_started=Clock::now();
    std::deque<std::size_t> blocked_closes;
    try {
        // Independent positions may overlap; Opening/Closing mappings never enter
        // Process. Reserve half a batch for new positions so sustained closes do not
        // starve admission. Each pass has its own cursor and read/time budget.
        for (auto phase:{Phase::Active,Phase::Candidate}) {
            auto& cursor=phase==Phase::Active?next_link_:next_candidate_;
            const auto pass_start=Clock::now();
            const auto pass_first=state_.intents.size();
            const auto quota=phase==Phase::Active && std::any_of(work_links_.begin(),work_links_.end(),
                [&](std::size_t n) { return state_.links[n].phase==Phase::Candidate; }) ? kMaxInFlight/2 : kMaxInFlight;
            std::size_t visits=0,priority_visits=0,priority_requests=0,count=0;
            const auto scan_start=work_links_.empty()?0:cursor%work_links_.size();
            while (true) {
                if (pending_.size()+state_.intents.size()-first>=kMaxInFlight) break;
                // A detected close wakes its siblings, including those behind
                // the scan cursor. Busy accounts defer only their own mappings.
                // Reserve at least half the budget for discovering other closes.
                const bool priority=phase==Phase::Active && !ready_closes_.empty() &&
                    priority_visits<kPlanningVisits/2 && priority_requests<quota/2;
                if (!priority && count>=work_links_.size()) break;
                std::size_t index;
                if (priority) { index=ready_closes_.front(); ready_closes_.pop_front(); ++priority_visits; }
                else { index=work_links_[(scan_start+count)%work_links_.size()]; ++count; }
                auto& link=state_.links[index];
                const auto account=Rule(link).follower_login;
                if (link.phase!=phase || (phase==Phase::Candidate && state_.openings_paused)) {
                    if (priority) queued_closes_.erase(index);
                    continue;
                }
                if (busy[account]>=kAccountInFlight ||
                    (phase==Phase::Active && opening_accounts.count(account) && closing[account]>=kAccountInFlight/2)) {
                    ++performance_.account_limit_visits;
                    if (priority) blocked_closes.push_back(index);
                    continue;
                }
                if (priority) queued_closes_.erase(index);
                const auto before=state_.intents.size();
                try { Process(index); }
                catch (const std::exception& e) { Review(link,std::string("mapping_planning_error: ")+e.what()); }
                catch (...) { Review(link,"mapping_planning_error"); }
                ++visits;
                if (!priority) cursor=(scan_start+count)%work_links_.size();
                if (state_.intents.size()!=before) {
                    ++busy[account]; if (phase==Phase::Active) ++closing[account];
                    if (priority) ++priority_requests;
                }
                if (visits>=kPlanningVisits || state_.intents.size()-pass_first>=quota ||
                    Milliseconds(pass_start,Clock::now())>=5) break;
            }
        }
    } catch (...) {
        for (const auto index:blocked_closes) ready_closes_.push_back(index);
        AbandonPrepared(first,"batch_not_submitted_after_planning_error");
        ReportFault("planning_exception; only unsubmitted batch skipped");
        SaveProgress(); return;
    }
    for (const auto index:blocked_closes) ready_closes_.push_back(index);
    if (state_.intents.size()==first) { if (dirty_) SaveProgress(); return; }
    SubmitBatch(first,Milliseconds(planning_started,Clock::now()));
}

void Engine::ManagementCheck(Id expected_revision) const {
    if (!started_ || storage_failed_)
        throw std::runtime_error("执行器尚未就绪或账本不可写；本次管理操作未提交");
    if (state_.revision != expected_revision)
        throw std::runtime_error("运行状态已变化，请刷新后重新操作");
}

void Engine::PauseOpenings(Id expected_revision) {
    ManagementCheck(expected_revision);
    state_.openings_paused = true;
    for (auto& link : state_.links) if (link.phase == Phase::Candidate) {
        link.phase = Phase::Skipped;
        link.reason = "paused_before_submission";
    }
    Save();
    PruneSettled();
    try { Log("MANAGEMENT paused new openings; submitted requests and existing closes remain managed"); } catch (...) {}
}

void Engine::ResumeOpenings(Id expected_revision, const BootSnapshot& boot) {
    ManagementCheck(expected_revision);
    if (!state_.openings_paused || boot.time_ms <= 0)
        throw std::runtime_error("恢复需要已暂停状态和有效启用基线");
    const auto capacity=OpeningCapacity(1);
    if (!capacity.empty()) throw std::runtime_error("容量仍不足，不能恢复新仓："+capacity);
    // Already-active links retain their original sizing and close history.
    // New callbacks from the paused interval cannot pass this fresh barrier.
    state_.activation_ms = boot.time_ms;
    state_.activation_sequence = boot.sequence;
    state_.baseline = boot.positions;
    state_.openings_paused = false;
    state_.capacity_pause.clear();
    Save();
    try { Log("MANAGEMENT resumed new openings; no opening catch-up"); } catch (...) {}
}

std::string Engine::ApplyBlockReason() const {
    if (!started_ || storage_failed_) return "执行器尚未就绪或账本不可写";
    const auto isolated=ExecutionReviewAccounts(state_);
    if (!isolated.empty()) {
        std::string reason="旧版暂停配置迁移要求先核对请求，涉及账号：";
        for (std::size_t n=0;n<std::min<std::size_t>(isolated.size(),10);++n)
            reason+=(n?", ":"")+std::to_string(isolated[n]);
        if (isolated.size()>10) reason+=" 等 "+std::to_string(isolated.size())+" 个";
        return reason+"；请使用运行中订阅更新，其他映射继续管理";
    }
    if (!state_.openings_paused) return "请先暂停新仓跟随";
    for (std::size_t n=0;n<state_.links.size();++n) {
        const auto& link=state_.links[n];
        if (link.phase==Phase::Review && !UnsubmittedSymbolBlock(state_,n)) return "存在待核对映射，请先核对交易证据";
        if (link.phase==Phase::Opening || link.phase==Phase::Closing)
            return "正在等待交易成交确认；确认完成后即可应用，无需平掉已有持仓";
        if (link.phase==Phase::Candidate) return "仍有尚未提交的候选交易，请先暂停新仓跟随";
    }
    for (const auto& intent : state_.intents)
        if (intent.state != IntentState::Confirmed) return "账本中仍有未决或未核对请求";
    return {};
}

std::string Engine::ConfigChangeBlockReason(const CopierConfig& config) const {
    const auto reason = ApplyBlockReason();
    if (!reason.empty()) return reason;
    std::string error;
    if (!ValidateConfig(config, error) || !config.enabled ||
        std::none_of(config.relationships.begin(), config.relationships.end(),
                     [](const RuleConfig& rule) { return rule.enabled; }))
        return "配置必须全局启用且至少有一条启用的关系：" + error;
    return {};
}

void Engine::UpdateSubscriptions(const std::string& expected_config,const CopierConfig& config,const BootSnapshot& boot) {
    if (!started_ || storage_failed_) throw std::runtime_error("账本不可写，未提交订阅配置；已有仓位继续管理");
    if (expected_config!=state_.config_json) throw std::runtime_error("生效配置已被修改，请重新读取后应用");
    std::string error;
    if (!ValidateConfig(config,error)) throw std::runtime_error(error);
    auto next_config=config;
    std::unordered_map<Id,SymbolMask> masks;
    for (const auto& rule:config.relationships) masks.emplace(rule.id,SymbolMask(rule.symbol_mask));
    auto next=PlanSubscriptionUpdate(state_,config_,config,boot);
    if (next.config_json==state_.config_json) return;
    std::swap(state_,next);
    try { Save(); }
    catch (...) { std::swap(state_,next); throw; }
    std::swap(config_,next_config); symbol_masks_.swap(masks);
    for (std::size_t n=0;n<state_.links.size();++n) if (state_.links[n].phase==Phase::Active && state_.links[n].exit_requested &&
        std::find(work_links_.begin(),work_links_.end(),n)==work_links_.end()) work_links_.push_back(n);
    Log("MANAGEMENT subscriptions committed; joins follow only new positions; removed/disabled relationships drain their own positions; other subscriptions continue");
}

void Engine::ApplyConfig(Id expected_revision, const CopierConfig& config, const BootSnapshot& boot) {
    ManagementCheck(expected_revision);
    const auto reason=ConfigChangeBlockReason(config);
    if (!reason.empty()) throw std::runtime_error(reason);
    if (boot.time_ms <= 0) throw std::runtime_error("invalid activation barrier");
    auto next_config = config;
    std::unordered_map<Id,SymbolMask> next_masks;
    for (const auto& rule:config.relationships) next_masks.emplace(rule.id,SymbolMask(rule.symbol_mask));
    const auto canonical = WriteConfigJson(config);
    // One durable snapshot is the commit point. Old links/intents and their IDs
    // stay in the journal; the bootstrap JSON is not a second commit target.
    auto previous = state_;
    state_.config_json = canonical;
    state_.activation_ms = boot.time_ms;
    state_.activation_sequence = boot.sequence;
    state_.baseline = boot.positions;
    try { Save(); }
    catch (...) {
        std::swap(state_, previous);
        warning_ = "journal_write_failed; configuration commit uncertain";
        throw;
    }
    std::swap(config_, next_config);
    symbol_masks_.swap(next_masks);
    try { Log("MANAGEMENT configuration committed; existing mappings keep opening rules; openings remain paused"); } catch (...) {}
}

std::string Engine::InspectIntent(Id id) {
    const auto found=std::lower_bound(state_.intents.begin(),state_.intents.end(),id,
        [](const Intent& i,Id value) { return i.id<value; });
    if (!id || found==state_.intents.end() || found->id!=id) throw std::runtime_error("请求编号不存在或所属仓位已结束并清理");
    const auto& intent = *found;
    if (intent.state==IntentState::Rejected) {
        // Final zero-fill refusal evidence was already persisted before the
        // adapter released its bounded callback slot. Do not call its absence
        // from order history a new timeout or present a journal read as live API evidence.
        return "只读核对请求 "+std::to_string(id)+"\r\n账本已记录失败结果（非本次服务器查询）"+
            "\r\n账号="+std::to_string(intent.follower_login)+"，服务器请求="+std::to_string(intent.request_id)+
            "，返回码="+std::to_string(intent.retcode)+"\r\n原因="+state_.links.at(intent.link).reason+
            "\r\n未修改账本状态、未解除暂停、未重发交易。";
    }
    port_.BeginCycle(); // Explicit inspection must not reuse a previous poll's evidence.
    const auto result = port_.Reconcile(intent);
    const char* names[] = {"等待 / 尚无完整证据", "完整成交", "拒绝", "结果不确定 / 部分成交"};
    std::ostringstream out;
    out << "只读核对请求 " << id << "\r\n服务器证据：" << names[static_cast<unsigned>(result.status)]
        << "\r\n账号=" << intent.follower_login << "，操作=" << (intent.close?"平仓":"开仓")
        << "，服务器请求=" << intent.request_id << "，证据详情=" << result.reason
        << "\r\n请求量 VolumeExt=" << intent.units << "，证据成交量=" << result.units
        << "\r\n目标持仓=" << result.position << "，返回码=" << result.retcode << "\r\n成交票号：";
    for (std::size_t n=0;n<std::min<std::size_t>(100,result.deals.size());++n) out << result.deals[n] << ' ';
    if (result.deals.size()>100) out << "（仅显示前 100 笔，共 " << result.deals.size() << " 笔）";
    out << "\r\n未修改账本状态、未解除暂停、未重发交易。";
    return out.str();
}

} // namespace tradecopier::execution

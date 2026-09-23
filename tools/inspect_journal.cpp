#include "tradecopier/journal.hpp"
#include <iostream>
#include <stdexcept>

using namespace tradecopier::execution;
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    try {
        if (argc!=2) throw std::runtime_error("Usage: TradeCopierJournal <existing-journal-file> (stop executor first)");
#ifdef _WIN32
        const std::filesystem::path path(argv[1]);
#else
        const auto path=std::filesystem::u8path(argv[1]);
#endif
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("journal does not exist");
        FileJournal file(path);
        State state;
        if (!file.Load(state)) throw std::runtime_error("empty journal");
        std::cout<<"identity: "<<state.identity<<"\nrevision: "<<state.revision
                 <<"\nopenings_paused: "<<state.openings_paused<<"\nactivation_ms: "<<state.activation_ms
                 <<"\nnext_intent_id: "<<state.next_intent_id<<"\nsource_floor_ms: "<<state.source_floor_ms
                 <<"\nrecent_dedup_keys: "<<state.closed_sources.size()<<"\ncapacity_pause: "<<state.capacity_pause
                 <<"\nlegacy_halt: "<<state.halt_reason<<'\n';
        for (const auto& b:state.subscriptions)
            std::cout<<"SUBSCRIPTION rule="<<b.rule<<" activation_ms="<<b.time_ms<<" sequence="<<b.sequence<<'\n';
        for (const auto& link:state.links) {
            std::cout<<"LINK rule="<<link.rule<<" source="<<link.source.login<<" position="<<link.source.position
                     <<" source_symbol="<<link.source.symbol<<" target_symbol="<<link.follower_symbol
                     <<" target_position="<<link.follower_position<<" phase="<<static_cast<unsigned>(link.phase)
                     <<" source_remaining_ext="<<link.source_remaining<<" target_remaining_ext="<<link.follower_remaining
                     <<" exit_requested="<<link.exit_requested<<" reason="<<link.reason;
            if (link.opening_rule) std::cout<<" opening_target="<<link.opening_rule->follower_login
                <<" opening_mode="<<static_cast<unsigned>(link.opening_rule->mode)
                <<" opening_parameter="<<link.opening_rule->parameter;
            else std::cout<<" opening_rule=legacy_terminal_unknown";
            std::cout<<'\n';
        }
        for (const auto& i:state.intents)
            std::cout<<"INTENT id="<<i.id<<" link="<<i.link<<" close="<<i.close<<" login="<<i.follower_login
                     <<" position="<<i.follower_position<<" side="<<static_cast<unsigned>(i.side)
                     <<" symbol="<<i.symbol<<" volume_ext="<<i.units<<" state="<<static_cast<unsigned>(i.state)
                     <<" request="<<i.request_id<<" ret="<<i.retcode<<" result_position="<<i.result_position
                     <<" filled_ext="<<i.filled<<" equity_source="<<i.equity.source
                     <<" equity_target="<<i.equity.follower<<" equity_digits="<<i.equity.common_digits
                     <<" source_query_ms="<<i.equity.source_query_ms<<" target_query_ms="<<i.equity.follower_query_ms<<'\n';
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

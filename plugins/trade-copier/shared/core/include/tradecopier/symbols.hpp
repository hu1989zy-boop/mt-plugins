#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tradecopier {
// MetaQuotes name-mask subset: case-sensitive literals, '*', comma and '!'.
// Every exclusion wins; no PCRE syntax, implicit prefix stripping or fuzzy match.
class SymbolMask {
public:
    explicit SymbolMask(const std::string&);
    bool Matches(const std::string&) const;
private:
    struct Term { std::string pattern; bool exclude; };
    std::vector<Term> terms_;
};
bool ValidateSymbolRule(const std::string& mask,const std::string& suffix,std::string& error);
struct SymbolSpec {
    std::string name,basis,currency_base,currency_profit,currency_margin,isin;
    double contract_size=0, strike=0, face_value=0;
    unsigned calc_mode=0,digits=0,options_mode=0;
    std::int64_t start=0,expiration=0;
    bool buy=false,sell=false;
    // Zero preserves adapters that do not expose this metadata. An adapter
    // must provide positive values for contracts priced by tick value/size.
    double tick_size=0,tick_value=0;
};
struct SymbolResolution { std::string symbol,error; };
std::string SourceBaseSymbol(const SymbolSpec&,const std::string& suffix);
bool SameSymbolContract(const SymbolSpec&,const SymbolSpec&);
SymbolResolution ResolveTargetSymbol(const SymbolSpec& source,const std::vector<SymbolSpec>& targets,
                                     bool automatic,const std::string& source_suffix,bool buy);
// Removes only a validated old executor identity's trailing symbol filter.
// Account/server/API identity remains mandatory across a filter change.
std::string StableExecutorIdentity(const std::string&);
}

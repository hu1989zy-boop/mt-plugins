#include "tradecopier/symbols.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace tradecopier {
namespace {
bool Glob(const std::string& pattern,const std::string& text) {
    std::size_t p=0,t=0,star=std::string::npos,retry=0;
    while (t<text.size()) {
        if (p<pattern.size() && pattern[p]=='*') { star=p++; retry=t; }
        else if (p<pattern.size() && pattern[p]==text[t]) { ++p; ++t; }
        else if (star!=std::string::npos) { p=star+1; t=++retry; }
        else return false;
    }
    while (p<pattern.size() && pattern[p]=='*') ++p;
    return p==pattern.size();
}
bool Prefix(const std::string& name,const std::string& base) {
    return !base.empty() && name.compare(0,base.size(),base)==0;
}
}
SymbolMask::SymbolMask(const std::string& mask) {
    if (mask.empty() || mask.size()>4096) throw std::invalid_argument("品种掩码必须为 1–4096 字节");
    bool include=false;
    for (std::size_t start=0;start<mask.size();) {
        const auto end=mask.find(',',start);
        auto term=mask.substr(start,end==std::string::npos?end:end-start);
        start=end==std::string::npos?mask.size():end+1;
        const auto first=term.find_first_not_of(' ');
        if (first==std::string::npos) continue;
        term=term.substr(first,term.find_last_not_of(' ')-first+1);
        if (term.size()>255 || terms_.size()>=64) throw std::invalid_argument("品种掩码最多 64 项，每项 255 字节");
        bool exclude=term.front()=='!';
        if (exclude) term.erase(0,term.find_first_not_of('!'));
        if (term.empty()) throw std::invalid_argument("排除条件缺少品种掩码");
        for (unsigned char c:term) if (c<32 || c==127)
            throw std::invalid_argument("品种掩码不能包含控制字符");
        terms_.push_back({term,exclude}); include=include || !exclude;
    }
    if (!include) throw std::invalid_argument("品种掩码需要包含条件，例如 *,!EUR*");
}
bool SymbolMask::Matches(const std::string& name) const {
    if (name.empty()) return false;
    bool found=false;
    for (const auto& term:terms_) if (Glob(term.pattern,name)) {
        if (term.exclude) return false;
        found=true;
    }
    return found;
}
bool ValidateSymbolRule(const std::string& mask,const std::string& suffix,std::string& error) {
    try {
        (void)SymbolMask(mask);
        if (suffix.size()>64) throw std::invalid_argument("源后缀最多 64 字节");
        for (unsigned char c:suffix) if (c<=32 || c==127 || c=='*' || c=='!' || c==',' || c=='?' || c=='/' || c=='\\')
            throw std::invalid_argument("源后缀必须是精确文字，不能包含空格、路径或通配符");
        error.clear(); return true;
    } catch (const std::invalid_argument& e) { error=e.what(); return false; }
}
std::string SourceBaseSymbol(const SymbolSpec& source,const std::string& suffix) {
    if (!suffix.empty()) {
        if (source.name.size()<=suffix.size() || source.name.compare(source.name.size()-suffix.size(),suffix.size(),suffix)!=0)
            return {};
        return source.name.substr(0,source.name.size()-suffix.size());
    }
    // Basis is used only when it is a name prefix, never to map unrelated aliases.
    if (source.basis!=source.name && Prefix(source.name,source.basis)) return source.basis;
    if (!source.currency_base.empty() && !source.currency_profit.empty()) {
        const auto pair=source.currency_base+source.currency_profit;
        if (Prefix(source.name,pair)) return pair;
    }
    return source.name; // Non-FX names need an explicit source suffix if metadata is insufficient.
}
bool SameSymbolContract(const SymbolSpec& a,const SymbolSpec& b) {
    return std::isfinite(a.contract_size) && a.contract_size>0 && a.contract_size==b.contract_size &&
        a.calc_mode==b.calc_mode && a.digits==b.digits && a.currency_base==b.currency_base &&
        a.currency_profit==b.currency_profit && a.currency_margin==b.currency_margin &&
        a.isin==b.isin && a.start==b.start && a.expiration==b.expiration &&
        a.options_mode==b.options_mode && a.strike==b.strike && a.face_value==b.face_value &&
        std::isfinite(a.tick_size) && a.tick_size>=0 && a.tick_size==b.tick_size &&
        std::isfinite(a.tick_value) && a.tick_value>=0 && a.tick_value==b.tick_value;
}
SymbolResolution ResolveTargetSymbol(const SymbolSpec& source,const std::vector<SymbolSpec>& targets,
                                     bool automatic,const std::string& suffix,bool buy) {
    const auto base=automatic?SourceBaseSymbol(source,suffix):source.name;
    if (base.empty()) return {{},"源品种与配置的源后缀不一致："+source.name};
    std::set<std::string> found;
    for (const auto& target:targets) {
        if (!(buy?target.buy:target.sell) || !SameSymbolContract(source,target)) continue;
        if (automatic ? !Prefix(target.name,base) : target.name!=source.name) continue;
        // A configured basis that names a different instrument is not a suffix.
        if (automatic && !target.basis.empty() && target.basis!=source.basis && target.basis!=base && target.basis!=target.name)
            continue;
        found.insert(target.name);
    }
    if (found.empty()) return {{},"没有可交易且合约一致的目标品种："+base};
    if (found.size()>1) {
        std::string error="品种匹配冲突："+base+" → ";
        for (const auto& name:found) { if (error.size()>1500) { error+=" …"; break; } error+=name+" "; }
        return {{},error};
    }
    return {*found.begin(),{}};
}
std::string StableExecutorIdentity(const std::string& value) {
    const auto marker=value.find(";symbol=");
    if (marker==std::string::npos || marker+8==value.size()) return value;
    const auto base=value.substr(0,marker);
    std::size_t at=0;
    for (const auto* label:{"server=",";api=",";app="}) {
        const std::string key=label;
        if (base.compare(at,key.size(),key)!=0) return value;
        at+=key.size(); const auto first=at;
        while (at<base.size() && base[at]>='0' && base[at]<='9') ++at;
        if (at==first) return value;
    }
    return at==base.size()?base:value;
}
}

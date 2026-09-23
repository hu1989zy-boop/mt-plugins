#include "tradecopier/management.hpp"
#include <stdexcept>
#include <unordered_map>

namespace tradecopier::management {
namespace {
struct Writer {
    std::string data;
    void Number(Id n) { for (unsigned i=0;i<8;++i) data.push_back(static_cast<char>((n>>(8*i))&255)); }
    void Text(const std::string& s) { Number(s.size()); data+=s; }
    void Table(const Rows& rows) {
        if (rows.size()>kVisibleRows) throw std::runtime_error("too many management rows");
        Number(rows.size());
        for (const auto& row:rows) { Number(row.size()); for (const auto& s:row) Text(s); }
    }
    std::string Finish() { if (data.size()>kMaxMessage) throw std::runtime_error("management response too large"); return data; }
};
struct Reader {
    const std::string& data; std::size_t pos=0;
    explicit Reader(const std::string& s):data(s) {
        if (s.size()>kMaxMessage) throw std::runtime_error("management message too large");
    }
    Id Number(Id max=UINT64_MAX) {
        if (data.size()-pos<8) throw std::runtime_error("truncated management message");
        Id n=0; for (unsigned i=0;i<8;++i) n|=Id(static_cast<unsigned char>(data[pos++]))<<(8*i);
        if (n>max) throw std::runtime_error("management field out of range");
        return n;
    }
    std::string Text(std::size_t max=kMaxConfigBytes) {
        const auto size=static_cast<std::size_t>(Number(max));
        if (size>data.size()-pos) throw std::runtime_error("truncated management text");
        auto s=data.substr(pos,size); pos+=size; return s;
    }
    Rows Table() {
        Rows rows; auto count=Number(kVisibleRows);
        for (Id i=0;i<count;++i) { std::vector<std::string> row; auto columns=Number(16);
            for (Id c=0;c<columns;++c) row.push_back(Text(4096));
            rows.push_back(std::move(row)); }
        return rows;
    }
    void End() { if (pos!=data.size()) throw std::runtime_error("trailing management data"); }
};
std::string Lots(Units n) {
    auto fraction=std::to_string(n%kUnitsPerLot+kUnitsPerLot).substr(1);
    while (fraction.size()>2 && fraction.back()=='0') fraction.pop_back();
    return std::to_string(n/kUnitsPerLot)+"."+fraction;
}
}
std::string EncodeRequest(const Request& v) {
    Writer w; w.Number(2); w.Number(static_cast<unsigned>(v.operation)); w.Text(v.session);
    w.Number(v.revision); w.Number(v.intent); w.Text(v.config_json); w.Text(v.expected_config); return w.Finish();
}
Request DecodeRequest(const std::string& data) {
    Reader r(data); if (r.Number()!=2) throw std::runtime_error("management protocol version mismatch");
    Request v; v.operation=static_cast<Operation>(r.Number(5)); v.session=r.Text(128);
    v.revision=r.Number(); v.intent=r.Number(); v.config_json=r.Text(); v.expected_config=r.Text(); r.End(); return v;
}
std::string EncodeResponse(const Response& v) {
    Writer w; w.Number(2); w.Number(v.ok); w.Text(v.message); w.Text(v.session); w.Text(v.identity);
    w.Text(v.config_path); w.Text(v.journal_path); w.Number(v.revision); w.Number(v.paused);
    w.Text(v.halt); w.Text(v.apply_block); w.Text(v.config_json);
    w.Number(v.links_total); w.Number(v.intents_total); w.Number(v.active); w.Number(v.review); w.Number(v.pending); w.Number(v.exiting);
    w.Table(v.links); w.Table(v.intents); return w.Finish();
}
Response DecodeResponse(const std::string& data) {
    Reader r(data); if (r.Number()!=2) throw std::runtime_error("management protocol version mismatch");
    Response v; v.ok=r.Number(1)!=0; v.message=r.Text(); v.session=r.Text(128); v.identity=r.Text(1024);
    v.config_path=r.Text(32768*4); v.journal_path=r.Text(32768*4); v.revision=r.Number(); v.paused=r.Number(1)!=0;
    v.halt=r.Text(); v.apply_block=r.Text(); v.config_json=r.Text();
    v.links_total=r.Number(execution::Engine::kOpeningLinkLimit); v.intents_total=r.Number(100000);
    v.active=r.Number(execution::Engine::kOpeningLinkLimit); v.review=r.Number(execution::Engine::kOpeningLinkLimit); v.pending=r.Number(100000); v.exiting=r.Number(execution::Engine::kOpeningLinkLimit);
    v.links=r.Table(); v.intents=r.Table(); r.End(); return v;
}
Response Snapshot(const execution::Engine& engine) {
    using namespace execution;
    const auto& s=engine.state(); Response v;
    v.identity=s.identity; v.revision=s.revision; v.paused=s.openings_paused; v.halt=engine.warning();
    v.apply_block=!engine.started()?"执行器尚未就绪":engine.storage_degraded()?"账本不可写，暂不能提交订阅配置；可核实旧仓继续管理":""; v.config_json=s.config_json;
    v.links_total=s.links.size(); v.intents_total=s.intents.size();
    std::unordered_map<std::size_t,Id> targets;
    for (const auto& i:s.intents) {
        targets[i.link]=i.follower_login;
        if (i.state==IntentState::Prepared || i.state==IntentState::Submitted || i.state==IntentState::Review) ++v.pending;
    }
    const char* phases[]={"等待源成交", "开仓确认中", "跟随中", "平仓确认中", "已关闭", "人工全平", "待核对", "已跳过"};
    for (const auto& link:s.links) {
        if (link.exit_requested && link.phase!=Phase::Closed && link.phase!=Phase::ManualClosed && link.phase!=Phase::Skipped) ++v.exiting;
        if (link.phase==Phase::Review) ++v.review;
        else if (link.phase==Phase::Active || link.phase==Phase::Opening || link.phase==Phase::Closing || link.phase==Phase::Candidate) ++v.active;
    }
    // Nonterminal rows first. Totals remain exact when the display is bounded.
    for (unsigned pass=0;pass<2;++pass) for (std::size_t n=s.links.size();n>0 && v.links.size()<kVisibleRows;--n) {
        const auto& l=s.links[n-1];
        const bool terminal=l.phase==Phase::Closed || l.phase==Phase::ManualClosed || l.phase==Phase::Skipped;
        if (terminal!=(pass==1)) continue;
        const auto target=l.opening_rule?l.opening_rule->follower_login:
            (targets.count(n-1)?targets.at(n-1):0);
        std::string detail=l.reason;
        if (l.exit_requested) detail="关系退出中；"+detail;
        if (!detail.empty()) detail+="; ";
        if (l.opening_rule) {
            const char* modes[]={"手数比例", "净值比例", "固定手数"};
            detail+="开仓规则：" + std::string(modes[static_cast<unsigned>(l.opening_rule->mode)]) +
                " " + l.opening_rule->parameter;
        } else detail+="旧版已结束映射：未记录开仓规则";
        v.links.push_back({std::to_string(l.rule),std::to_string(l.source.login),std::to_string(target),
            std::to_string(l.source.position),std::to_string(l.follower_position),l.source.symbol+" → "+(l.follower_symbol.empty()?"待匹配":l.follower_symbol),
            l.source.side==Side::Buy?"Buy":"Sell",Lots(l.source_remaining),Lots(l.follower_remaining),
            l.exit_requested && !terminal?"退出中":phases[static_cast<unsigned>(l.phase)],detail.substr(0,4096)});
    }
    const char* states[]={"已准备", "待成交确认", "已确认", "被拒绝", "待核对"};
    for (std::size_t n=s.intents.size();n>0 && v.intents.size()<kVisibleRows;--n) {
        const auto& i=s.intents[n-1];
        v.intents.push_back({std::to_string(i.id),std::to_string(i.follower_login),i.close?"平仓":"开仓",
            std::to_string(i.close?i.follower_position:i.result_position),Lots(i.units),Lots(i.filled),
            states[static_cast<unsigned>(i.state)],std::to_string(i.request_id),std::to_string(i.retcode)});
    }
    return v;
}
} // namespace tradecopier::management

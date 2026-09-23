#include "tradecopier/config.hpp"
#include "tradecopier/symbols.hpp"
#include <cmath>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

using namespace tradecopier;

namespace {
int checks = 0;

void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

CopierConfig Example() {
    CopierConfig config;
    config.relationships = {
        {1, 10001, 20001, false, SizingMode::LotRatio, "0.50"},
        {2, 10001, 20002, false, SizingMode::EquityRatio, "1.00"},
        {3, 10002, 20001, false, SizingMode::FixedLots, "0.20"}
    };
    return config;
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const auto found = text.find(from);
    Check(found != std::string::npos, "test mutation anchor exists");
    text.replace(found, from.size(), to);
    return text;
}

std::string RemoveLinesContaining(std::string text, const std::string& field) {
    std::size_t found = 0;
    while ((found = text.find(field)) != std::string::npos) {
        const auto start = text.rfind('\n', found);
        const auto end = text.find('\n', found);
        text.erase(start == std::string::npos ? 0 : start + 1,
                   end == std::string::npos ? std::string::npos : end - (start == std::string::npos ? 0 : start + 1) + 1);
    }
    return text;
}

void Reject(const std::string& json, const char* description) {
    CopierConfig output = Example();
    output.enabled = true;
    const auto unchanged = WriteConfigJson(output);
    std::string error;
    Check(!ParseConfigJson(json, output, error), description);
    Check(!error.empty(), "rejection supplies an error");
    Check(WriteConfigJson(output) == unchanged, "failed parse preserves all output fields");
}

void RoundTripTests(const std::string& example_file) {
    auto config = Example();
    std::string error = "old error";
    Check(ValidateConfig(config, error) && error.empty(), "many-to-many config validates and clears error");
    auto text = WriteConfigJson(config);
    CopierConfig decoded;
    Check(ParseConfigJson(text, decoded, error), "canonical JSON parses");
    Check(!decoded.enabled && decoded.relationships.size() == 3, "default config disabled with three rules");
    Check(decoded.relationships[0].parameter == "0.50" &&
          decoded.relationships[1].mode == SizingMode::EquityRatio &&
          decoded.relationships[2].mode == SizingMode::FixedLots,
          "all sizing modes and decimal strings round trip");
    Check(WriteConfigJson(decoded) == text, "canonical roundtrip is stable");
    Check(text.find("\"copy_stop_loss\": false") != std::string::npos &&
          text.find("\"copy_pending_orders\": false") != std::string::npos &&
          text.find("\"equity_snapshot\": \"before_follower_submission\"") != std::string::npos &&
          text.find("logical_example_not_deployable") != std::string::npos,
          "writer includes complete fixed policies and local example status");

    config.enabled = true;
    config.relationships[0].enabled = true;
    config.relationships[0].id = std::numeric_limits<Id>::max();
    config.relationships[0].source_login = 9007199254740993ULL;
    Check(ParseConfigJson(WriteConfigJson(config), decoded, error) && decoded.enabled &&
          decoded.relationships[0].enabled &&
          decoded.relationships[0].id == std::numeric_limits<Id>::max() &&
          decoded.relationships[0].source_login == 9007199254740993ULL,
          "enabled flags and full 64-bit IDs survive without floating point");

    text = RemoveLinesContaining(WriteConfigJson(Example()), "\"enabled\"");
    text = RemoveLinesContaining(text, "\"status\"");
    Check(ParseConfigJson(text, decoded, error) && !decoded.enabled && !decoded.relationships[0].enabled,
          "omitted enabled flags default false and status is optional");
    text = Replace(WriteConfigJson(Example()), "\"platform\": \"mt5\"", "\"platf\\u006frm\": \"mt\\u0035\"");
    text = Replace(text, "\"0.50\"", "\"0\\u002e50\"");
    Check(ParseConfigJson(text, decoded, error) && decoded.relationships[0].parameter == "0.50",
          "valid Unicode escapes decode in keys and decimal string values");

    CopierConfig empty;
    Check(ParseConfigJson(WriteConfigJson(empty), decoded, error) && decoded.relationships.empty(),
          "empty disabled configuration round trips");

    if (!example_file.empty()) {
        std::ifstream stream(example_file, std::ios::binary);
        Check(stream.good(), "repository schema example file readable");
        const std::string example((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        Check(ParseConfigJson(example, decoded, error), "current repository example parses without migration");
        Check(WriteConfigJson(decoded) == WriteConfigJson(Example()), "repository example matches schema contract");
    }
}

void PolicyTests() {
    std::string error;
    VolumePolicy result;
    Check(MakePolicy({1, 1, 2, false, SizingMode::LotRatio, "1.50"}, result, error) &&
          result.factor.numerator == 3 && result.factor.denominator == 2,
          "multiplier is exact rational");
    Check(MakePolicy({1, 1, 2, false, SizingMode::EquityRatio, "0.001"}, result, error) &&
          result.factor.numerator == 1 && result.factor.denominator == 1000,
          "equity coefficient is exact rational");
    Check(MakePolicy({1, 1, 2, false, SizingMode::FixedLots, "0.20"}, result, error) &&
          result.fixed_units == 20000000, "fixed lots convert into business units");
    Check(MakePolicy({1, 1, 2, false, SizingMode::FixedLots, "0.000000010"}, result, error) &&
          result.fixed_units == 1, "trailing zero beyond eighth decimal is exact and accepted");
    const auto prior = result;
    for (const auto* parameter : {"0.000000001", "0.123456789", "184467440737.09551616", "0", "-1", "1e-2"}) {
        Check(!MakePolicy({1, 1, 2, false, SizingMode::FixedLots, parameter}, result, error),
              "invalid, excess precision or overflowing fixed amount rejected");
        Check(result.mode == prior.mode && result.fixed_units == prior.fixed_units,
              "failed policy conversion preserves output");
    }
    Check(MakePolicy({1, 1, 2, false, SizingMode::FixedLots, "184467440737.09551615"}, result, error) &&
          result.fixed_units == std::numeric_limits<Units>::max(), "largest exact business-unit quantity accepted");
    Check(!MakePolicy({1, 1, 2, false, static_cast<SizingMode>(99), "1"}, result, error),
          "unknown enum rejected before serialization");
    Check(!MakePolicy({1, 1, 2, false, SizingMode::LotRatio, std::string(65, '0') + "1"}, result, error),
          "unbounded leading-zero parameter text rejected");
}

void SchemaRejectionTests() {
    const auto base = WriteConfigJson(Example());
    Reject(Replace(base, "\"schema_version\": 2", "\"schema_version\": 1"), "old schema rejected");
    Reject(Replace(base, "\"schema_version\": 2", "\"schema_version\": \"2\""), "string schema version rejected");
    Reject(Replace(base, "\"schema_version\": 2", "\"schema_version\": 2.0"), "noncanonical noninteger schema token rejected");
    Reject(Replace(base, "\"enabled\": false", "\"enabled\": \"false\""), "string boolean rejected");
    Reject(Replace(base, "\"source_login\": \"10001\"", "\"source_login\": 10001"), "numeric account ID rejected");
    Reject(Replace(base, "\"source_login\": \"10001\"", "\"source_login\": \"18446744073709551616\""),
           "overflowing account ID rejected");
    Reject(Replace(base, "\"source_login\": \"10001\"", "\"source_login\": \"0\""), "zero account ID rejected");
    Reject(Replace(base, "\"source_login\": \"10001\"", "\"source_login\": \"+1\""), "signed ID rejected");
    Reject(Replace(base, "\"source_login\": \"10001\"", "\"source_login\": \"１２３\""), "non-ASCII numeral ID rejected");
    Reject(Replace(base, "\"multiplier\": \"0.50\"", "\"multiplier\": 0.50"), "numeric sizing parameter rejected");
    Reject(Replace(base, "\"multiplier\": \"0.50\"", "\"coefficient\": \"0.50\""), "mode-mismatched parameter rejected");
    Reject(Replace(base, "\"multiplier\": \"0.50\"", "\"multiplier\": \"0.50\", \"coefficient\": \"1\""),
           "extra parameter from another mode rejected");
    Reject(Replace(base, "\"mode\": \"multiplier\"", "\"mode\": \"balance_ratio\""), "unknown mode rejected");
    Reject(Replace(base, "\"lots_per_source_trade\": \"0.20\"", "\"lots_per_source_trade\": \"0.000000001\""),
           "sub-business-unit fixed precision rejected at import");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"EUR\""), "unsupported currency rejected");
    Reject(Replace(base, "\"copy_pending_orders\": false", "\"copy_pending_orders\": true"), "unsupported pending policy rejected");
    Reject(Replace(base, "\"copy_stop_loss\": false", "\"copy_stop_loss\": true"), "unsupported SL policy rejected");
    Reject(Replace(base, "\"new_positions_only\": true", "\"new_positions_only\": false"), "old-position copying rejected");
    Reject(Replace(base, "\"equity_snapshot\": \"before_follower_submission\"",
                        "\"equity_snapshot\": \"source_trade_open_event_time\""),
           "old exact source-execution equity timing rejected instead of silently reinterpreted");
    Reject(Replace(base, "logical_example_not_deployable", "deployed"), "false deployed status rejected");
    Reject(Replace(base, "{", "{\"extra\": true,"), "unknown root field rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"USD\", \"extra\": false"),
           "unknown nested policy field rejected");
    Reject(RemoveLinesContaining(base, "\"platform\""), "missing required global field rejected");
    Reject(RemoveLinesContaining(base, "\"copy_stop_loss\""), "missing required fixed policy rejected");
    Reject(RemoveLinesContaining(base, "\"source_login\""), "missing required rule field rejected");
    Reject(Replace(base, "\"multiplier\": \"0.50\"", "\"multiplier\": \"0.50\", \"multiplier\": \"2\""),
           "duplicate rule key rejected");
    Reject(Replace(base, "\"enabled\": false", "\"enabled\": false, \"enabled\": true"), "duplicate root key rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"USD\", \"curr\\u0065ncy\": \"USD\""),
           "duplicate decoded Unicode key rejected");
}

void JsonRejectionTests() {
    const auto base = WriteConfigJson(Example());
    Reject(base + "true", "trailing JSON value rejected");
    Reject(base + std::string(1, '\0'), "trailing NUL rejected");
    Reject(Replace(base, "\"schema_version\": 2", "\"schema_version\": 02"), "leading zero number rejected");
    Reject(Replace(base, "\"schema_version\": 2", "\"schema_version\": 2e"), "incomplete exponent rejected");
    Reject(Replace(base, "\"schema_version\": 2", "\"schema_version\": -"), "missing number digit rejected");
    Reject(Replace(base, "\"schema_version\": 2", "\"schema_version\": 2."), "missing fractional digit rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"US\nD\""), "raw control byte in string rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"\\x41\""), "unknown escape rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"\\uD800\""), "unpaired high surrogate rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"\\uDC00\""), "unpaired low surrogate rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"\\uD800\\u0041\""), "invalid surrogate pair rejected");
    Reject(Replace(base, "\"currency\": \"USD\"", "\"currency\": \"\\u12G4\""), "nonhex Unicode escape rejected");
    Reject(Replace(base, "\"USD\"", std::string("\"") + char(0xc0) + char(0xaf) + "\""), "overlong UTF-8 rejected");
    Reject(Replace(base, "\"USD\"", std::string("\"") + char(0xed) + char(0xa0) + char(0x80) + "\""),
           "UTF-8 encoded surrogate rejected");
    Reject(Replace(base, "\"USD\"", std::string("\"") + char(0xf4) + char(0x90) + char(0x80) + char(0x80) + "\""),
           "UTF-8 code point above Unicode range rejected");
    Reject(Replace(base, "\"USD\"", std::string("\"") + char(0xe4) + "x\""), "bad UTF-8 continuation rejected");
    Reject(Replace(base, "\"USD\"", "\"USD\",}"), "trailing object comma rejected");
    Reject(std::string(40, '[') + "0" + std::string(40, ']'), "excessive nesting rejected");
    Reject(std::string(kMaxConfigBytes + 1, ' '), "oversize configuration rejected before parsing");
    Reject("{", "truncated object rejected");
    Reject("\"\\u12", "truncated Unicode escape rejected without reading beyond input");
    Reject(std::string("\"") + char(0xf0), "truncated raw UTF-8 sequence rejected");
    Reject("[1,]", "trailing array comma rejected");
    Reject("null", "nonobject root rejected");
    std::string too_many_nodes = "[";
    for (std::size_t index = 0; index < 20000; ++index) too_many_nodes += "0,";
    too_many_nodes += "0]";
    Reject(too_many_nodes, "JSON node count bounded independently of byte size and nesting");

    // These well-formed Unicode strings fail schema validation, not parsing.
    for (const auto* key : {"\\uD83D\\uDE00", "中文", "quote\\\"slash\\\\line\\n"}) {
        CopierConfig output;
        std::string error;
        Check(!ParseConfigJson(Replace(base, "{", std::string("{\"") + key + "\": true,"), output, error) &&
              error.find("config: unknown field") == 0,
              "valid Unicode and escaped strings reach schema validation");
    }
}

void RelationValidationTests() {
    std::string error;
    auto config = Example();
    config.relationships[0].source_login = config.relationships[0].follower_login;
    Check(!ValidateConfig(config, error), "self-follow rejected");
    config = Example();
    config.relationships.push_back(config.relationships.front());
    config.relationships.back().id = 9;
    Check(!ValidateConfig(config, error), "duplicate source/target pair rejected even while disabled");
    config = Example();
    config.relationships.back().id = config.relationships.front().id;
    Check(!ValidateConfig(config, error), "duplicate relationship ID rejected");
    config = Example();
    config.relationships.push_back({4, 20001, 10001, false, SizingMode::FixedLots, "0.10"});
    Check(!ValidateConfig(config, error), "disabled reciprocal relationship cannot create later cycle");
    bool threw = false;
    try { (void)WriteConfigJson(config); } catch (const std::invalid_argument&) { threw = true; }
    Check(threw, "writer refuses invalid graph instead of creating invalid file");
    config = Example();
    config.relationships.front().id = 0;
    Check(!ValidateConfig(config, error), "zero relationship ID rejected");
    config.relationships.assign(kMaxRelationships + 1, Example().relationships.front());
    Check(!ValidateConfig(config, error), "relationship count bounded before graph processing");

    config.relationships.clear();
    for (std::size_t index = 0; index < kMaxRelationships; ++index)
        config.relationships.push_back({index + 1, 10001, 20000 + index, false, SizingMode::LotRatio, "1"});
    Check(ValidateConfig(config, error), "maximum allowed rule count validates");
    auto text = WriteConfigJson(config);
    Check(text.size() < kMaxConfigBytes, "maximum rule count stays within byte limit");
    CopierConfig parsed;
    Check(ParseConfigJson(text, parsed, error) && parsed.relationships.size() == kMaxRelationships,
          "maximum rule count fits parser node limit and round trips");
    text = Replace(text, "\n  ],", ",\n    {\"relationship_id\": \"1001\", \"source_login\": \"10001\", "
                   "\"follower_login\": \"30001\", \"mode\": \"multiplier\", \"multiplier\": \"1\"}\n  ],");
    Reject(text, "1001 imported relationships rejected even when all IDs and pairs are unique");
}

void SymbolTests() {
    for (const auto& item:std::vector<std::pair<std::string,std::string>>{
        {"*","XAUUSDp"},{"XAUUSD*","XAUUSD.p"},{"*USD*","EURUSD"},{"X*U*D*","XAUUSDp"},
        {"XAUUSD.p","XAUUSD.p"},{" EUR*, XAU* , !EURJPY* ","XAUUSDp"},{"*,!*JPY*","EURUSD"},
        {"*,!!EUR*","XAUUSD"},{",,*, ,","BTCUSD"},{"XAUUSD*","XAUUSD"}})
        Check(SymbolMask(item.first).Matches(item.second),"MetaQuotes literal/star/list mask includes expected symbol");
    for (const auto& item:std::vector<std::pair<std::string,std::string>>{
        {"XAUUSD","XAUUSDp"},{"XAUUSD.p","XAUUSDxp"},{"xau*","XAUUSD"},
        {"*,!XAU*","XAUUSDp"},{"!XAU*,*","XAUUSDp"},{"XAU*,EUR*","BTCUSD"},{"X*D","XAUUSDp"}})
        Check(!SymbolMask(item.first).Matches(item.second),"exact names, case and exclusions are respected");
    Check(SymbolMask("EUR?").Matches("EUR?") && !SymbolMask("EUR?").Matches("EURp"), "question mark is a literal as in the SDK helper, not PCRE");
    std::string error;
    for (const auto* mask:{"",",,","!EUR*","*,!","EUR\nUSD"})
        Check(!ValidateSymbolRule(mask,"",error) && !error.empty(),"invalid or unsupported mask has a clear error");
    Check(!ValidateSymbolRule(std::string(256,'x'),"",error),"mask term length bounded like SDK helper");
    Check(!ValidateSymbolRule("*","*p",error),"suffix is exact text, not a mask");
    SymbolSpec source; source.name="XAUUSDp"; source.currency_base="XAU"; source.currency_profit="USD";
    source.currency_margin="USD"; source.contract_size=100; source.digits=2; source.buy=source.sell=true;
    auto dot=source; dot.name="XAUUSD.p";
    Check(SourceBaseSymbol(source,"")=="XAUUSD" && SourceBaseSymbol(dot,"")=="XAUUSD", "currency metadata identifies base across p and .p suffixes");
    Check(ResolveTargetSymbol(source,{dot},true,"",true).symbol==dot.name,"unique tradable .p maps automatically");
    Check(ResolveTargetSymbol(dot,{source},true,"",false).symbol==source.name,"reverse suffix mapping works for sells");
    Check(ResolveTargetSymbol(source,{source,dot},true,"",true).symbol.empty(),"exact-name candidate never wins an ambiguous automatic match");
    Check(ResolveTargetSymbol(source,{source,dot},true,"",true).error.find("冲突")!=std::string::npos,"ambiguity gives a conflict message");
    Check(ResolveTargetSymbol(source,{source,dot},false,"",true).symbol==source.name,"legacy exact-name policy stays exact");
    auto disabled=source; disabled.buy=false;
    Check(ResolveTargetSymbol(source,{disabled,dot},true,"",true).symbol==dot.name,"unavailable side excluded from candidate set");
    Check(!ResolveTargetSymbol(source,{disabled,dot},true,"",false).error.empty(),"side permissions checked separately");
    Check(!ResolveTargetSymbol(source,{},true,"",true).error.empty(),"no candidate is explicit, never a guessed symbol");
    for (unsigned n=0;n<10;++n) {
        auto other=dot;
        switch(n) {
        case 0:other.contract_size=1;break; case 1:other.calc_mode=1;break; case 2:other.digits=3;break;
        case 3:other.currency_profit="EUR";break; case 4:other.isin="OTHER";break;
        case 5:other.expiration=100;break; case 6:other.options_mode=1;break;
        case 7:other.strike=100;break; case 8:other.face_value=100;break; case 9:other.basis="OTHER";break;
        }
        Check(ResolveTargetSymbol(source,{other},true,"",true).symbol.empty(),"different effective contracts or instrument identities cannot be copied");
    }
    auto index=source; index.name="US30p"; index.currency_base=index.currency_profit="USD";
    auto target=index; target.name="US30.p";
    Check(ResolveTargetSymbol(index,{target},true,"",true).symbol.empty(),"missing non-FX base metadata does not guess a source suffix");
    Check(ResolveTargetSymbol(index,{target},true,"p",true).symbol==target.name,"explicit source suffix supports non-FX bases");
    index.basis="US30";
    Check(ResolveTargetSymbol(index,{target},true,"",true).symbol==target.name,"SDK basic symbol metadata supports indices");
    Check(!ResolveTargetSymbol(source,{dot},true,".p",true).error.empty(),"incorrect exact source suffix blocks the mapping");
    auto config=Example(); config.relationships[0].auto_symbol=true;
    config.relationships[0].symbol_mask="*,!EUR*"; config.relationships[0].source_suffix="p";
    const auto json=WriteConfigJson(config); CopierConfig loaded;
    Check(json.find("\"schema_version\": 3")!=std::string::npos && ParseConfigJson(json,loaded,error),"symbol configuration uses schema 3 and imports");
    Check(WriteConfigJson(loaded)==json && loaded.relationships[0].auto_symbol && loaded.relationships[0].source_suffix=="p", "symbol routing settings roundtrip without changing defaults in other rules");
    Reject(Replace(json,"\"schema_version\": 3","\"schema_version\": 2"),"new symbol fields cannot be silently read as old schema");
    Reject(Replace(json,"\"auto_symbol\": true","\"auto_symbol\": \"true\""),"automatic selection is a strict boolean");
    Reject(Replace(json,"*,!EUR*","!*"),"invalid mask rejected by JSON import");
    for (auto& r:config.relationships) r.symbol_mask="*";
    Check(ValidateConfig(config,error),"automatic matching does not impose a gold-only restriction");
    Check(StableExecutorIdentity("server=1;api=6182;app=3101;symbol=XAUUSDp")=="server=1;api=6182;app=3101", "legacy symbol filter is not the server identity");
    Check(StableExecutorIdentity("server=2;api=6182;app=3101;symbol=*")!="server=1;api=6182;app=3101", "server identity remains mandatory");
    Check(StableExecutorIdentity("test;symbol=*")=="test;symbol=*", "arbitrary identities are not rewritten");
}

} // namespace

int main(int argc, char** argv) {
    RoundTripTests(argc > 1 ? argv[1] : "");
    PolicyTests();
    SymbolTests();
    SchemaRejectionTests();
    JsonRejectionTests();
    RelationValidationTests();
    std::cout << "PASS: " << checks << " strict schema, decimal, graph and JSON checks\n";
}

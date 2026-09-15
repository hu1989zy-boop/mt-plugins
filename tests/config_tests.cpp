#include "tradecopier/config.hpp"

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

} // namespace

int main(int argc, char** argv) {
    RoundTripTests(argc > 1 ? argv[1] : "");
    PolicyTests();
    SchemaRejectionTests();
    JsonRejectionTests();
    RelationValidationTests();
    std::cout << "PASS: " << checks << " strict schema, decimal, graph and JSON checks\n";
}

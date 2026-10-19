#include "tradecopier/config.hpp"
#include "tradecopier/symbols.hpp"

#include <initializer_list>
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace tradecopier {
namespace {

constexpr std::size_t kMaxJsonDepth = 32;
constexpr std::size_t kMaxJsonNodes = 20000;
constexpr std::size_t kMaxParameterLength = 64;

// This complete schema template is shared by validation and serialization so
// fixed global policies cannot diverge between imported and exported files.
const char* kSchemaTemplate = R"json({
  "schema_version": 2,
  "status": "logical_example_not_deployable",
  "platform": ")json" TRADECOPIER_PLATFORM_NAME R"json(",
  "enabled": false,
  "deployment": "server_api_dll",
  "management_ui": "standalone_windows",
  "server_scope": "same_live",
  "account_requirements": { "mode": "hedging", "currency": "USD" },
  "activation": {
    "scope": "per_relationship",
    "new_positions_only": true,
    "initial_positions": "ignore",
    "existing_position_increases": "ignore",
    "resume_catch_up": false
  },
  "trade_policy": {
    "direction": "same_as_source",
    "copy_pending_orders": false,
    "follow_positions_from_triggered_pending_orders": true,
    "copy_stop_loss": false,
    "copy_take_profit": false,
    "set_target_stop_loss_take_profit": false,
    "sync_actual_source_closes": true,
    "partial_close_basis": "target_actual_remaining_times_source_close_fraction",
    "target_manual_full_close": "terminate_mapping_without_reopen",
    "target_manual_partial_close": "continue_from_actual_remaining",
    "copy_plugin_generated_trades": false
  },
  "quantity_policy": {
    "equity_snapshot": "before_follower_submission",
    "record_equity_snapshot_times": true,
    "freeze_opening_parameters": true,
    "open_rounding": "down_to_target_step",
    "below_minimum_open": "skip_and_record",
    "above_maximum_open": "reject_and_record",
    "partial_close_rounding": "down_to_target_step",
    "invalid_partial_close_remainder": "pause_and_record",
    "source_full_close": "close_all_mapped_target_remaining"
  },
  "relationships": []
})json";

struct ConfigError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Json {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};

Json String(std::string text) {
    Json result;
    result.kind = Json::Kind::String;
    result.text = std::move(text);
    return result;
}

Json Boolean(bool value) {
    Json result;
    result.kind = Json::Kind::Boolean;
    result.boolean = value;
    return result;
}

// Bounded recursive-descent JSON parser, without regexes or external packages.
// It rejects duplicate decoded keys, malformed numbers, illegal UTF-8, invalid
// surrogate escapes, non-JSON whitespace, and trailing data.
class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    Json Read() {
        Json result = Value(0);
        Space();
        if (cursor_ != text_.size()) Fail("trailing content");
        return result;
    }

private:
    const std::string& text_;
    std::size_t cursor_ = 0;
    std::size_t nodes_ = 0;

    [[noreturn]] void Fail(const std::string& message) const {
        throw ConfigError("JSON byte " + std::to_string(cursor_) + ": " + message);
    }

    char Peek() const { return cursor_ < text_.size() ? text_[cursor_] : '\0'; }

    void Space() {
        while (cursor_ < text_.size() && (Peek() == ' ' || Peek() == '\t' ||
               Peek() == '\r' || Peek() == '\n')) ++cursor_;
    }

    bool Take(char character) {
        if (cursor_ < text_.size() && Peek() == character) { ++cursor_; return true; }
        return false;
    }

    void Expect(char character) {
        if (!Take(character)) Fail(std::string("expected '") + character + "'");
    }

    void Literal(const char* literal) {
        while (*literal != '\0') {
            if (!Take(*literal++)) Fail("invalid literal");
        }
    }

    static void AppendUtf8(std::string& result, std::uint32_t point) {
        if (point <= 0x7f) {
            result.push_back(static_cast<char>(point));
        } else if (point <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (point >> 6)));
            result.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        } else if (point <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (point >> 12)));
            result.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        }
    }

    std::uint32_t HexQuad() {
        std::uint32_t result = 0;
        for (int index = 0; index < 4; ++index) {
            if (cursor_ == text_.size()) Fail("incomplete Unicode escape");
            const char character = text_[cursor_++];
            std::uint32_t digit = 0;
            if (character >= '0' && character <= '9') digit = static_cast<unsigned>(character - '0');
            else if (character >= 'a' && character <= 'f') digit = static_cast<unsigned>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F') digit = static_cast<unsigned>(character - 'A' + 10);
            else Fail("invalid Unicode escape");
            result = result * 16 + digit;
        }
        return result;
    }

    void EscapedUnicode(std::string& result) {
        std::uint32_t point = HexQuad();
        if (point >= 0xd800 && point <= 0xdbff) {
            Expect('\\');
            Expect('u');
            const auto low = HexQuad();
            if (low < 0xdc00 || low > 0xdfff) Fail("invalid Unicode surrogate pair");
            point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
        } else if (point >= 0xdc00 && point <= 0xdfff) {
            Fail("unpaired low Unicode surrogate");
        }
        AppendUtf8(result, point);
    }

    void RawUtf8(std::string& result) {
        const std::size_t start = cursor_;
        const auto first = static_cast<unsigned char>(text_[cursor_++]);
        int continuation_count = 0;
        std::uint32_t point = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation_count = 1; point = first & 0x1f; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation_count = 2; point = first & 0x0f; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation_count = 3; point = first & 0x07; minimum = 0x10000;
        } else {
            Fail("invalid UTF-8 leading byte");
        }
        for (int index = 0; index < continuation_count; ++index) {
            if (cursor_ == text_.size()) Fail("truncated UTF-8");
            const auto next = static_cast<unsigned char>(text_[cursor_++]);
            if ((next & 0xc0) != 0x80) Fail("invalid UTF-8 continuation byte");
            point = (point << 6) | (next & 0x3f);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
            Fail("invalid UTF-8 code point");
        result.append(text_, start, cursor_ - start);
    }

    std::string Text() {
        Expect('"');
        std::string result;
        while (cursor_ < text_.size()) {
            const auto byte = static_cast<unsigned char>(Peek());
            if (byte == '"') { ++cursor_; return result; }
            if (byte < 0x20) Fail("unescaped control character in string");
            if (byte >= 0x80) { RawUtf8(result); continue; }
            ++cursor_;
            if (byte != '\\') { result.push_back(static_cast<char>(byte)); continue; }
            if (cursor_ == text_.size()) Fail("incomplete escape");
            switch (text_[cursor_++]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': EscapedUnicode(result); break;
            default: Fail("invalid string escape");
            }
        }
        Fail("unterminated string");
    }

    void Digits() {
        if (Peek() < '0' || Peek() > '9') Fail("number requires a digit");
        do { ++cursor_; } while (Peek() >= '0' && Peek() <= '9');
    }

    Json Number() {
        const auto start = cursor_;
        Take('-');
        if (!Take('0')) Digits();
        if (Take('.')) Digits();
        if (Take('e') || Take('E')) {
            if (!Take('+')) Take('-');
            Digits();
        }
        Json result;
        result.kind = Json::Kind::Number;
        result.text = text_.substr(start, cursor_ - start);
        return result;
    }

    Json Value(std::size_t depth) {
        if (depth > kMaxJsonDepth) Fail("maximum nesting depth exceeded");
        if (++nodes_ > kMaxJsonNodes) Fail("too many JSON values");
        Space();
        if (Peek() == '"') return String(Text());
        if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9')) return Number();
        if (Peek() == 't') { Literal("true"); return Boolean(true); }
        if (Peek() == 'f') { Literal("false"); return Boolean(false); }
        if (Peek() == 'n') { Literal("null"); return {}; }
        Json result;
        if (Take('[')) {
            result.kind = Json::Kind::Array;
            Space();
            if (Take(']')) return result;
            do {
                result.array.push_back(Value(depth + 1));
                Space();
                if (Take(']')) return result;
                Expect(',');
            } while (true);
        }
        if (Take('{')) {
            result.kind = Json::Kind::Object;
            Space();
            if (Take('}')) return result;
            do {
                Space();
                const auto key = Text();
                if (result.object.count(key) != 0) Fail("duplicate object key: " + key);
                Space();
                Expect(':');
                result.object.emplace(key, Value(depth + 1));
                Space();
                if (Take('}')) return result;
                Expect(',');
            } while (true);
        }
        Fail("expected JSON value");
    }
};

const Json& SchemaTemplate() {
    static const Json schema = [] {
        const std::string text(kSchemaTemplate);
        return JsonReader(text).Read();
    }();
    return schema;
}

void RequireKind(const Json& value, Json::Kind expected, const std::string& path) {
    if (value.kind != expected) throw ConfigError(path + ": incorrect JSON type");
}

const Json& Field(const Json& value, const std::string& key, const std::string& path) {
    RequireKind(value, Json::Kind::Object, path);
    const auto found = value.object.find(key);
    if (found == value.object.end()) throw ConfigError(path + ": missing field " + key);
    return found->second;
}

std::string TextField(const Json& value, const std::string& key, const std::string& path) {
    const auto& field = Field(value, key, path);
    RequireKind(field, Json::Kind::String, path + "." + key);
    return field.text;
}

bool EnabledField(const Json& value, const std::string& path) {
    const auto found = value.object.find("enabled");
    if (found == value.object.end()) return false;
    RequireKind(found->second, Json::Kind::Boolean, path + ".enabled");
    return found->second.boolean;
}

void MatchFixed(const Json& value, const Json& expected, const std::string& path) {
    RequireKind(value, expected.kind, path);
    switch (expected.kind) {
    case Json::Kind::Object:
        for (const auto& entry : value.object) {
            if (expected.object.count(entry.first) == 0)
                throw ConfigError(path + ": unknown field " + entry.first);
        }
        for (const auto& entry : expected.object)
            MatchFixed(Field(value, entry.first, path), entry.second, path + "." + entry.first);
        return;
    case Json::Kind::String:
    case Json::Kind::Number:
        if (value.text == expected.text) return;
        break;
    case Json::Kind::Boolean:
        if (value.boolean == expected.boolean) return;
        break;
    default:
        break;
    }
    throw ConfigError(path + ": unsupported fixed policy value");
}

Id ParseId(const std::string& text, const std::string& path) {
    Id result = 0;
    if (text.empty() || text.size() > 20) throw ConfigError(path + ": expected positive 64-bit decimal ID string");
    for (const char character : text) {
        if (character < '0' || character > '9')
            throw ConfigError(path + ": expected positive 64-bit decimal ID string");
        const auto digit = static_cast<Id>(character - '0');
        if (result > (std::numeric_limits<Id>::max() - digit) / 10)
            throw ConfigError(path + ": 64-bit ID overflow");
        result = result * 10 + digit;
    }
    if (result == 0) throw ConfigError(path + ": ID must be positive");
    return result;
}

const char* ModeName(SizingMode mode) {
    switch (mode) {
    case SizingMode::LotRatio: return "multiplier";
    case SizingMode::EquityRatio: return "equity_ratio";
    case SizingMode::FixedLots: return "fixed_lots";
    }
    throw ConfigError("unknown sizing mode");
}

const char* ParameterName(SizingMode mode) {
    switch (mode) {
    case SizingMode::LotRatio: return "multiplier";
    case SizingMode::EquityRatio: return "coefficient";
    case SizingMode::FixedLots: return "lots_per_source_trade";
    }
    throw ConfigError("unknown sizing mode");
}

SizingMode ParseMode(const std::string& mode, const std::string& path) {
    if (mode == "multiplier") return SizingMode::LotRatio;
    if (mode == "equity_ratio") return SizingMode::EquityRatio;
    if (mode == "fixed_lots") return SizingMode::FixedLots;
    throw ConfigError(path + ": unknown sizing mode");
}

void Quote(const std::string& text, std::string& result) {
    static const char hex[] = "0123456789abcdef";
    result.push_back('"');
    for (const unsigned char byte : text) {
        switch (byte) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (byte < 0x20) {
                result += "\\u00";
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 0xf]);
            } else result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back('"');
}

void WriteJson(const Json& value, std::string& result, std::size_t depth = 0) {
    switch (value.kind) {
    case Json::Kind::Null: result += "null"; return;
    case Json::Kind::Boolean: result += value.boolean ? "true" : "false"; return;
    case Json::Kind::Number: result += value.text; return;
    case Json::Kind::String: Quote(value.text, result); return;
    case Json::Kind::Object: {
        result += "{";
        bool first = true;
        for (const auto& entry : value.object) {
            if (!first) result += ",";
            result += "\n" + std::string((depth + 1) * 2, ' ');
            Quote(entry.first, result);
            result += ": ";
            WriteJson(entry.second, result, depth + 1);
            first = false;
        }
        if (!first) result += "\n" + std::string(depth * 2, ' ');
        result += "}";
        return;
    }
    case Json::Kind::Array: {
        result += "[";
        bool first = true;
        for (const auto& entry : value.array) {
            if (!first) result += ",";
            result += "\n" + std::string((depth + 1) * 2, ' ');
            WriteJson(entry, result, depth + 1);
            first = false;
        }
        if (!first) result += "\n" + std::string(depth * 2, ' ');
        result += "]";
        return;
    }
    }
}

} // namespace

bool ValidDirection(CopyDirection direction) {
    return direction == CopyDirection::Forward ||
           (direction == CopyDirection::Reverse && std::string(kPlatformName) == "mt5");
}

bool MakePolicy(const RuleConfig& rule, VolumePolicy& output, std::string& error) {
    Rational parameter;
    if (rule.parameter.size() > kMaxParameterLength ||
        !ParsePositiveDecimal(rule.parameter, parameter)) {
        error = "parameter: expected a positive plain decimal string of at most 64 characters";
        return false;
    }
    VolumePolicy policy;
    policy.mode = rule.mode;
    switch (rule.mode) {
    case SizingMode::LotRatio:
    case SizingMode::EquityRatio:
        policy.factor = parameter;
        break;
    case SizingMode::FixedLots: {
        const auto divisor = std::gcd(kUnitsPerLot, parameter.denominator);
        if (parameter.denominator / divisor != 1) {
            error = "lots_per_source_trade: precision must convert exactly to 0.00000001-lot business units";
            return false;
        }
        const auto multiplier = kUnitsPerLot / divisor;
        if (parameter.numerator > std::numeric_limits<Units>::max() / multiplier) {
            error = "lots_per_source_trade: business volume units overflow";
            return false;
        }
        policy.fixed_units = parameter.numerator * multiplier;
        break;
    }
    default:
        error = "mode: unknown sizing mode";
        return false;
    }
    output = policy;
    error.clear();
    return true;
}

bool ValidateConfig(const CopierConfig& config, std::string& error) {
    if (config.relationships.size() > kMaxRelationships) {
        error = "relationships: maximum 1000 rules exceeded";
        return false;
    }
    std::vector<Relation> relations;
    relations.reserve(config.relationships.size());
    for (std::size_t index = 0; index < config.relationships.size(); ++index) {
        const auto& rule = config.relationships[index];
        if (!ValidDirection(rule.direction)) {
            error = "relationships[" + std::to_string(index) + "].direction: " +
                    (rule.direction == CopyDirection::Reverse ?
                        "reverse copying is supported only by MT5" : "unknown copy direction");
            return false;
        }
        VolumePolicy policy;
        if (!MakePolicy(rule, policy, error) || !ValidateSymbolRule(rule.symbol_mask,rule.source_suffix,error)) {
            error = "relationships[" + std::to_string(index) + "]." + error;
            return false;
        }
        relations.push_back({rule.id, rule.source_login, rule.follower_login, policy});
    }
    const auto errors = ValidateRelations(relations);
    if (!errors.empty()) {
        switch (errors.front()) {
        case RelationError::InvalidId: error = "relationship and account IDs must be positive 64-bit integers"; break;
        case RelationError::DuplicateId: error = "relationship_id must be unique"; break;
        case RelationError::SelfFollow: error = "source_login and follower_login must differ"; break;
        case RelationError::DuplicatePair: error = "each source/follower pair may appear only once"; break;
        case RelationError::InvalidPolicy: error = "invalid volume policy"; break;
        case RelationError::DirectedCycle: error = "relationships form a directed follow cycle"; break;
        }
        return false;
    }
    error.clear();
    return true;
}

bool ParseConfigJson(const std::string& json, CopierConfig& output, std::string& error) {
    if (json.size() > kMaxConfigBytes) {
        error = "configuration exceeds the 1 MiB size limit";
        return false;
    }
    try {
        const Json document = JsonReader(json).Read();
        Json schema = SchemaTemplate();
        const auto& version=Field(document,"schema_version","config");
        RequireKind(version,Json::Kind::Number,"config.schema_version");
        if (version.text!="2" && version.text!="3" && version.text!="4")
            throw ConfigError("unsupported config schema_version");
        schema.object["schema_version"].text=version.text;
        const bool directions=version.text=="4";
        const bool symbols=version.text=="3" || directions;
        if (directions) schema.object["trade_policy"].object["direction"]=String("per_relationship");
        RequireKind(document, Json::Kind::Object, "config");
        for (const auto& entry : document.object) {
            if (schema.object.count(entry.first) == 0)
                throw ConfigError("config: unknown field " + entry.first);
        }
        for (const auto& entry : schema.object) {
            if (entry.first == "enabled" || entry.first == "relationships") continue;
            if (entry.first == "status" && document.object.count("status") == 0) continue;
            MatchFixed(Field(document, entry.first, "config"), entry.second, "config." + entry.first);
        }
        CopierConfig parsed;
        parsed.enabled = EnabledField(document, "config");
        const Json& relationships = Field(document, "relationships", "config");
        RequireKind(relationships, Json::Kind::Array, "relationships");
        if (relationships.array.size() > kMaxRelationships)
            throw ConfigError("relationships: maximum 1000 rules exceeded");
        parsed.relationships.reserve(relationships.array.size());
        for (std::size_t index = 0; index < relationships.array.size(); ++index) {
            const auto& value = relationships.array[index];
            const std::string path = "relationships[" + std::to_string(index) + "]";
            RequireKind(value, Json::Kind::Object, path);
            RuleConfig rule;
            rule.mode = ParseMode(TextField(value, "mode", path), path + ".mode");
            std::set<std::string> allowed{
                "relationship_id", "source_login", "follower_login", "enabled", "mode", ParameterName(rule.mode)};
            if (symbols) allowed.insert({"symbol_mask","auto_symbol","source_suffix"});
            if (directions) allowed.insert("direction");
            for (const auto& entry : value.object) {
                if (allowed.count(entry.first) == 0)
                    throw ConfigError(path + ": unknown or mode-mismatched field " + entry.first);
            }
            rule.id = ParseId(TextField(value, "relationship_id", path), path + ".relationship_id");
            rule.source_login = ParseId(TextField(value, "source_login", path), path + ".source_login");
            rule.follower_login = ParseId(TextField(value, "follower_login", path), path + ".follower_login");
            rule.enabled = EnabledField(value, path);
            rule.parameter = TextField(value, ParameterName(rule.mode), path);
            if (directions) {
                const auto direction=TextField(value,"direction",path);
                if (direction=="forward") rule.direction=CopyDirection::Forward;
                else if (direction=="reverse") rule.direction=CopyDirection::Reverse;
                else throw ConfigError(path+".direction: expected forward or reverse");
            }
            if (symbols) {
                rule.symbol_mask=TextField(value,"symbol_mask",path);
                rule.source_suffix=TextField(value,"source_suffix",path);
                const auto& auto_symbol=Field(value,"auto_symbol",path);
                RequireKind(auto_symbol,Json::Kind::Boolean,path+".auto_symbol");
                rule.auto_symbol=auto_symbol.boolean;
            }
            parsed.relationships.push_back(std::move(rule));
        }
        if (!ValidateConfig(parsed, error)) return false;
        output = std::move(parsed);
        error.clear();
        return true;
    } catch (const ConfigError& exception) {
        error = exception.what();
        return false;
    }
}

std::string WriteConfigJson(const CopierConfig& config) {
    std::string error;
    if (!ValidateConfig(config, error)) throw std::invalid_argument(error);
    Json document = SchemaTemplate();
    const bool directions=std::any_of(config.relationships.begin(),config.relationships.end(),[](const RuleConfig& r) {
        return r.direction==CopyDirection::Reverse;
    });
    const bool symbols=directions || std::any_of(config.relationships.begin(),config.relationships.end(),[](const RuleConfig& r) {
        return r.symbol_mask!="*" || r.auto_symbol || !r.source_suffix.empty();
    });
    if (directions) {
        document.object["schema_version"].text="4";
        document.object["trade_policy"].object["direction"]=String("per_relationship");
    } else if (symbols) document.object["schema_version"].text="3";
    document.object["enabled"] = Boolean(config.enabled);
    auto& relationships = document.object["relationships"].array;
    for (const auto& rule : config.relationships) {
        Json value;
        value.kind = Json::Kind::Object;
        value.object["relationship_id"] = String(std::to_string(rule.id));
        value.object["source_login"] = String(std::to_string(rule.source_login));
        value.object["follower_login"] = String(std::to_string(rule.follower_login));
        value.object["enabled"] = Boolean(rule.enabled);
        value.object["mode"] = String(ModeName(rule.mode));
        value.object[ParameterName(rule.mode)] = String(rule.parameter);
        if (directions)
            value.object["direction"]=String(rule.direction==CopyDirection::Reverse ? "reverse" : "forward");
        if (symbols) {
            value.object["symbol_mask"]=String(rule.symbol_mask);
            value.object["auto_symbol"]=Boolean(rule.auto_symbol);
            value.object["source_suffix"]=String(rule.source_suffix);
        }
        relationships.push_back(std::move(value));
    }
    std::string result;
    WriteJson(document, result);
    result += "\n";
    if (result.size() > kMaxConfigBytes) throw std::invalid_argument("configuration exceeds 1 MiB");
    return result;
}

} // namespace tradecopier

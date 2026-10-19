#include "dynamicleverage/core.hpp"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace dynamicleverage {
namespace {
constexpr std::size_t kMaxJsonBytes = 1024 * 1024;
constexpr std::size_t kMaxJsonDepth = 16;
constexpr std::size_t kMaxJsonNodes = 40000;

// The dependency-free JSON reader is shared in design with trade-copier,
// but this plugin compiles and links independently.
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

void require(bool condition, const std::string& message) {
  if (!condition) throw ConfigError(message);
}

void fields(const Json& value, std::initializer_list<const char*> names) {
  require(value.kind == Json::Kind::Object, "expected JSON object");
  require(value.object.size() == names.size(), "unknown or missing JSON fields");
  for (const auto* name : names)
    require(value.object.count(name) == 1, std::string("missing JSON field: ") + name);
}

const std::string& string_field(const Json& object, const char* key) {
  const auto& value = object.object.at(key);
  require(value.kind == Json::Kind::String, std::string(key) + " must be a JSON string");
  return value.text;
}

bool bool_field(const Json& object, const char* key) {
  const auto& value = object.object.at(key);
  require(value.kind == Json::Kind::Boolean, std::string(key) + " must be a JSON boolean");
  return value.boolean;
}

std::uint32_t integer_field(const Json& object, const char* key, std::uint32_t maximum) {
  const auto& value = object.object.at(key);
  require(value.kind == Json::Kind::Number && !value.text.empty(), std::string(key) + " must be a JSON integer");
  std::uint64_t result = 0;
  for (const char c : value.text) {
    require(c >= '0' && c <= '9', std::string(key) + " must be a nonnegative JSON integer");
    result = result * 10 + static_cast<unsigned>(c - '0');
    require(result <= maximum, std::string(key) + " exceeds maximum");
  }
  return static_cast<std::uint32_t>(result);
}

const std::vector<Json>& array_field(const Json& object, const char* key) {
  const auto& value = object.object.at(key);
  require(value.kind == Json::Kind::Array, std::string(key) + " must be a JSON array");
  return value.array;
}

std::string quoted(const std::string& value) {
  std::string result = "\"";
  for (const unsigned char c : value) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20) {
          static constexpr char digits[] = "0123456789abcdef";
          result += "\\u00";
          result.push_back(digits[c >> 4]);
          result.push_back(digits[c & 15]);
        } else result.push_back(static_cast<char>(c));
    }
  }
  return result + '"';
}
} // namespace

Config parse_rules_json(const std::string& text, const std::string& expected_platform) {
  require(expected_platform == "mt4" || expected_platform == "mt5", "expected platform must be mt4 or mt5");
  require(text.size() <= kMaxJsonBytes, "rules JSON exceeds 1 MiB");
  const auto root = JsonReader(text).Read();
  fields(root, {"schema_version", "platform", "enabled", "poll_interval_ms", "rules"});
  require(integer_field(root, "schema_version", 1) == 1, "unsupported schema_version");
  Config result;
  result.platform = string_field(root, "platform");
  require(result.platform == expected_platform, "rules platform does not match server platform");
  result.enabled = bool_field(root, "enabled");
  result.poll_interval_ms = integer_field(root, "poll_interval_ms", 60000);
  const auto& rules = array_field(root, "rules");
  require(rules.size() <= 256, "at most 256 rules are supported");
  for (const auto& value : rules) {
    fields(value, {"id", "name", "enabled", "scope", "selector", "tiers"});
    Rule rule;
    rule.id = string_field(value, "id");
    rule.name = string_field(value, "name");
    rule.enabled = bool_field(value, "enabled");
    const auto& scope = string_field(value, "scope");
    require(scope == "group" || scope == "account", "scope must be group or account");
    rule.scope = scope == "group" ? Scope::group : Scope::account;
    rule.selector = string_field(value, "selector");
    rule.tiers.clear();
    const auto& tiers = array_field(value, "tiers");
    require(!tiers.empty() && tiers.size() <= 32, "require 1 to 32 leverage tiers");
    for (const auto& tier : tiers) {
      fields(tier, {"above_lots", "leverage"});
      const auto& above = string_field(tier, "above_lots");
      require(!above.empty() && above.size() <= 40, "above_lots requires 1 to 40 decimal characters");
      for (const char c : above)
        require((c >= '0' && c <= '9') || c == '.', "above_lots must be a decimal lot string");
      rule.tiers.push_back({parse_lots(above), integer_field(tier, "leverage", 100000)});
    }
    result.rules.push_back(std::move(rule));
  }
  validate(result);
  return result;
}

std::string rules_json(const Config& config) {
  require(config.platform == "mt4" || config.platform == "mt5", "rules export requires mt4 or mt5 platform");
  validate(config);
  std::ostringstream result;
  result.imbue(std::locale::classic());
  result << "{\n  \"schema_version\": 1,\n  \"platform\": " << quoted(config.platform)
         << ",\n  \"enabled\": " << (config.enabled ? "true" : "false")
         << ",\n  \"poll_interval_ms\": " << config.poll_interval_ms << ",\n  \"rules\": [";
  for (std::size_t i = 0; i < config.rules.size(); ++i) {
    const auto& rule = config.rules[i];
    result << (i ? ",\n" : "\n") << "    {\"id\": " << quoted(rule.id)
           << ", \"name\": " << quoted(rule.name) << ", \"enabled\": " << (rule.enabled ? "true" : "false")
           << ", \"scope\": " << quoted(rule.scope == Scope::group ? "group" : "account")
           << ", \"selector\": " << quoted(rule.selector) << ", \"tiers\": [";
    for (std::size_t t = 0; t < rule.tiers.size(); ++t) {
      const auto& tier = rule.tiers[t];
      result << (t ? ", " : "") << "{\"above_lots\": " << quoted(format_lots(tier.above))
             << ", \"leverage\": " << tier.leverage << '}';
    }
    result << "]}";
  }
  result << (config.rules.empty() ? "" : "\n  ") << "]\n}\n";
  auto text = result.str();
  require(text.size() <= kMaxJsonBytes, "exported rules JSON exceeds 1 MiB");
  return text;
}

Config load_rules_file(const std::string& path, const std::string& expected_platform) {
  require(!path.empty() && path.find('\0') == std::string::npos, "invalid rules file path");
  require(std::filesystem::u8path(path).is_absolute(), "rules file path must be absolute");
  std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
  require(stream.is_open(), "cannot open rules file");
  std::string data(kMaxJsonBytes + 1, '\0');
  stream.read(&data[0], static_cast<std::streamsize>(data.size()));
  require(!stream.bad() && (stream.eof() || !stream.fail()), "cannot read complete rules file");
  data.resize(static_cast<std::size_t>(stream.gcount()));
  require(data.size() <= kMaxJsonBytes, "rules file exceeds 1 MiB");
  auto result = parse_rules_json(data, expected_platform);
  result.rules_file = path;
  return result;
}
} // namespace dynamicleverage

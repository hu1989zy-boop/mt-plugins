#include "dynamicleverage/core.hpp"

#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
std::string quoted(const std::string& text) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result = "\"";
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
    else if (c < 0x20) {
      result += "\\u00";
      result += digits[c >> 4]; result += digits[c & 15];
    } else result += static_cast<char>(c);
  }
  return result + '"';
}

int failure(const std::exception& error) {
  std::cout << "{\"ok\":false,\"error\":" << quoted(error.what()) << "}\n";
  return 2;
}

int run(const std::vector<std::string>& arguments) {
  using namespace dynamicleverage;
  try {
    if (arguments.size() == 1 && arguments[0] == "--help") {
      std::cout << "{\"ok\":true,\"usage\":" << quoted(
        "DynamicLeverageRulesCheck --platform mt4|mt5 --file ABSOLUTE_JSON_PATH "
        "[--login DECIMAL_LOGIN --group ACCOUNT_GROUP --lots DECIMAL_LOTS]") << "}\n";
      return 0;
    }
    const std::set<std::string> supported{"--platform", "--file", "--login", "--group", "--lots"};
    std::map<std::string, std::string> options;
    for (std::size_t i = 0; i < arguments.size(); i += 2) {
      if (!supported.count(arguments[i])) throw std::invalid_argument("unknown argument: " + arguments[i]);
      if (i + 1 == arguments.size()) throw std::invalid_argument("argument requires a value: " + arguments[i]);
      if (!options.emplace(arguments[i], arguments[i + 1]).second)
        throw std::invalid_argument("duplicate argument: " + arguments[i]);
    }
    if (!options.count("--platform") || !options.count("--file"))
      throw std::invalid_argument("--platform and --file are required");
    const bool preview = options.count("--login") || options.count("--lots") || options.count("--group");
    if (preview && (!options.count("--login") || !options.count("--lots")))
      throw std::invalid_argument("preview requires both --login and --lots");

    const auto config = load_rules_file(options.at("--file"), options.at("--platform"));
    // Complete all potentially failing work before emitting any output.
    const auto canonical = rules_json(config);
    RulePreview result;
    if (preview) result = preview_rules(config, options.at("--login"), options["--group"], options.at("--lots"));
    std::cout << "{\"ok\":true,\"platform\":" << quoted(config.platform)
              << ",\"enabled\":" << (config.enabled ? "true" : "false")
              << ",\"rule_count\":" << config.rules.size() << ",\"config\":" << canonical;
    if (preview) {
      std::cout << ",\"preview\":{\"matched\":" << (result.matched ? "true" : "false")
                << ",\"rule_id\":" << (result.matched ? quoted(result.rule_id) : "null")
                << ",\"rule_name\":" << (result.matched ? quoted(result.rule_name) : "null")
                << ",\"leverage\":" << (result.matched ? std::to_string(result.leverage) : "null")
                << ",\"volume_lots\":" << quoted(format_lots(result.volume)) << '}';
    }
    std::cout << "}\n";
    return 0;
  } catch (const std::exception& error) { return failure(error); }
}

#ifdef _WIN32
std::string utf8(const wchar_t* value) {
  const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
  if (required == 0) throw std::invalid_argument("invalid Unicode command-line argument");
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, &result[0], required, nullptr, nullptr) == 0)
    throw std::invalid_argument("cannot convert Unicode command-line argument");
  result.pop_back();
  return result;
}
#endif
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  try {
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) arguments.push_back(utf8(argv[i]));
    return run(arguments);
  } catch (const std::exception& error) { return failure(error); }
}
#else
int main(int argc, char** argv) { return run({argv + 1, argv + argc}); }
#endif

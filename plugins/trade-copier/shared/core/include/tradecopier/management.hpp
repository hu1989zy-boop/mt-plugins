#pragma once
#include "tradecopier/executor.hpp"

namespace tradecopier::management {
constexpr std::size_t kMaxMessage = 4 * 1024 * 1024;
constexpr std::size_t kVisibleRows = 200;
enum class Operation : unsigned { Status, Pause, Resume, Apply, Inspect, UpdateSubscriptions };
struct Request {
    Operation operation = Operation::Status;
    std::string session;
    Id revision = 0, intent = 0;
    std::string config_json;
    std::string expected_config;
};
using Rows = std::vector<std::vector<std::string>>;
struct Response {
    bool ok = true;
    std::string message, session, identity, config_path, journal_path;
    Id revision = 0;
    bool paused = false;
    std::string halt, apply_block, config_json;
    Id links_total = 0, intents_total = 0, active = 0, review = 0, pending = 0, exiting = 0;
    Rows links, intents;
};
std::string EncodeRequest(const Request&);
Request DecodeRequest(const std::string&);
std::string EncodeResponse(const Response&);
Response DecodeResponse(const std::string&);
Response Snapshot(const execution::Engine&);
} // namespace tradecopier::management

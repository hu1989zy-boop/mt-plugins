#pragma once

#include "tradecopier/core.hpp"

#include <cstddef>
#include <string>
#include <vector>

#ifndef TRADECOPIER_PLATFORM_NAME
#define TRADECOPIER_PLATFORM_NAME "mt5"
#endif

namespace tradecopier {

inline constexpr char kPlatformName[] = TRADECOPIER_PLATFORM_NAME;
constexpr std::size_t kMaxConfigBytes = 1024 * 1024;
constexpr std::size_t kMaxRelationships = 1000;

enum class CopyDirection { Forward, Reverse };

// Validates enum values and the platform's supported directions. Reverse
// copying is currently available only in the MT5 build.
bool ValidDirection(CopyDirection direction);

struct RuleConfig {
    Id id = 0;
    Id source_login = 0;
    Id follower_login = 0;
    bool enabled = false;
    SizingMode mode = SizingMode::LotRatio;
    std::string parameter;
    std::string symbol_mask = "*";
    bool auto_symbol = false; // Legacy configurations keep exact-name copying.
    std::string source_suffix = {}; // Optional exact suffix; empty uses SDK metadata.
    CopyDirection direction = CopyDirection::Forward;
};

struct CopierConfig {
    bool enabled = false;
    std::vector<RuleConfig> relationships;
};

// Strict schema_version 2/3/4 codec for local configuration editing. Accepts only
// the fixed policy values in config/copier.example.json. Parsing validates the
// entire document and all rules, including disabled rules. Failure leaves the
// supplied output unchanged. No SDK configuration or trading action is applied.
bool ParseConfigJson(const std::string& json, CopierConfig& output, std::string& error);

// Canonical JSON with IDs as decimal strings and all fixed schema policy fields.
// Throws std::invalid_argument for invalid configurations. Call ValidateConfig
// first when the caller needs a non-throwing validation result for a UI message.
std::string WriteConfigJson(const CopierConfig& config);
bool ValidateConfig(const CopierConfig& config, std::string& error);

// Decimal parameters must be positive. Fixed lots convert exactly into business
// units; a non-integral result below kUnitsPerLot precision is rejected.
// Failure leaves output unchanged. The function does not validate account IDs.
bool MakePolicy(const RuleConfig& rule, VolumePolicy& output, std::string& error);

} // namespace tradecopier

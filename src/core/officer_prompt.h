#pragma once

#include "data/models.h"
#include "data/llm_client.h"
#include "json.hpp"

namespace stfc {

nlohmann::json build_officer_assessment_data(const PlayerData& pd, const GameData& gd);
LlmRequest build_officer_assessment_request(const PlayerData& pd, const GameData& gd);

} // namespace stfc

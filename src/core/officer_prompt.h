#pragma once

#include "data/models.h"
#include "data/llm_client.h"
#include "core/crew_optimizer.h"
#include "json.hpp"

namespace stfc {

nlohmann::json build_officer_assessment_data(const PlayerData& pd, const GameData& gd,
                                              const std::vector<ClassifiedOfficer>& classified);
LlmRequest build_officer_assessment_request(const PlayerData& pd, const GameData& gd,
                                             const std::vector<ClassifiedOfficer>& classified);

} // namespace stfc

#pragma once

#include "data/models.h"
#include "data/llm_client.h"
#include "json.hpp"

namespace stfc {

nlohmann::json build_strategic_assessment_data(const PlayerData& pd);
LlmRequest build_strategic_assessment_request(const PlayerData& pd);

} // namespace stfc

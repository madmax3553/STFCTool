#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

#include "data/llm_client.h"
#include "core/account_state.h"
#include "core/crew_optimizer.h"

namespace stfc {

// ---------------------------------------------------------------------------
// AI Crew Advisor — bridges the account state + LLM to produce recommendations
//
// Three query modes:
//   1. Crew recommendation — best crew for a scenario given your officers
//   2. Progression advice — what to invest in next given your full account
//   3. META analysis — current community meta (uses web search grounding)
//
// The advisor builds structured prompts, sends them through the LlmClient,
// and parses responses back into usable data structures.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Crew recommendation result (parsed from LLM response)
// ---------------------------------------------------------------------------

struct AiCrewRecommendation {
    std::string captain;
    std::vector<std::string> bridge;       // 2 bridge officers
    std::vector<std::string> below_decks;  // optional BDA suggestions
    std::string reasoning;                  // AI's explanation
    double confidence = 0.0;                // 0.0-1.0 self-assessed confidence
    std::string ship_advice;                // ship type recommendation
    std::vector<std::string> warnings;      // caveats, assumptions
};

struct AiCrewResult {
    std::vector<AiCrewRecommendation> recommendations;  // top N
    std::string raw_response;              // full LLM response text
    std::string model_used;
    bool search_grounded = false;
    std::string error;                     // empty if success

    bool ok() const { return error.empty() && !recommendations.empty(); }
};

// ---------------------------------------------------------------------------
// Progression advice result
// ---------------------------------------------------------------------------

struct ProgressionAdvice {
    struct Investment {
        std::string category;     // "officer", "ship", "research", "tech"
        std::string target;       // e.g. "Khan", "Enterprise-E", "Advanced Warp"
        std::string action;       // e.g. "Rank up to 5", "Unlock tier 3"
        std::string reason;       // Why this investment matters
        int priority = 0;         // 1 = highest priority
    };

    std::vector<Investment> investments;
    std::string summary;           // Overall assessment
    std::string raw_response;
    std::string model_used;
    bool search_grounded = false;
    std::string error;

    bool ok() const { return error.empty() && !investments.empty(); }
};

// ---------------------------------------------------------------------------
// META analysis result
// ---------------------------------------------------------------------------

struct MetaAnalysis {
    struct MetaCrew {
        std::string captain;
        std::vector<std::string> bridge;
        std::string scenario;      // what it's good for
        std::string explanation;
        // New fields for meta comparison
        std::vector<std::string> player_has;    // officers the player owns from this crew
        std::vector<std::string> missing;       // officers the player doesn't own
        std::map<std::string, std::string> substitutes;  // missing_name -> substitute_name
    };

    std::vector<MetaCrew> top_crews;
    std::string meta_summary;      // Current state of the META + readiness assessment
    std::vector<std::string> sources;  // URLs from search grounding
    std::string raw_response;
    std::string model_used;
    bool search_grounded = false;
    std::string error;

    bool ok() const { return error.empty() && !meta_summary.empty(); }
};

// ---------------------------------------------------------------------------
// Local crew summary — optimizer results fed into META analysis
// ---------------------------------------------------------------------------

struct LocalCrewSummary {
    std::string scenario;
    std::string captain;
    std::vector<std::string> bridge;
    double score = 0.0;
    std::string synergy_group;        // shared group if any
    std::vector<std::string> notes;   // synergy_notes from breakdown
};

// ---------------------------------------------------------------------------
// Streaming callback for TUI display
// ---------------------------------------------------------------------------

using AdvisorStreamCallback = std::function<void(const std::string& chunk)>;

// ---------------------------------------------------------------------------
// CrewAdvisor — the main class
// ---------------------------------------------------------------------------

class CrewAdvisor {
public:
    // Construct with an LLM client (takes ownership)
    explicit CrewAdvisor(std::unique_ptr<LlmClient> client);

    // -------------------------------------------------------------------
    // Query methods
    // -------------------------------------------------------------------

    // Recommend best crews for a scenario
    AiCrewResult recommend_crew(
        const AccountSnapshot& snapshot,
        int top_n = 3,
        AdvisorStreamCallback stream_cb = nullptr);

    // Get progression advice (what to invest in next)
    ProgressionAdvice advise_progression(
        const AccountSnapshot& snapshot,
        const std::string& goal = "",     // optional player goal, e.g. "better PvP"
        AdvisorStreamCallback stream_cb = nullptr);

    // Analyze current META (uses local optimizer results + web search)
    MetaAnalysis analyze_meta(
        Scenario scenario,
        const AccountSnapshot& snapshot,
        const std::vector<LocalCrewSummary>& local_crews = {},
        AdvisorStreamCallback stream_cb = nullptr);

    // -------------------------------------------------------------------
    // Free-form question (with account context)
    // -------------------------------------------------------------------

    LlmResponse ask(
        const AccountSnapshot& snapshot,
        const std::string& question,
        AdvisorStreamCallback stream_cb = nullptr);

    // -------------------------------------------------------------------
    // Provider info
    // -------------------------------------------------------------------

    std::string provider_name() const;
    std::string model_name() const;
    LlmCapabilities capabilities() const;

    // -------------------------------------------------------------------
    // Direct client access (for group-based pipeline in AiCrewEngine)
    // -------------------------------------------------------------------

    LlmResponse client_query(const LlmRequest& req) {
        return client_ ? client_->query(req) : LlmResponse{};
    }

    LlmResponse client_query_stream(const LlmRequest& req, AdvisorStreamCallback cb) {
        if (!client_) return LlmResponse{};
        if (cb && client_->capabilities().streaming) {
            return client_->query_stream(req, cb);
        }
        return client_->query(req);
    }

private:
    // Prompt builders
    LlmRequest build_crew_prompt(const AccountSnapshot& snapshot, int top_n) const;
    LlmRequest build_progression_prompt(const AccountSnapshot& snapshot,
                                         const std::string& goal) const;
    LlmRequest build_meta_prompt(Scenario scenario,
                                   const AccountSnapshot& snapshot,
                                   const std::vector<LocalCrewSummary>& local_crews) const;

    // Response parsers
    AiCrewResult parse_crew_response(const LlmResponse& resp) const;
    ProgressionAdvice parse_progression_response(const LlmResponse& resp) const;
    MetaAnalysis parse_meta_response(const LlmResponse& resp) const;

    std::unique_ptr<LlmClient> client_;
};

} // namespace stfc

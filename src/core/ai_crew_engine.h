#pragma once

#include <string>
#include <memory>
#include <functional>
#include <atomic>

#include "data/llm_client.h"
#include "data/ssh_tunnel.h"
#include "core/crew_advisor.h"
#include "core/account_state.h"
#include "core/crew_optimizer.h"
#include "core/officer_groups.h"
#include "core/ai_history.h"
#include "core/meta_cache.h"

namespace stfc {

// ---------------------------------------------------------------------------
// AiCrewEngine — integration layer between the TUI and the AI advisor
//
// Owns the full AI lifecycle: config → tunnel → LLM client → advisor.
// Provides high-level methods that the TUI can call without knowing about
// prompts, JSON parsing, or SSH tunnels.
//
// NEW: Group-based query pipeline
// Instead of one giant query with all officers, splits officers into logical
// groups and queries the LLM per group. Each response is stored in the
// history with user ratings. Good-rated responses are injected as context
// into future queries for iterative refinement.
//
// Designed to sit alongside CrewOptimizer in AppState, not replace it.
// Mining scenarios still use the local optimizer; combat scenarios can
// optionally route through AI for richer, META-aware recommendations.
// ---------------------------------------------------------------------------

// Status of the AI subsystem (for TUI display)
struct AiStatus {
    bool available = false;          // AI advisor is ready to use
    bool is_fallback = false;        // Using fallback provider
    std::string provider;            // Active provider name
    std::string model;               // Active model name
    std::string tunnel_status;       // SSH tunnel status
    std::string error;               // Non-empty if setup failed
    bool has_search = false;         // Provider supports web search
    bool has_streaming = false;      // Provider supports streaming
};

// ---------------------------------------------------------------------------
// Group query result — one per officer group
// ---------------------------------------------------------------------------

struct GroupQueryResult {
    std::string group_name;                         // "PvP Combat", etc.
    OfficerGroupId group_id;
    int officer_count = 0;                          // How many officers in this group
    std::vector<AiCrewRecommendation> crews;        // Parsed crew recs
    std::string raw_response;                       // Raw LLM output
    std::string error;                              // Non-empty if this group query failed
    std::string history_id;                         // ID in the history store (for rating)
    AiRating rating = AiRating::Unrated;            // Current user rating

    bool ok() const { return error.empty() && !crews.empty(); }
};

// ---------------------------------------------------------------------------
// Full group-based query result — the pipeline output
// ---------------------------------------------------------------------------

struct GroupQueryPipelineResult {
    std::vector<GroupQueryResult> group_results;     // One per group queried
    int groups_total = 0;                            // Total groups
    int groups_completed = 0;                        // How many have been queried
    int groups_succeeded = 0;                        // How many returned valid crews
    std::string model_used;
    std::string error;                               // Pipeline-level error (e.g., AI not available)

    bool ok() const { return error.empty() && groups_succeeded > 0; }

    // All crews across all groups (flattened)
    std::vector<AiCrewRecommendation> all_crews() const {
        std::vector<AiCrewRecommendation> result;
        for (const auto& gr : group_results) {
            for (const auto& crew : gr.crews) {
                result.push_back(crew);
            }
        }
        return result;
    }
};

// Callback for streaming AI responses to the TUI
using AiStreamCallback = std::function<void(const std::string& chunk)>;

// Progress callback for group pipeline: (current_group_index, total_groups, group_name)
using GroupProgressCallback = std::function<void(int current, int total, const std::string& group_name)>;

class AiCrewEngine {
public:
    AiCrewEngine();
    ~AiCrewEngine();

    // -------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------

    // Initialize the AI subsystem: load config, open tunnel, create client
    // Returns empty string on success, error message on failure.
    // The engine is usable even on failure (methods return errors gracefully).
    std::string initialize(const std::string& config_path = "data/ai_config.json");

    // Re-initialize (e.g., after config change)
    std::string reinitialize();

    // Shut down: close tunnel, release client
    void shutdown();

    // -------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------

    AiStatus status() const;
    bool is_available() const;

    // -------------------------------------------------------------------
    // NEW: Group-based query pipeline
    //
    // This is the primary query method for the new architecture.
    // Splits officers into groups, queries LLM per group with focused
    // prompts, stores results in history, injects good-rated prior
    // responses as context.
    //
    // If a META cache exists, it filters officers to only those in the
    // Gemini-sourced META list (intersected with owned roster).
    // Falls back to tag-based grouping if no META cache.
    //
    // Skips Mining group (handled by local optimizer).
    // -------------------------------------------------------------------

    GroupQueryPipelineResult query_by_groups(
        const std::vector<ClassifiedOfficer>& officers,
        AiStreamCallback stream_cb = nullptr,
        GroupProgressCallback progress_cb = nullptr,
        std::atomic<bool>* cancel_flag = nullptr);

    // Query a single group (for re-running one group or targeted queries)
    GroupQueryResult query_single_group(
        const OfficerGroup& group,
        AiStreamCallback stream_cb = nullptr);

    // -------------------------------------------------------------------
    // META cache refresh (Gemini web-search-grounded)
    //
    // Queries Gemini for each group's top META officers, parses results,
    // and saves to data/meta_cache.json. Rate-limited: manual refresh
    // only (user presses 'M'). ~10 Gemini requests per refresh.
    // -------------------------------------------------------------------

    using MetaRefreshCallback = std::function<void(int current, int total,
                                                    const std::string& group_name)>;

    // Refresh the META cache. known_officers = all officer names in the roster.
    // Returns empty string on success, error message on failure.
    std::string refresh_meta_cache(
        const std::vector<std::string>& known_officers,
        AiStreamCallback stream_cb = nullptr,
        MetaRefreshCallback progress_cb = nullptr,
        std::atomic<bool>* cancel_flag = nullptr);

    // Get current META cache (read-only)
    const MetaCache& meta_cache() const { return meta_cache_; }

    // Load META cache from disk (called during init)
    void load_meta_cache();

    // -------------------------------------------------------------------
    // History & rating
    // -------------------------------------------------------------------

    AiHistoryStore& history() { return history_; }
    const AiHistoryStore& history() const { return history_; }

    bool rate_result(const std::string& history_id, AiRating rating);

    // -------------------------------------------------------------------
    // Legacy queries (still available for Progression / META / Ask)
    // -------------------------------------------------------------------

    // Recommend crews for a scenario (OLD monolithic approach)
    AiCrewResult recommend_crews(
        const PlayerData& player_data,
        const GameData& game_data,
        const std::vector<ClassifiedOfficer>& officers,
        Scenario scenario,
        ShipType ship_type,
        int top_n = 3,
        const std::set<std::string>& excluded = {},
        AiStreamCallback stream_cb = nullptr);

    // Get progression advice
    ProgressionAdvice advise_progression(
        const PlayerData& player_data,
        const GameData& game_data,
        const std::vector<ClassifiedOfficer>& officers,
        const std::string& goal = "",
        AiStreamCallback stream_cb = nullptr);

    // Analyze META for a scenario (runs local optimizer, feeds results to LLM)
    MetaAnalysis analyze_meta(
        Scenario scenario,
        const PlayerData& player_data,
        const GameData& game_data,
        const std::vector<ClassifiedOfficer>& officers,
        CrewOptimizer* optimizer = nullptr,
        AiStreamCallback stream_cb = nullptr);

    // Free-form question with account context
    LlmResponse ask_question(
        const std::string& question,
        const PlayerData& player_data,
        const GameData& game_data,
        const std::vector<ClassifiedOfficer>& officers,
        AiStreamCallback stream_cb = nullptr);

    // -------------------------------------------------------------------
    // Config access
    // -------------------------------------------------------------------

    const AiConfig& config() const { return config_; }
    AiConfig& config() { return config_; }
    bool save_config();

private:
    AiConfig config_;
    std::string config_path_;

    std::unique_ptr<SshTunnel> tunnel_;
    std::unique_ptr<LlmClient> client_;
    std::unique_ptr<CrewAdvisor> advisor_;
    std::unique_ptr<LlmClient> gemini_client_;  // Dedicated Gemini client for META queries

    AiHistoryStore history_;
    MetaCache meta_cache_;                       // Cached Gemini META knowledge

    bool initialized_ = false;
    bool is_fallback_ = false;
    std::string primary_error_;
    std::string tunnel_status_;

    // Build a snapshot for legacy query methods
    AccountSnapshot build_snapshot(
        const PlayerData& player_data,
        const GameData& game_data,
        const std::vector<ClassifiedOfficer>& officers,
        Scenario scenario,
        ShipType ship_type,
        const std::set<std::string>& excluded = {}) const;

    // Parse crew recommendations from raw LLM response
    // (duplicated from CrewAdvisor so the group pipeline doesn't need the advisor)
    std::vector<AiCrewRecommendation> parse_group_response(const std::string& content) const;

    // Build META-filtered officer groups (uses meta_cache_ if available)
    std::vector<OfficerGroup> build_meta_filtered_groups(
        const std::vector<ClassifiedOfficer>& officers) const;

    // Ensure Gemini client is available (lazy creation)
    bool ensure_gemini_client();
};

} // namespace stfc

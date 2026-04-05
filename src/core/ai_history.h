#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace stfc {

// ---------------------------------------------------------------------------
// AI History Store — persistent storage for AI query/response pairs
//
// Every LLM response is stored with:
//   - The group it was for (or "ask" / "progression" / "meta")
//   - The query timestamp
//   - The raw response content
//   - A user rating: unrated / good / bad
//   - The model that produced it
//
// Good-rated responses are injected as context into future queries for the
// same group, creating an iterative refinement loop. Over time, the AI
// learns what the user considers good advice (within the session/history).
//
// Storage: data/ai_history.json — simple JSON array, appended to.
// ---------------------------------------------------------------------------

// User rating for an AI response
enum class AiRating {
    Unrated = 0,    // Not yet rated
    Good = 1,       // Thumbs up — inject as context in future queries
    Bad = -1,       // Thumbs down — exclude from future context
};

// A single history entry
struct AiHistoryEntry {
    std::string id;                // Unique ID (timestamp-based)
    std::string group;             // Group name: "PvP Combat", "ask", "progression", etc.
    std::string query_type;        // "group_crew", "crew", "progression", "meta", "ask"
    std::string model;             // Model that produced the response
    std::string prompt_summary;    // First ~200 chars of user prompt (for display)
    std::string response;          // Full response content
    AiRating rating = AiRating::Unrated;
    int64_t timestamp = 0;         // Unix epoch seconds
    int input_tokens = 0;
    int output_tokens = 0;
};

// ---------------------------------------------------------------------------
// AiHistoryStore — thread-safe read/write of the history file
// ---------------------------------------------------------------------------

class AiHistoryStore {
public:
    explicit AiHistoryStore(const std::string& path = "data/ai_history.json");

    // Load all entries from disk (call once at startup)
    bool load();

    // Save all entries to disk
    bool save() const;

    // Add a new entry (auto-assigns ID and timestamp, auto-saves)
    std::string add_entry(const AiHistoryEntry& entry);

    // Rate an entry by ID
    bool rate_entry(const std::string& id, AiRating rating);

    // Get all entries for a group (newest first)
    std::vector<AiHistoryEntry> entries_for_group(const std::string& group) const;

    // Get only good-rated entries for a group (for context injection)
    std::vector<AiHistoryEntry> good_entries_for_group(const std::string& group,
                                                         int max_entries = 3) const;

    // Get the most recent entry (for rating from TUI)
    const AiHistoryEntry* latest_entry() const;

    // Get entry by ID
    const AiHistoryEntry* find_entry(const std::string& id) const;

    // Get all entries (newest first)
    const std::vector<AiHistoryEntry>& all_entries() const { return entries_; }

    // Get count of entries
    int count() const { return static_cast<int>(entries_.size()); }

    // Clear all history
    void clear();

private:
    std::string path_;
    std::vector<AiHistoryEntry> entries_;
    mutable std::mutex mutex_;

    static std::string generate_id();
};

// ---------------------------------------------------------------------------
// Build context string from good-rated prior responses
//
// This is injected into the system prompt to give the LLM examples of
// responses the user liked. Format:
//
//   PRIOR GOOD RESPONSES (the user rated these highly):
//   ---
//   [Response 1 content, truncated to ~500 chars]
//   ---
//   [Response 2 content, truncated to ~500 chars]
//
// Returns empty string if no good entries exist for this group.
// ---------------------------------------------------------------------------

std::string build_history_context(const AiHistoryStore& store,
                                   const std::string& group,
                                   int max_entries = 2,
                                   int max_chars_per_entry = 500);

} // namespace stfc

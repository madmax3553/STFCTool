#include "core/ai_history.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>

#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ===========================================================================
// AiHistoryStore
// ===========================================================================

AiHistoryStore::AiHistoryStore(const std::string& path)
    : path_(path) {}

// ---------------------------------------------------------------------------
// Generate a unique ID: timestamp + random suffix
// ---------------------------------------------------------------------------

std::string AiHistoryStore::generate_id() {
    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1000, 9999);

    std::ostringstream ss;
    ss << epoch << "-" << dist(rng);
    return ss.str();
}

// ---------------------------------------------------------------------------
// JSON serialization helpers
// ---------------------------------------------------------------------------

static json entry_to_json(const AiHistoryEntry& e) {
    json j;
    j["id"] = e.id;
    j["group"] = e.group;
    j["query_type"] = e.query_type;
    j["model"] = e.model;
    j["prompt_summary"] = e.prompt_summary;
    j["response"] = e.response;
    j["rating"] = static_cast<int>(e.rating);
    j["timestamp"] = e.timestamp;
    j["input_tokens"] = e.input_tokens;
    j["output_tokens"] = e.output_tokens;
    return j;
}

static AiHistoryEntry entry_from_json(const json& j) {
    AiHistoryEntry e;
    if (j.contains("id") && j["id"].is_string())
        e.id = j["id"].get<std::string>();
    if (j.contains("group") && j["group"].is_string())
        e.group = j["group"].get<std::string>();
    if (j.contains("query_type") && j["query_type"].is_string())
        e.query_type = j["query_type"].get<std::string>();
    if (j.contains("model") && j["model"].is_string())
        e.model = j["model"].get<std::string>();
    if (j.contains("prompt_summary") && j["prompt_summary"].is_string())
        e.prompt_summary = j["prompt_summary"].get<std::string>();
    if (j.contains("response") && j["response"].is_string())
        e.response = j["response"].get<std::string>();
    if (j.contains("rating") && j["rating"].is_number_integer())
        e.rating = static_cast<AiRating>(j["rating"].get<int>());
    if (j.contains("timestamp") && j["timestamp"].is_number())
        e.timestamp = j["timestamp"].get<int64_t>();
    if (j.contains("input_tokens") && j["input_tokens"].is_number_integer())
        e.input_tokens = j["input_tokens"].get<int>();
    if (j.contains("output_tokens") && j["output_tokens"].is_number_integer())
        e.output_tokens = j["output_tokens"].get<int>();
    return e;
}

// ---------------------------------------------------------------------------
// Load / Save
// ---------------------------------------------------------------------------

bool AiHistoryStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream f(path_);
    if (!f.is_open()) return false;

    try {
        json j = json::parse(f);
        entries_.clear();
        if (j.is_array()) {
            for (const auto& item : j) {
                entries_.push_back(entry_from_json(item));
            }
        }
        // Sort newest first
        std::sort(entries_.begin(), entries_.end(),
            [](const AiHistoryEntry& a, const AiHistoryEntry& b) {
                return a.timestamp > b.timestamp;
            });
        return true;
    } catch (...) {
        return false;
    }
}

bool AiHistoryStore::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    json arr = json::array();
    for (const auto& e : entries_) {
        arr.push_back(entry_to_json(e));
    }

    std::ofstream f(path_);
    if (!f.is_open()) return false;

    f << arr.dump(2);  // Pretty-print for debuggability
    return f.good();
}

// ---------------------------------------------------------------------------
// Add entry
// ---------------------------------------------------------------------------

std::string AiHistoryStore::add_entry(const AiHistoryEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    AiHistoryEntry e = entry;
    e.id = generate_id();

    if (e.timestamp == 0) {
        e.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Insert at front (newest first)
    entries_.insert(entries_.begin(), e);

    // Cap total history at 200 entries to prevent unbounded growth
    if (entries_.size() > 200) {
        entries_.resize(200);
    }

    // Auto-save (unlock mutex first to avoid deadlock with save's lock)
    // Actually save() also locks — we need to call the internal save without locking
    {
        json arr = json::array();
        for (const auto& entry : entries_) {
            arr.push_back(entry_to_json(entry));
        }
        std::ofstream f(path_);
        if (f.is_open()) {
            f << arr.dump(2);
        }
    }

    return e.id;
}

// ---------------------------------------------------------------------------
// Rate an entry
// ---------------------------------------------------------------------------

bool AiHistoryStore::rate_entry(const std::string& id, AiRating rating) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& e : entries_) {
        if (e.id == id) {
            e.rating = rating;

            // Auto-save
            json arr = json::array();
            for (const auto& entry : entries_) {
                arr.push_back(entry_to_json(entry));
            }
            std::ofstream f(path_);
            if (f.is_open()) {
                f << arr.dump(2);
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<AiHistoryEntry> AiHistoryStore::entries_for_group(const std::string& group) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AiHistoryEntry> result;
    for (const auto& e : entries_) {
        if (e.group == group) result.push_back(e);
    }
    return result;
}

std::vector<AiHistoryEntry> AiHistoryStore::good_entries_for_group(
    const std::string& group, int max_entries) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AiHistoryEntry> result;
    for (const auto& e : entries_) {
        if (e.group == group && e.rating == AiRating::Good) {
            result.push_back(e);
            if (static_cast<int>(result.size()) >= max_entries) break;
        }
    }
    return result;
}

const AiHistoryEntry* AiHistoryStore::latest_entry() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.empty() ? nullptr : &entries_[0];
}

const AiHistoryEntry* AiHistoryStore::find_entry(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& e : entries_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void AiHistoryStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();

    // Save empty
    std::ofstream f(path_);
    if (f.is_open()) f << "[]";
}

// ===========================================================================
// Build context from good-rated prior responses
// ===========================================================================

std::string build_history_context(const AiHistoryStore& store,
                                   const std::string& group,
                                   int max_entries,
                                   int max_chars_per_entry)
{
    auto good = store.good_entries_for_group(group, max_entries);
    if (good.empty()) return "";

    std::ostringstream ss;
    ss << "\n\nPRIOR GOOD RESPONSES (the user rated these highly — use them as reference):\n";

    for (size_t i = 0; i < good.size(); ++i) {
        ss << "--- Response " << (i + 1) << " ---\n";
        const auto& resp = good[i].response;
        if (static_cast<int>(resp.size()) <= max_chars_per_entry) {
            ss << resp;
        } else {
            ss << resp.substr(0, max_chars_per_entry) << "...[truncated]";
        }
        ss << "\n";
    }

    ss << "---\n"
       << "Build on what worked in these prior responses. Keep the same style and quality.\n";

    return ss.str();
}

} // namespace stfc

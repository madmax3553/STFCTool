#include "util/csv_import.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace stfc {

// ---------------------------------------------------------------------------
// RFC 4180 CSV parser — handles quoted fields with embedded newlines/commas
// ---------------------------------------------------------------------------

namespace {

// Parse a single CSV record (which may span multiple lines due to quoted fields).
// Returns false at EOF.
bool read_csv_record(std::istream& in, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool in_quotes = false;
    bool field_started = false;
    int c;

    while ((c = in.get()) != EOF) {
        if (in_quotes) {
            if (c == '"') {
                int next = in.peek();
                if (next == '"') {
                    // Escaped quote ""
                    in.get();
                    field += '"';
                } else {
                    // End of quoted field
                    in_quotes = false;
                }
            } else {
                field += static_cast<char>(c);
            }
        } else {
            if (c == ',' ) {
                fields.push_back(field);
                field.clear();
                field_started = false;
            } else if (c == '\n') {
                fields.push_back(field);
                return true;  // end of record
            } else if (c == '\r') {
                // Handle \r\n
                if (in.peek() == '\n') in.get();
                fields.push_back(field);
                return true;
            } else if (c == '"' && !field_started) {
                in_quotes = true;
                field_started = true;
            } else {
                field += static_cast<char>(c);
                field_started = true;
            }
        }
    }

    // EOF reached — emit last field if we got any data
    if (!field.empty() || !fields.empty()) {
        fields.push_back(field);
        return true;
    }
    return false;
}

// Strip whitespace from both ends
std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Strip commas and % from a string, then parse as double
double parse_number(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (char c : raw) {
        if (c != ',' && c != '%') s += c;
    }
    s = trim(s);
    if (s.empty()) return 0.0;
    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

// Parse integer from a string that may be float-formatted (e.g., "5.0")
int parse_int(const std::string& raw) {
    std::string s = trim(raw);
    if (s.empty()) return 0;
    try {
        return static_cast<int>(std::stod(s));
    } catch (...) {
        return 0;
    }
}

// Lowercase a string
std::string to_lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<RosterOfficer> load_roster_csv(const std::string& path) {
    std::vector<RosterOfficer> result;
    std::ifstream file(path);
    if (!file.is_open()) return result;

    // Skip first 19 rows (header/config area)
    std::vector<std::string> fields;
    for (int i = 0; i < 19; ++i) {
        if (!read_csv_record(file, fields)) return result;
    }

    // Parse data rows
    while (read_csv_record(file, fields)) {
        if (fields.size() < 12) continue;

        // Column 2 = officer name
        std::string name = trim(fields.size() > 2 ? fields[2] : "");
        if (name.empty()) continue;

        // Column 5 = attack — skip officers with no attack
        double attack = (fields.size() > 5) ? parse_number(fields[5]) : 0.0;
        if (attack <= 0.0) continue;

        RosterOfficer officer;
        officer.name = name;

        // Column 1 = rarity letter (C, U, R, E)
        if (fields.size() > 1) {
            std::string r = trim(fields[1]);
            if (!r.empty()) {
                officer.rarity = static_cast<char>(std::toupper(static_cast<unsigned char>(r[0])));
            }
        }

        officer.level   = (fields.size() > 3) ? parse_int(fields[3]) : 0;
        officer.rank    = (fields.size() > 4) ? parse_int(fields[4]) : 0;
        officer.attack  = attack;
        officer.defense = (fields.size() > 6) ? parse_number(fields[6]) : 0.0;
        officer.health  = (fields.size() > 7) ? parse_number(fields[7]) : 0.0;

        // Column 8 = officer group (Python skips this, but we capture it)
        if (fields.size() > 8) officer.group = trim(fields[8]);

        // Column 9 = CM/BDA percentage
        officer.cm_pct = (fields.size() > 9) ? parse_number(fields[9]) : 0.0;

        // Column 10 = OA percentage
        officer.oa_pct = (fields.size() > 10) ? parse_number(fields[10]) : 0.0;

        // Column 11 = status effect
        if (fields.size() > 11) officer.effect = to_lower(trim(fields[11]));

        // Column 12 = causes effect (Y/empty)
        if (fields.size() > 12) {
            std::string cause = trim(fields[12]);
            officer.causes_effect = (!cause.empty() && (cause[0] == 'Y' || cause[0] == 'y'));
        }

        // Column 13 = player uses (Y/empty)
        if (fields.size() > 13) {
            std::string use = trim(fields[13]);
            officer.player_uses = (!use.empty() && (use[0] == 'Y' || use[0] == 'y'));
        }

        // Column 14 = full ability description
        if (fields.size() > 14) officer.description = to_lower(trim(fields[14]));

        result.push_back(std::move(officer));
    }

    return result;
}

int parse_mess_hall_level(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return 0;

    // Row 18 (0-indexed 17) contains "Crew Level (Mess Hall) = NNNN"
    std::vector<std::string> fields;
    for (int i = 0; i < 18; ++i) {
        if (!read_csv_record(file, fields)) return 0;
    }

    // The 18th row has been read into fields — search for the mess hall text
    for (const auto& f : fields) {
        // Look for "= NNNN" pattern
        auto pos = f.find("Mess Hall");
        if (pos == std::string::npos) continue;

        auto eq_pos = f.find('=', pos);
        if (eq_pos == std::string::npos) continue;

        std::string num_str = trim(f.substr(eq_pos + 1));
        return parse_int(num_str);
    }
    return 0;
}

} // namespace stfc

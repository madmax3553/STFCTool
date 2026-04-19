#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <vector>
#include <utility>

#include "ftxui/dom/elements.hpp"

namespace stfc {
namespace ui {

using namespace ftxui;

inline std::string fmt_num(int64_t n) {
    if (n >= 1000000000) return std::to_string(n / 1000000000) + "." + std::to_string((n % 1000000000) / 100000000) + "B";
    if (n >= 1000000) return std::to_string(n / 1000000) + "." + std::to_string((n % 1000000) / 100000) + "M";
    if (n >= 1000) return std::to_string(n / 1000) + "." + std::to_string((n % 1000) / 100) + "K";
    return std::to_string(n);
}

inline std::string fmt_dur(int seconds) {
    if (seconds <= 0) return "-";
    int d = seconds / 86400;
    int h = (seconds % 86400) / 3600;
    int m = (seconds % 3600) / 60;
    if (d > 0) return std::to_string(d) + "d" + std::to_string(h) + "h";
    if (h > 0) return std::to_string(h) + "h" + std::to_string(m) + "m";
    return std::to_string(m) + "m" + std::to_string(seconds % 60) + "s";
}

inline std::string fmt_time(std::chrono::system_clock::time_point tp) {
    if (tp == std::chrono::system_clock::time_point{}) return "never";
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tb{};
    localtime_r(&tt, &tb);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tb.tm_hour, tb.tm_min, tb.tm_sec);
    return buf;
}

inline std::string fmt_date(int64_t epoch) {
    if (epoch <= 0) return "-";
    auto tt = static_cast<time_t>(epoch);
    std::tm tb{};
    localtime_r(&tt, &tb);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d", tb.tm_mon + 1, tb.tm_mday, tb.tm_hour, tb.tm_min);
    return buf;
}

inline int64_t now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string trunc(const std::string& s, int n) {
    if ((int)s.size() <= n) return s;
    return n > 2 ? s.substr(0, n - 2) + ".." : s.substr(0, n);
}

// Pad or truncate string to exact width
inline std::string pad(const std::string& s, int w) {
    if ((int)s.size() >= w) return s.substr(0, w);
    return s + std::string(w - s.size(), ' ');
}

// Borderless panel: colored title + separator + content
inline Element panel(const std::string& title, Element content) {
    return vbox({
        text(" " + title) | bold | color(Color::Cyan),
        separator(),
        std::move(content),
    });
}

inline Element panel(const std::string& title, Elements lines) {
    return panel(title, vbox(std::move(lines)));
}

// Table header row
struct ColDef { std::string label; int width; };

inline Element tbl_header(std::initializer_list<ColDef> cols) {
    Elements cells;
    for (auto& c : cols) {
        cells.push_back(text(c.label) | bold | dim | size(WIDTH, EQUAL, c.width));
    }
    return hbox(std::move(cells));
}

} // namespace ui
} // namespace stfc

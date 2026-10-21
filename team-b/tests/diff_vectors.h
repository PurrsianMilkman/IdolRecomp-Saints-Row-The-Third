// Reader for the Team D differential-vector markdown tables (spec-differential-vectors-*.md).
// SR3_SPEC_DIR is the repo root (set by CMake). Rows are returned as trimmed cells; header/separator rows are skipped
// by the caller (cells that do not parse as numbers).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace dv {
using Row = std::vector<std::string>;

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// All table rows of `file`; if `sectionPrefix` is non-empty only rows between the heading line starting with it and the next
// line starting with "## " are returned.
inline std::vector<Row> rows(const char* file, const std::string& sectionPrefix = "") {
    std::vector<Row> out;
    std::ifstream in(std::string(SR3_SPEC_DIR) + "/" + file);
    if (!in) { std::printf("cannot open %s\n", file); return out; }
    std::string line;
    bool active = sectionPrefix.empty();
    while (std::getline(in, line)) {
        if (!sectionPrefix.empty()) {
            if (line.rfind(sectionPrefix, 0) == 0) { active = true; continue; }
            if (active && line.rfind("## ", 0) == 0) active = false;
        }
        if (!active || line.empty() || line[0] != '|') continue;
        Row r;
        size_t p = 1;
        while (p < line.size()) {
            size_t q = line.find('|', p);
            if (q == std::string::npos) break;
            r.push_back(trim(line.substr(p, q - p)));
            p = q + 1;
        }
        out.push_back(r);
    }
    return out;
}

inline bool num(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* e = nullptr;
    v = std::strtod(s.c_str(), &e);
    return e && *e == 0;
}

inline bool numf(const std::string& s, float& v) {
    if (s.empty()) return false;
    char* e = nullptr;
    v = std::strtof(s.c_str(), &e);
    return e && *e == 0;
}
}  // namespace dv

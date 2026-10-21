// Team D differential vectors against sr3xtbl and vpp::hashFilename:
//   D-5  rotate-left-6 / XOR name hash, bytes are signed        (spec-differential-results.md D-5)
//   D-14 table float grammar                                    (spec-differential-vectors-tables.md T1-T3)
//   D-16 table XML document parser                              (spec-differential-vectors-xml.md)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "diff_vectors.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/hash.h"

namespace {

std::string stripTicks(const std::string& s) {
    if (s.size() >= 2 && s.front() == '`' && s.back() == '`') return s.substr(1, s.size() - 2);
    return s;
}

std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o.push_back(s[i]); continue; }
        const char c = s[++i];
        if (c == 'r') o.push_back('\r');
        else if (c == 'n') o.push_back('\n');
        else if (c == 't') o.push_back('\t');
        else if (c == '\\') o.push_back('\\');
        else if (c == 'x' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            o.push_back(static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        } else { o.push_back('\\'); o.push_back(c); }
    }
    return o;
}

std::string repr(const std::string& t) {
    std::string o = "'";
    for (unsigned char c : t) {
        char b[8];
        if (c == '\r') o += "\\r";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\'') o += "\\'";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20 || c >= 0x7f) { std::snprintf(b, sizeof b, "\\x%02x", c); o += b; }
        else o.push_back(static_cast<char>(c));
    }
    return o + "'";
}

std::string tree(const sr3xtbl::Node* n) {
    std::string o = n->name();
    if (const std::string* t = n->text()) o += "=" + repr(*t);
    if (!n->children().empty()) {
        o += "{";
        bool first = true;
        for (const sr3xtbl::Node* c : n->children()) {
            if (!first) o += ", ";
            first = false;
            o += tree(c);
        }
        o += "}";
    }
    return o;
}

int g_fail = 0, g_total = 0;

void floatVector(const std::string& textCell, const std::string& expCell, const char* tag) {
    float exp;
    if (!dv::numf(expCell, exp)) return;
    std::string text = stripTicks(textCell);
    if (text == "(empty)") text.clear();
    ++g_total;
    const float got = sr3xtbl::ParseFloat(text);
    uint32_t a, b;
    std::memcpy(&a, &got, 4);
    std::memcpy(&b, &exp, 4);
    if (a != b) {
        if (++g_fail <= 40) std::printf("FAIL %s float '%s': got %.9g (%08x) expected %s (%08x)\n", tag, text.c_str(), got, a, expCell.c_str(), b);
    }
}

void hashVector(const std::string& bytes, uint32_t expected) {
    ++g_total;
    const uint32_t got = vpp::hashFilename(bytes);
    if (got != expected) {
        ++g_fail;
        std::printf("FAIL D-5 hash of %zu bytes (first %02x): got %u expected %u\n", bytes.size(), bytes.empty() ? 0 : static_cast<unsigned char>(bytes[0]), got, expected);
    }
}

}  // namespace

int main() {
    // ---- D-5 ----
    hashVector(std::string("\x80", 1), 4294967168u);
    hashVector(std::string("\xff", 1), 4294967295u);
    hashVector(std::string("caf\xe9", 4), 4269143657u);
    hashVector(std::string("\xc0\xe0", 2), 4063u);
    // ASCII names are unaffected (rotate-left-6 / XOR with A-Z folded).
    {
        uint32_t h = 0;
        const std::string s = "tint_color";
        for (char c : s) h = ((h << 6) | (h >> 26)) ^ static_cast<uint32_t>(c);
        hashVector(s, h);
    }
    hashVector("", 0u);

    // ---- D-14 ----
    for (const auto& r : dv::rows("spec-differential-vectors-tables.md", "## T1.")) if (r.size() >= 3) floatVector(r[0], r[1], "T1");
    for (const auto& r : dv::rows("spec-differential-vectors-tables.md", "## T2.")) if (r.size() >= 3) floatVector(r[0], r[1], "T2");
    for (const auto& r : dv::rows("spec-differential-vectors-tables.md", "## T3.")) if (r.size() >= 4) floatVector(r[0], r[2], "T3");

    // ---- D-16 ----
    for (const auto& r : dv::rows("spec-differential-vectors-xml.md")) {
        if (r.size() < 3 || r[0].empty() || r[0][0] != '`') continue;
        std::string doc = stripTicks(r[0]);
        if (doc == "(empty)") doc.clear();
        doc = unescape(doc);
        const std::string& exp = r[1];
        ++g_total;
        std::string got;
        try {
            sr3xtbl::Document d = sr3xtbl::ParseDocument(doc);
            got = tree(d.root());
        } catch (const std::exception&) {
            got = "whole file rejected";
        }
        if (got != exp) {
            if (++g_fail <= 60) std::printf("FAIL D-16 doc [%s]: got [%s] expected [%s]\n", r[0].c_str(), got.c_str(), exp.c_str());
        }
    }

    std::printf("differential tables vectors: %d, failures: %d\n", g_total, g_fail);
    return (g_fail || g_total < 400) ? 1 : 0;
}

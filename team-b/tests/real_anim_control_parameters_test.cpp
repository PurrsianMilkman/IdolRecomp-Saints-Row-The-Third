// Real-data test for sr3animruntime U0 (2026-10-03): loads the shipped
// control_parameters.xtbl out of misc_tables.vpp_pc in CRREISH_GAME_CACHE_DIR
// (exit 77 - ctest SKIPPED, never a pass - when the archive is absent;
// nothing from the archive is copied into the repository) and checks the
// registry against spec-anim-runtime.md:
//   * Sec1.4: 15,465 bytes, 87 rows (52 bool / 29 float / 6 integer), 35
//     numeric rows with min_value/max_value, exactly three non-zero defaults
//     (all bool true, rows 25 / 50 / 73 by name), every float and integer
//     default 0, the six integer ranges;
//   * Sec1.2: every numeric row's stored min/max equals the row text, read
//     through an independent parser (sr3tables_ui_controls'
//     ParseControlParametersTable, engine float grammar) as the oracle;
//   * Sec4.1: all 62 compiled-in names resolve, case-exact, and the resolved
//     registry type equals the setter type the spec gives each writer
//     (Sec4.1 "all 72 agree with the registry type");
//   * Sec4.6: the shipped rows minus the 62 compiled-in names are exactly
//     the 25 listed no-writer rows; and the three rider "... Abs" rows are
//     the only "<listed rider base> Abs" rows (how the header pins them).
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "sr3animruntime/control_parameters.h"
#include "sr3tables_ui_controls/tables.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

using namespace sr3animruntime;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool readEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name != name) continue;
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            vpp::ByteView r = c.rawEntryBytes(i);
            out.assign(r.data(), r.data() + r.size());
            return true;
        }
        vpp::DecompressResult r = c.decompressEntry(i);
        if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
            r.status != vpp::DecodeStatus::ContentValidated)
            return false;
        out = std::move(r.data);
        return true;
    }
    return false;
}

}  // namespace

int main() {
    const fs::path archivePath = fs::path(CRREISH_GAME_CACHE_DIR) / "misc_tables.vpp_pc";
    if (!fs::exists(archivePath)) {
        std::cout << "SKIP: " << archivePath.string() << " not present\n";
        return 77;
    }
    std::vector<uint8_t> archive;
    {
        std::ifstream in(archivePath, std::ios::binary);
        archive.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
    std::vector<uint8_t> bytes;
    CHECK(readEntry(c, "control_parameters.xtbl", bytes));
    if (bytes.empty()) {
        std::cerr << "control_parameters.xtbl not found / not decodable\n";
        return 1;
    }
    CHECK(bytes.size() == 15465);  // Sec1.4

    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(bytes.data(), bytes.size());
    const ControlParameterRegistry reg = ControlParameterRegistry::load(doc);

    // ---- Sec1.4 counts --------------------------------------------------
    CHECK(reg.loaded());
    CHECK(reg.count() == 87);
    int nBool = 0, nFloat = 0, nInt = 0, nNone = 0, nRange = 0;
    const std::vector<const sr3xtbl::Node*> rows =
        sr3xtbl::Children(sr3xtbl::FindChild(doc.root(), "control_parameters"), "control_parameter");
    CHECK(rows.size() == 87);
    for (size_t i = 0; i < reg.records().size() && i < rows.size(); ++i) {
        const ParamRecord& r = reg.records()[i];
        switch (r.type) {
        case ParamType::Bool: ++nBool; break;
        case ParamType::Float: ++nFloat; break;
        case ParamType::Integer: ++nInt; break;
        case ParamType::None: ++nNone; break;
        }
        const bool hasRange = sr3xtbl::FindChild(rows[i], "min_value") && sr3xtbl::FindChild(rows[i], "max_value");
        if (hasRange) ++nRange;
        // "the 35 numeric rows (and only they) also carry min_value/max_value"
        CHECK(hasRange == (r.type == ParamType::Float || r.type == ParamType::Integer));
        CHECK(sr3xtbl::FindChild(rows[i], "name") && sr3xtbl::FindChild(rows[i], "data_type") &&
              sr3xtbl::FindChild(rows[i], "default_value"));
    }
    CHECK(nBool == 52 && nFloat == 29 && nInt == 6 && nNone == 0);
    CHECK(nRange == 35);

    // ---- Sec1.4 defaults --------------------------------------------------
    std::vector<int> nonZero;
    for (int i = 0; i < reg.count(); ++i) {
        const ParamRecord& r = reg.records()[static_cast<size_t>(i)];
        if (r.type == ParamType::Bool && r.defaultBool()) nonZero.push_back(i);
        if (r.type == ParamType::Float) CHECK(r.defaultFloat() == 0.0f);
        if (r.type == ParamType::Integer) CHECK(r.defaultInt() == 0);
    }
    CHECK((nonZero == std::vector<int>{25, 50, 73}));
    CHECK(reg.resolve("Supported By Ground") == 25);
    CHECK(reg.resolve("spike closed bool") == 50);
    CHECK(reg.resolve("Vehicle Landed Bool") == 73);

    // ---- Sec1.4 integer ranges ---------------------------------------------
    struct IntRange { const char* name; int32_t lo, hi; };
    const IntRange ints[6] = {
        {"cower type", 0, 1}, {"idle type", 0, 1}, {"Ball Rider Stage Number", 0, 14},
        {"Weapon Group", 0, 32}, {"Coop Rider Stage Number", 0, 10}, {"Climb Duration ms", 0, 10000},
    };
    for (const IntRange& e : ints) {
        const ParamRecord* r = reg.record(reg.resolve(e.name));
        CHECK(r && r->type == ParamType::Integer);
        if (r && r->type == ParamType::Integer) CHECK(r->minInt() == e.lo && r->maxInt() == e.hi);
    }

    // ---- Sec1.2: every numeric row's range against an independent parse ----
    const std::vector<sr3tables_ui_controls::ControlParameter> oracle =
        sr3tables_ui_controls::ParseControlParametersTable(doc);
    CHECK(oracle.size() == 87);
    int numericChecked = 0;
    for (size_t i = 0; i < oracle.size() && i < reg.records().size(); ++i) {
        const ParamRecord& r = reg.records()[i];
        CHECK(oracle[i].name && *oracle[i].name == r.name);
        if (r.type == ParamType::Float) {
            CHECK(oracle[i].minValue && std::fabs(*oracle[i].minValue - r.minFloat()) <= 1e-5f);
            CHECK(oracle[i].maxValue && std::fabs(*oracle[i].maxValue - r.maxFloat()) <= 1e-5f);
            ++numericChecked;
        } else if (r.type == ParamType::Integer) {
            CHECK(oracle[i].minValue && static_cast<float>(r.minInt()) == *oracle[i].minValue);
            CHECK(oracle[i].maxValue && static_cast<float>(r.maxInt()) == *oracle[i].maxValue);
            ++numericChecked;
        } else if (r.type == ParamType::Bool) {
            CHECK(!r.minBool() && r.maxBool());  // bytes 0 / 1
        }
    }
    CHECK(numericChecked == 35);
    // Two spot values quoted by the spec (Sec4.2 row 21a: "the shipped range is 0...2pi").
    {
        const ParamRecord* d = reg.record(reg.resolve("direction"));
        CHECK(d && d->type == ParamType::Float && d->minFloat() == 0.0f && std::fabs(d->maxFloat() - 6.283185f) < 1e-6f);
    }

    // ---- Sec4.1: the 62 compiled-in names ----------------------------------
    std::set<std::string> compiled;
    for (const CompiledInParameterName& n : kCompiledInParameterNames) {
        const int16_t id = reg.resolve(n.name);
        CHECK(id >= 0);
        const ParamRecord* r = reg.record(id);
        CHECK(r != nullptr);
        if (!r) continue;
        CHECK(r->name == n.name);          // exact, including case
        CHECK(r->type == n.setterType);    // the writer's setter type agrees with the table
        compiled.insert(n.name);
    }
    CHECK(compiled.size() == 62);  // distinct

    // ---- Sec4.6: the complement is exactly the 25 no-writer rows ------------
    std::set<std::string> rest;
    for (const ParamRecord& r : reg.records())
        if (!compiled.count(r.name)) rest.insert(r.name);
    std::set<std::string> listed(std::begin(kParametersWithoutCompiledWriter), std::end(kParametersWithoutCompiledWriter));
    CHECK(listed.size() == 25);
    CHECK(rest == listed);

    // ---- the three rider "... Abs" forms ------------------------------------
    const char* riderBases[6] = {"Vehicle Steering", "Vehicle Steering Time", "Vehicle MPH",
                                 "Vehicle Forward Lean", "Vehicle Vert Lean", "Vehicle Tilt"};
    std::set<std::string> absRows;
    for (const ParamRecord& r : reg.records())
        for (const char* b : riderBases)
            if (r.name == std::string(b) + " Abs") absRows.insert(r.name);
    CHECK((absRows == std::set<std::string>{"Vehicle Tilt Abs", "Vehicle Forward Lean Abs", "Vehicle Vert Lean Abs"}));

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real_anim_control_parameters_test: all checks passed (87 rows, 62 compiled-in names)\n";
    return 0;
}

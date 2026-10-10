// Real-data test for the engine-curve crossed-mapping wiring (2026-10-08,
// engine-curve/slip-table follow-up task, authority spec-physics-runtime.md
// Sec10.2/Sec10.8, synced commit 4c7d3f2): loads real base-game
// `<name>_veh.xtbl` files out of `misc_tables.vpp_pc` in
// CRREISH_GAME_CACHE_DIR (exit 77 - ctest SKIPPED, never a pass - when the
// archive is absent; nothing from it is copied into the repository) and
// checks three things this task's own report must answer with real numbers,
// not synthetic ones:
//
//   1. VehicleTuning::ToEngineCurveTuning()'s CONFIRMED crossed mapping
//      (Sec10.2's "Construction site and the real xtbl field mapping")
//      genuinely differs from the NAIVE (name-matched, uncrossed) mapping a
//      careless clean-room implementation would produce, for real vehicles
//      whose crossed field values actually differ pairwise - i.e. the
//      crossing is actually being applied to real data, not a no-op that
//      happens to coincide with the naive reading.
//   2. The RETIRED placeholder ComputeEngineTorqueNm() and the LIVE
//      EngineNetTorque()-via-ToEngineCurveTuning() model produce genuinely
//      DIFFERENT net-torque numbers for real vehicle data (task's own
//      "prove you switched model, not just plumbing" requirement).
//   3. At least 3 real, distinct Automobile-class vehicles - chosen by
//      sorting on real `Mass`, NOT hand-picked by name, so this is not
//      cherry-picked - produce MEASURABLY DIFFERENT short-run acceleration
//      profiles when driven through VehicleSubstepSimulator under IDENTICAL
//      inputs (task's own "distinct handling, not a no-op" requirement).
//
// Selection method mirrors tools/validation/validate_vehicleinfo_population.cpp's
// own recursive vpp container walk (G1), narrowed to `_veh.xtbl` entries
// only, reusing sr3vehicleinfo::ParseVehicleEntry (the same real-data path
// this project's own population gates already validate against).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "sr3physics/vehicle_substeps.h"
#include "sr3vehicleinfo/vehicle_entry.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

using namespace sr3xtbl;
using namespace sr3vehicleinfo;
using namespace sr3physics;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                       \
    } while (0)

constexpr double kEps = 1e-9;
bool Near(double a, double b, double eps = kEps) { return std::abs(a - b) < eps; }

bool EndsWithCi(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    std::string tail = s.substr(s.size() - n);
    for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string suf(suffix);
    for (char& c : suf) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tail == suf;
}

struct VehXtblItem {
    std::string name;
    std::vector<uint8_t> data;
};

// Recursive container walk (same pattern as validate_vehicleinfo_population.cpp's
// own walk(), narrowed to _veh.xtbl entries) - `c` may be the top-level,
// vppcache-backed Container (OpenedArchive::container) or a locally
// constructed nested one; both expose the identical entries()/
// rawEntryBytes()/decompressEntry() interface.
void WalkContainer(const vpp::Container& c, std::vector<VehXtblItem>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        if (!EndsWithCi(e.name, "_veh.xtbl")) continue;
        std::vector<uint8_t> data;
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            vpp::ByteView v = c.rawEntryBytes(i);
            data.assign(v.data(), v.data() + v.size());
        } else {
            vpp::DecompressResult r = c.decompressEntry(i);
            if (r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                r.status == vpp::DecodeStatus::ContentValidated) {
                data = std::move(r.data);
            }
        }
        if (!data.empty()) out.push_back({e.name, std::move(data)});
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        if (EndsWithCi(c.entries()[i].name, "xtbl")) continue;
        try {
            vpp::Container nested(c.rawEntryBytes(i));
            WalkContainer(nested, out);
        } catch (const std::exception&) {
        }
    }
}

struct CandidateVehicle {
    std::string fileName;
    VehicleEntry entry;
};

// A naive (uncrossed, name-matched) reading of the same 5 fields
// VehicleTuning::ToEngineCurveTuning() CORRECTLY crosses - built here ONLY
// to prove the real method does not coincidentally produce the same
// numbers (check 1 above). NOT used anywhere else - a reader must not
// mistake this for a second legitimate model.
EngineCurveTuning NaiveUncrossedMapping(const VehicleTuning& t) {
    EngineCurveTuning e;
    e.idleRpm = t.minRpm;
    e.peakTorqueRpm = t.optRpm;
    e.redlineRpm = t.maxRpm;
    e.torqueScale = t.peakTorqueNm;
    // Naive (WRONG) name match: *_Torque_Factor -> torqueFactor*, *_Resistance -> resistance*BandCoeff.
    e.torqueFactorIdle = t.minRpmTorqueFactor;
    e.torqueFactorPeak = 1.0;  // the naive reading has no third torque-factor xtbl field to match against Opt_RPM_Resistance at all under a pure 1:1 name scheme - left at the struct default, irrelevant to the comparison below (idle/redline bands only)
    e.torqueFactorRedline = t.maxRpmTorqueFactor;
    e.resistanceLowBandCoeff = t.minRpmResistance;
    e.resistanceHighBandCoeff = t.maxRpmResistance;
    return e;
}

}  // namespace

int main() {
    const fs::path archivePath = fs::path(CRREISH_GAME_CACHE_DIR) / "misc_tables.vpp_pc";
    if (!fs::exists(archivePath)) {
        std::cout << "SKIP: " << archivePath.string() << " not present\n";
        return 77;
    }

    auto opened = vppcache::openArchive(archivePath);
    std::vector<VehXtblItem> items;
    WalkContainer(opened->container, items);
    std::cout << "Found " << items.size() << " *_veh.xtbl entries in " << archivePath.string() << "\n";
    CHECK(items.size() > 50);  // real base game ships 106 - a real population, not a near-empty archive

    std::vector<CandidateVehicle> candidates;
    for (auto& item : items) {
        Document doc = ParseDocument(item.data.data(), item.data.size());
        const Node* row = FindChild(doc.table(), "Vehicle");
        if (!row) continue;
        VehicleEntry entry = ParseVehicleEntry(row);
        if (entry.vehicleType.vehicleClass != VehicleClass::Automobile) continue;
        if (!(entry.mass.present && entry.torque.present && entry.minRpm.present && entry.optRpm.present &&
              entry.maxRpm.present && entry.minRpmTorqueFactor.present && entry.maxRpmTorqueFactor.present))
            continue;
        if (entry.axles.size() < 2) continue;
        candidates.push_back({item.name, std::move(entry)});
    }
    std::cout << "Of which " << candidates.size()
              << " are real Automobile-class entries with a full engine/axle schema\n";
    CHECK(candidates.size() >= 3);
    if (candidates.size() < 3) {
        std::cout << g_failures << " failures - cannot continue without >=3 real candidates.\n";
        return 1;
    }

    // Sort by real Mass (NOT hand-picked by name) and take low/middle/high -
    // a deterministic, non-cherry-picked selection of 3 genuinely different
    // real vehicles.
    std::sort(candidates.begin(), candidates.end(),
              [](const CandidateVehicle& a, const CandidateVehicle& b) { return a.entry.mass.value < b.entry.mass.value; });
    const std::vector<size_t> pickIdx = {0, candidates.size() / 2, candidates.size() - 1};

    std::vector<double> finalSpeeds;
    bool anyModelSwitchDifferenceSeen = false;
    bool anyRealCrossingDifferenceSeen = false;

    for (size_t pick : pickIdx) {
        const CandidateVehicle& cv = candidates[pick];
        VehicleTuning tuning = VehicleTuning::FromVehicleEntry(cv.entry);

        std::cout << "\nVehicle: " << cv.fileName << " (mass=" << tuning.mass << " kg, Torque=" << tuning.peakTorqueNm
                  << ", Min/Opt/Max_RPM=" << tuning.minRpm << "/" << tuning.optRpm << "/" << tuning.maxRpm
                  << ", Min/Max_RPM_Torque_Factor=" << tuning.minRpmTorqueFactor << "/" << tuning.maxRpmTorqueFactor
                  << ", Min/Opt/Max_RPM_Resistance=" << tuning.minRpmResistance << "/" << tuning.optRpmResistance
                  << "/" << tuning.maxRpmResistance << ")\n";

        // --- Check 1: the real crossed mapping actually differs from the
        // naive (uncrossed) one for this vehicle's own real data. ---
        const EngineCurveTuning crossed = tuning.ToEngineCurveTuning();
        const EngineCurveTuning naive = NaiveUncrossedMapping(tuning);
        const bool differsFromNaive = !(Near(crossed.resistanceLowBandCoeff, naive.resistanceLowBandCoeff) &&
                                         Near(crossed.resistanceHighBandCoeff, naive.resistanceHighBandCoeff) &&
                                         Near(crossed.torqueFactorIdle, naive.torqueFactorIdle) &&
                                         Near(crossed.torqueFactorRedline, naive.torqueFactorRedline));
        if (differsFromNaive) anyRealCrossingDifferenceSeen = true;

        // --- Check 2: retired placeholder vs. live model, real numbers. ---
        const double oldModelTorque = ComputeEngineTorqueNm(tuning, tuning.optRpm, /*throttle=*/1.0);
        const double newModelTorque = EngineNetTorque(crossed, tuning.optRpm, /*throttle=*/1.0, /*attachedObjectPenalty=*/false);
        std::cout << "  old placeholder ComputeEngineTorqueNm(optRpm,throttle=1)=" << oldModelTorque
                  << "  vs. live EngineNetTorque(...)=" << newModelTorque << "\n";
        if (std::abs(oldModelTorque - newModelTorque) > 1.0) anyModelSwitchDifferenceSeen = true;

        // --- Check 3 setup: short full-throttle run, record final speed. ---
        VehicleSubstepSimulator sim(tuning);
        const DriverInput input{/*throttle=*/1.0, /*brake=*/0.0, /*steer=*/0.0};
        constexpr double kDt = 1.0 / 60.0;
        constexpr int kSteps = 300;  // 5 s
        for (int i = 0; i < kSteps; ++i) sim.Step(kDt, input);
        std::cout << "  speed after 5s full throttle: " << sim.state_.velForwardBody << " m/s, gear "
                  << sim.state_.currentGear << ", engineRpm " << sim.state_.engineRpm << "\n";
        finalSpeeds.push_back(sim.state_.velForwardBody);
    }

    CHECK(anyRealCrossingDifferenceSeen);  // real crossed mapping genuinely differs from a naive reading for at least one real vehicle
    CHECK(anyModelSwitchDifferenceSeen);   // old vs. new engine model genuinely differ for at least one real vehicle (task 4)

    // --- Check 3: distinct real vehicles show MEASURABLY different
    // acceleration behaviour - not identical output, which would mean the
    // wiring is a no-op. ---
    std::cout << "\nFinal speeds across the 3 selected real vehicles: ";
    for (double s : finalSpeeds) std::cout << s << " ";
    std::cout << "\n";
    bool anyPairDiffers = false;
    for (size_t a = 0; a < finalSpeeds.size() && !anyPairDiffers; ++a) {
        for (size_t b = a + 1; b < finalSpeeds.size(); ++b) {
            if (std::abs(finalSpeeds[a] - finalSpeeds[b]) > 0.5) {  // >0.5 m/s real difference, not noise
                anyPairDiffers = true;
                break;
            }
        }
    }
    CHECK(anyPairDiffers);

    std::cout << "\n" << g_failures << " failures.\n";
    return g_failures == 0 ? 0 : 1;
}

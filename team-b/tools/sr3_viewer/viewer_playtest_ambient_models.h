#pragma once

// Ambient model variety (TEAM B free-roam task "ambient-model-variety", 2026-10-18).
//
// Before this file every near-field ambient car was drawn with the player demo vehicle's mesh
// (car_4dr_genki) and every ambient ped with the player's own skinned brad buffer (which is re-uploaded
// with the PLAYER's pose every frame - so ambient peds even mimicked the player's pose). This registry gives
// every ambient actor a real model chosen from its hood's spawn category.
//
// SPEC USED (spec-ai-runtime.md; the only source of engine facts here):
//   * 20.6.1(e) Vehicle model choice (PARTIAL in the spec, not FINAL as the audit said):
//       - a streamed set is built from the category's groups that have vehicles: one vehicle drawn
//         UNIFORMLY from the group's `Vehicles` list (duplicate rows weigh double);
//       - per spawn: candidates are the streamed models whose live count is below the group's
//         VehicleDayCap/VehicleNightCap; a candidate with the fewest duplicates among nearby live vehicles
//         wins, ties broken by the fewest live copies, then uniform. No probability weight at spawn time.
//   * 20.6.2(e) Ped character choice (FINAL for the rolls): slot kind (General ped / Special ped / Special
//       vehicle) drawn by the category's slot weights (+ day/night), then a group of that kind by a roulette
//       over DayChance/NightChance, then one character uniformly from the group.
//   * 20.6.0(b) day/night index: day_begin 630 / day_end 2200 (HIGH).
//   * 10.6 / 14.12 Spline_Type: six named classes, "All Roads"/"Surface Roads" store -1.
//
// CHOSEN (not in any spec; each is also a Team A question in the task report):
//   * the streamed-slot budget is OPEN in the spec: kVehicleStreamRolls / kPedStreamRolls rolls per category;
//   * distinct loaded models are capped (kMaxVehicleModels / kMaxPedModels); a roll that would exceed the cap
//     is only accepted if the model is already loaded;
//   * "nearby" for the duplicate count of 20.6.1(e) is kNearbyRadiusM;
//   * the vehicle `Variant` is not mapped to a paint/mesh variant: variant _0 is used (the group rows here carry
//     no Variant text; the handle's variant index semantics are in spec-vehicle-data.md, not wired);
//   * character-row -> mesh chain (character.xtbl `Character` -> character_definitions.xtbl `Model/Filename`,
//     `Heads/Head/Name`, `Heads/Head/Lod_Head`) is read from the shipped XML (the spec leaves the
//     character_definitions field reader undecompiled, spec-tables-customization.md 12.1);
//   * ped pose: static BIND POSE (a per-ped walk clip would need per-ped CPU skinning + re-upload; see the
//     report). Peds already face their movement direction (ActorPlacement::heading);
//   * Height/Height_Class scaling of ped meshes is not applied.

#include "viewer_platform.h"
#include "viewer_common.h"
#include "viewer_playtest_vehicle_rig.h"
#include "viewer_skinning.h"
#include "sr3geometry/character_matlib.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3mesh/mesh_block.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/texture_upload.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"
#include "sr3tables_progression/tables_rules.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <psapi.h>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace sr3viewer {

// One ambient ped model: its own body + head renderers (own vertex buffers, bind pose) and textures.
struct AmbientPedModel {
    std::string characterRow;  // character.xtbl row name (the spawn group's Character text)
    std::string definition;    // character_definitions.xtbl row name
    std::string stem;          // mesh stem, e.g. "alfred"
    std::unique_ptr<sr3render::MeshRenderer> body, head;
    std::vector<sr3render::UploadedTexture> bodyTex, headTex;
    std::vector<ID3D11ShaderResourceView*> bodySrv, headSrv;
    bool bodyTextured = false, headTextured = false;
    bool ready = false;
};

struct AmbientVehicleModel {
    std::string key;  // "<name>_<variant>"
    PlaytestVehicleRig rig;
    int uses = 0;
};

struct AmbientModelStats {
    size_t vehicleModelsLoaded = 0, pedModelsLoaded = 0;
    size_t vehicleLoadFailures = 0, pedLoadFailures = 0;
    size_t vehicleAssignments = 0, pedAssignments = 0;
    size_t cacheHits = 0, cacheMisses = 0;
    double loadMs = 0.0;
    size_t peakWorkingSetBytes = 0;
    size_t setsBuilt = 0;
    std::map<std::string, size_t> vehicleUse, pedUse;
};

class AmbientModelRegistry {
public:
    static constexpr size_t kMaxVehicleModels = 8;   // CHOSEN cap
    static constexpr size_t kMaxPedModels = 16;      // CHOSEN cap
    static constexpr int kVehicleStreamRolls = 12;   // CHOSEN (streamed-slot budget OPEN, 20.6.1(e))
    static constexpr int kPedStreamRolls = 24;       // CHOSEN (streamed-slot budget OPEN, 20.6.2(e))
    static constexpr double kNearbyRadiusM = 120.0;  // CHOSEN ("nearby" of 0x00913f20 not given a radius)

    // `hoodCategory(x, y, z)` returns the spawn category NAME for a world point (hood BSP lookup, 20.6.8).
    using HoodCategoryFn = std::function<std::string(double, double, double)>;

    bool ready() const { return ready_; }
    const AmbientModelStats& stats() const { return stats_; }

    bool init(ID3D11Device* device, const std::string& vehiclesArchivePath, const std::string& wheelsArchivePath,
              const MappedBytes* characterArchive, const std::function<sr3xtbl::Document(const std::string&)>& docOf,
              HoodCategoryFn hoodCategory) {
        device_ = device;
        wheelsPath_ = wheelsArchivePath;
        hoodCategory_ = std::move(hoodCategory);
        charBytes_ = characterArchive;
        try {
            for (const auto& c : sr3tables_progression::ParseSpawnInfoCategoriesTable(docOf("spawn_info_categories.xtbl")))
                if (c.name) categories_[lower(*c.name)] = c;
            for (const auto& g : sr3tables_progression::ParseSpawnInfoGroupsTable(docOf("spawn_info_groups.xtbl")))
                if (g.name) groups_[lower(*g.name)] = g;
            charDoc_ = std::make_unique<sr3xtbl::Document>(docOf("character.xtbl"));
            defDoc_ = std::make_unique<sr3xtbl::Document>(docOf("character_definitions.xtbl"));
            indexCharacters();
        } catch (const std::exception& ex) {
            std::printf("ambient-models: tables unavailable (%s) - ambient actors keep the placeholder models\n", ex.what());
            return false;
        }
        vehMap_ = mapFileLargeC(vehiclesArchivePath);
        if (!vehMap_.empty()) {
            try {
                vehContainer_ = std::make_unique<vpp::Container>(vpp::ByteView(vehMap_.data(), vehMap_.size()));
                for (const auto& e : vehContainer_->entries()) {
                    const std::string n = lower(e.name);
                    static const std::string kExt = ".str2_pc";
                    if (n.size() > kExt.size() && n.compare(n.size() - kExt.size(), kExt.size(), kExt) == 0)
                        vehicleNames_.insert(n.substr(0, n.size() - kExt.size()));
                }
            } catch (const std::exception& ex) {
                std::printf("ambient-models: vehicles archive unreadable (%s)\n", ex.what());
                vehContainer_.reset();
            }
        }
        if (charBytes_ && !charBytes_->empty()) {
            try {
                charContainer_ = std::make_unique<vpp::Container>(vpp::ByteView(charBytes_->data(), charBytes_->size()));
                static const std::string kExt = ".str2_pc";
                for (size_t i = 0; i < charContainer_->entries().size(); ++i) {
                    const std::string n = lower(charContainer_->entries()[i].name);
                    if (n.size() > kExt.size() && n.compare(n.size() - kExt.size(), kExt.size(), kExt) == 0)
                        charStr2_[n.substr(0, n.size() - kExt.size())] = i;
                }
            } catch (const std::exception& ex) {
                std::printf("ambient-models: characters archive unreadable (%s)\n", ex.what());
                charContainer_.reset();
            }
        }
        ready_ = vehContainer_ || charContainer_;
        std::printf("ambient-models: %zu categories, %zu groups, %zu character rows, %zu vehicle models in archive, "
                    "%zu character containers\n",
                    categories_.size(), groups_.size(), charRowToDef_.size(), vehicleNames_.size(), charStr2_.size());
        return ready_;
    }

    // ---- vehicles -------------------------------------------------------------------------------------
    // Chooses (and loads on first use) a model for a new ambient vehicle at (x,y,z). `live` = (model key,
    // x, z) of every already-assigned live ambient vehicle. Returns nullptr when nothing could be loaded
    // (caller then keeps the placeholder genki rig).
    struct LiveVeh { const AmbientVehicleModel* model; double x, z; };
    AmbientVehicleModel* chooseVehicle(uint64_t id, double x, double y, double z, bool night,
                                       const std::vector<LiveVeh>& live) {
        if (!vehContainer_) return nullptr;
        const std::string cat = categoryFor(x, y, z);
        const StreamedVehicles& set = streamedVehicles(cat, night);
        if (set.models.empty()) return nullptr;
        // Candidates: live count below the supplying group's vehicle cap.
        std::vector<size_t> cand;
        for (size_t i = 0; i < set.models.size(); ++i) {
            size_t liveCount = 0;
            for (const auto& l : live) if (l.model && l.model->key == set.models[i]) ++liveCount;
            if (set.caps[i] == 0 || liveCount < set.caps[i]) cand.push_back(i);
        }
        if (cand.empty()) for (size_t i = 0; i < set.models.size(); ++i) cand.push_back(i);
        // Fewest nearby duplicates, then fewest live copies, then uniform (20.6.1(e) step 2). A model with
        // >= 4 nearby copies is excluded (when others remain).
        struct Score { size_t idx; size_t nearby; size_t total; };
        std::vector<Score> scores;
        for (size_t i : cand) {
            Score s{i, 0, 0};
            for (const auto& l : live) {
                if (!l.model || l.model->key != set.models[i]) continue;
                ++s.total;
                const double dx = l.x - x, dz = l.z - z;
                if (dx * dx + dz * dz <= kNearbyRadiusM * kNearbyRadiusM) ++s.nearby;
            }
            scores.push_back(s);
        }
        std::vector<Score> kept;
        for (const auto& s : scores) if (s.nearby < 4) kept.push_back(s);
        if (kept.empty()) kept = scores;
        size_t bestNearby = SIZE_MAX;
        for (const auto& s : kept) bestNearby = (std::min)(bestNearby, s.nearby);
        std::vector<Score> t1;
        for (const auto& s : kept) if (s.nearby == bestNearby) t1.push_back(s);
        size_t bestTotal = SIZE_MAX;
        for (const auto& s : t1) bestTotal = (std::min)(bestTotal, s.total);
        std::vector<Score> t2;
        for (const auto& s : t1) if (s.total == bestTotal) t2.push_back(s);
        uint64_t r = mix(id * 0x9E3779B97F4A7C15ULL + 0x51ED);
        const Score& pick = t2[static_cast<size_t>(r % t2.size())];
        AmbientVehicleModel* m = loadedVehicle(set.models[pick.idx]);
        if (m) {
            ++m->uses;
            ++stats_.vehicleAssignments;
            ++stats_.vehicleUse[m->key];
        }
        return m;
    }

    // ---- peds -----------------------------------------------------------------------------------------
    AmbientPedModel* choosePed(uint64_t id, double x, double y, double z, bool night) {
        if (!charContainer_) return nullptr;
        const std::string cat = categoryFor(x, y, z);
        const StreamedPeds& set = streamedPeds(cat, night);
        if (set.rows.empty()) return nullptr;
        // Per spawn: uniform among the streamed characters (CHOSEN; the 0x009160a0 tie-break is PARTIAL).
        const uint64_t r = mix(id * 0xD1B54A32D192ED03ULL + 0xA11CE);
        AmbientPedModel* m = loadedPed(set.rows[static_cast<size_t>(r % set.rows.size())]);
        if (m) {
            ++stats_.pedAssignments;
            ++stats_.pedUse[m->stem];
        }
        return m;
    }

    void noteMemory() {
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        using Fn = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
        static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo"));
        if (fn && fn(GetCurrentProcess(), &pmc, sizeof(pmc)))
            stats_.peakWorkingSetBytes = (std::max)(stats_.peakWorkingSetBytes, static_cast<size_t>(pmc.PeakWorkingSetSize));
    }

    void printTrace() {
        noteMemory();
        std::printf("ambient-models-trace: ready=%d distinct_vehicle_models=%zu (cap %zu) distinct_ped_characters=%zu (cap %zu) "
                    "load_ms_total=%.1f cache_hits=%zu cache_misses=%zu vehicle_assignments=%zu ped_assignments=%zu "
                    "vehicle_load_failures=%zu ped_load_failures=%zu streamed_sets_built=%zu peak_working_set_bytes=%zu\n",
                    ready_ ? 1 : 0, vehicles_.size(), kMaxVehicleModels, peds_.size(), kMaxPedModels, stats_.loadMs,
                    stats_.cacheHits, stats_.cacheMisses, stats_.vehicleAssignments, stats_.pedAssignments,
                    stats_.vehicleLoadFailures, stats_.pedLoadFailures, stats_.setsBuilt, stats_.peakWorkingSetBytes);
        std::string v, p;
        for (const auto& kv : stats_.vehicleUse) v += " " + kv.first + "=" + std::to_string(kv.second);
        for (const auto& kv : stats_.pedUse) p += " " + kv.first + "=" + std::to_string(kv.second);
        std::printf("ambient-models-trace: vehicle_use:%s\n", v.empty() ? " (none)" : v.c_str());
        std::printf("ambient-models-trace: ped_use:%s\n", p.empty() ? " (none)" : p.c_str());
    }

    // Drawing (same lighting parameters the placeholder draws used).
    static void DrawPed(AmbientPedModel& m, ID3D11DeviceContext* ctx, const float vp[16], const float world[16],
                        const float* sun, const float* amb, const float* sunCol) {
        if (!m.ready) return;
        if (m.bodyTextured)
            m.body->drawTextured(ctx, vp, m.bodySrv, world, sun, amb, sunCol);
        else
            m.body->draw(ctx, vp, sr3render::MeshDrawMode::Checker, nullptr, world, nullptr, sun, amb, sunCol);
        if (m.head) {
            if (m.headTextured)
                m.head->drawTextured(ctx, vp, m.headSrv, world, sun, amb, sunCol);
            else
                m.head->draw(ctx, vp, sr3render::MeshDrawMode::Checker, nullptr, world, nullptr, sun, amb, sunCol);
        }
    }

private:
    struct StreamedVehicles { std::vector<std::string> models; std::vector<size_t> caps; };
    struct StreamedPeds { std::vector<std::string> rows; };
    struct DefInfo { std::string name, stem, headName, headLod; };

    static std::string lower(std::string s) {
        for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }
    static uint64_t mix(uint64_t z) {
        z += 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    static uint64_t hashStr(const std::string& s) {
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        return h;
    }
    static std::string stemOf(const std::string* fname) {
        if (!fname) return {};
        std::string s = lower(*fname);
        const size_t dot = s.find_last_of('.');
        return dot == std::string::npos ? s : s.substr(0, dot);
    }

    std::string categoryFor(double x, double y, double z) const {
        std::string c = hoodCategory_ ? hoodCategory_(x, y, z) : std::string("Default Spawn");
        if (!categories_.count(lower(c))) c = "Default Spawn";
        return c;
    }

    void indexCharacters() {
        // character_definitions.xtbl: Name -> {Model stem, head name, lod head}
        const sr3xtbl::Node* dt = defDoc_->table();
        if (dt)
            for (const sr3xtbl::Node* row : dt->children()) {
                if (!sr3xtbl::NameEquals(row->name(), "Character")) continue;
                const std::string* nm = sr3xtbl::ChildText(row, "Name");
                if (!nm) continue;
                DefInfo d;
                d.name = lower(*nm);
                if (const sr3xtbl::Node* model = sr3xtbl::FindChild(row, "Model"))
                    d.stem = stemOf(sr3xtbl::ChildText(model, "Filename"));
                if (const sr3xtbl::Node* heads = sr3xtbl::FindChild(row, "Heads"))
                    if (const sr3xtbl::Node* h = sr3xtbl::FindChild(heads, "Head")) {
                        if (const std::string* hn = sr3xtbl::ChildText(h, "Name")) d.headName = lower(*hn);
                        if (const std::string* hl = sr3xtbl::ChildText(h, "Lod_Head")) d.headLod = lower(*hl);
                    }
                if (!d.stem.empty()) defs_[d.name] = d;
            }
        // character.xtbl: row Name -> definition name (the row's own `Character` child)
        const sr3xtbl::Node* ct = charDoc_->table();
        if (ct)
            for (const sr3xtbl::Node* row : ct->children()) {
                if (!sr3xtbl::NameEquals(row->name(), "Character")) continue;
                const std::string* nm = sr3xtbl::ChildText(row, "Name");
                const std::string* def = sr3xtbl::ChildText(row, "Character");
                if (nm && def) charRowToDef_[lower(*nm)] = lower(*def);
            }
    }

    // Weighted draw of an index from non-negative weights; -1 if all zero.
    static int roulette(const std::vector<double>& w, uint64_t& rng) {
        double sum = 0;
        for (double x : w) sum += (std::max)(0.0, x);
        if (sum <= 0) return -1;
        rng = mix(rng);
        double r = (static_cast<double>(rng >> 11) / 9007199254740992.0) * sum;
        for (size_t i = 0; i < w.size(); ++i) {
            r -= (std::max)(0.0, w[i]);
            if (r < 0) return static_cast<int>(i);
        }
        return static_cast<int>(w.size()) - 1;
    }

    static int kindOf(const std::optional<std::string>& groupCategory) {
        if (!groupCategory) return 0;
        const std::string g = lower(*groupCategory);
        if (g == "special ped") return 1;
        if (g == "special vehicle") return 2;
        return 0;  // "General Ped" and anything else
    }

    const sr3tables_progression::SpawnGroup* groupOf(const sr3tables_progression::SpawnCategoryGroup& cg) const {
        if (!cg.name) return nullptr;
        auto it = groups_.find(lower(*cg.name));
        return it == groups_.end() ? nullptr : &it->second;
    }

    // Spline_Type classes that are not ordinary roads for an on-street car (10.6): boat, baggage, indoor.
    // CHOSEN: "Offroad" and "Highway Only"/"Taxi" cars are kept (they still drive on streets in this build).
    static bool roadEligible(const sr3tables_progression::SpawnGroup& g) {
        if (!g.splineType) return true;  // -1 (All Roads / Surface Roads)
        const std::string s = lower(*g.splineType);
        return !(s == "boat" || s == "baggage" || s == "indoor");
    }

    const StreamedVehicles& streamedVehicles(const std::string& cat, bool night) {
        const std::string key = lower(cat) + (night ? "|n" : "|d");
        auto it = vehSets_.find(key);
        if (it != vehSets_.end()) return it->second;
        ++stats_.setsBuilt;
        StreamedVehicles set;
        const auto& c = categories_.at(lower(cat));
        uint64_t rng = hashStr(key);
        const double slotW[3] = {night ? c.generalPedSlotNight.value : c.generalPedSlotDay.value,
                                 night ? c.specialPedSlotNight.value : c.specialPedSlotDay.value,
                                 night ? c.specialVehicleSlotNight.value : c.specialVehicleSlotDay.value};
        // groups by kind that actually have loadable vehicles
        std::vector<const sr3tables_progression::SpawnCategoryGroup*> byKind[3];
        for (const auto& cg : c.groups) {
            const auto* g = groupOf(cg);
            if (!g || g->vehicles.empty() || !roadEligible(*g)) continue;
            bool any = false;
            for (const auto& v : g->vehicles)
                if (v.name && vehicleNames_.count(vehicleKey(*v.name))) { any = true; break; }
            if (any) byKind[kindOf(cg.groupCategory)].push_back(&cg);
        }
        for (int roll = 0; roll < kVehicleStreamRolls; ++roll) {
            std::vector<double> kw(3, 0.0);
            for (int k = 0; k < 3; ++k) kw[k] = byKind[k].empty() ? 0.0 : slotW[k];
            int kind = roulette(kw, rng);
            if (kind < 0) {
                // slot weights all zero for kinds that have vehicles: fall back to any kind that has some (CHOSEN)
                for (int k = 0; k < 3; ++k) if (!byKind[k].empty()) { kind = k; break; }
                if (kind < 0) break;
            }
            std::vector<double> gw;
            for (const auto* cg : byKind[kind])
                gw.push_back(night ? cg->nightChance.value_or(0.f) : cg->dayChance.value_or(0.f));
            int gi = roulette(gw, rng);
            if (gi < 0) gi = 0;
            const auto* cg = byKind[kind][static_cast<size_t>(gi)];
            const auto* g = groupOf(*cg);
            std::vector<std::string> avail;  // uniform over rows, duplicates weigh double
            for (const auto& v : g->vehicles)
                if (v.name && vehicleNames_.count(vehicleKey(*v.name))) avail.push_back(vehicleKey(*v.name));
            rng = mix(rng);
            const std::string model = avail[static_cast<size_t>(rng % avail.size())];
            if (std::find(set.models.begin(), set.models.end(), model) != set.models.end()) continue;
            if (!vehicles_.count(model) && vehicles_.size() >= kMaxVehicleModels) continue;  // cap
            if (!loadedVehicle(model)) continue;
            set.models.push_back(model);
            set.caps.push_back(night ? cg->vehicleNightCap.value_or(0u) : cg->vehicleDayCap.value_or(0u));
        }
        if (set.models.empty())  // all rolls capped/failed: reuse whatever is already loaded
            for (const auto& kv : vehicles_) { set.models.push_back(kv.first); set.caps.push_back(0); }
        return vehSets_.emplace(key, std::move(set)).first->second;
    }

    static std::string vehicleKey(const std::string& name) { return lower(name) + "_0"; }  // CHOSEN variant 0

    AmbientVehicleModel* loadedVehicle(const std::string& key) {
        auto it = vehicles_.find(key);
        if (it != vehicles_.end()) {
            ++stats_.cacheHits;
            return it->second.get();
        }
        if (failedVehicles_.count(key) || vehicles_.size() >= kMaxVehicleModels) return nullptr;
        ++stats_.cacheMisses;
        const auto t0 = std::chrono::steady_clock::now();
        auto m = std::make_unique<AmbientVehicleModel>();
        m->key = key;
        std::string diag;
        const vpp::ByteView view(vehMap_.data(), vehMap_.size());
        const bool ok = m->rig.Load(device_, std::string(), key + ".ccar_pc", wheelsPath_, diag, &view);
        stats_.loadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!ok) {
            std::printf("ambient-models: vehicle '%s' failed to load: %s\n", key.c_str(), diag.c_str());
            failedVehicles_.insert(key);
            ++stats_.vehicleLoadFailures;
            return nullptr;
        }
        m->rig.sim->state_.posX = 0.0;
        m->rig.sim->state_.posZ = 0.0;
        m->rig.sim->state_.heading = 0.0;
        m->rig.PoseAndUpload(device_, 0.0);  // static pose, wheels at rest (ambient cars do not roll wheels: CHOSEN)
        ++stats_.vehicleModelsLoaded;
        noteMemory();
        return vehicles_.emplace(key, std::move(m)).first->second.get();
    }

    const StreamedPeds& streamedPeds(const std::string& cat, bool night) {
        const std::string key = lower(cat) + (night ? "|n" : "|d");
        auto it = pedSets_.find(key);
        if (it != pedSets_.end()) return it->second;
        ++stats_.setsBuilt;
        StreamedPeds set;
        const auto& c = categories_.at(lower(cat));
        uint64_t rng = hashStr(key) ^ 0x5EDULL;
        const double slotW[2] = {night ? c.generalPedSlotNight.value : c.generalPedSlotDay.value,
                                 night ? c.specialPedSlotNight.value : c.specialPedSlotDay.value};
        // groups of the two ped kinds that have at least one resolvable character
        std::vector<const sr3tables_progression::SpawnCategoryGroup*> byKind[2];
        for (const auto& cg : c.groups) {
            const int k = kindOf(cg.groupCategory);
            if (k > 1) continue;  // Special Vehicle slots stream occupants/vehicles, not pedestrians
            const auto* g = groupOf(cg);
            if (!g) continue;
            bool any = false;
            for (const auto& ch : g->characters)
                if (resolvable(ch)) { any = true; break; }
            if (any) byKind[k].push_back(&cg);
        }
        for (int roll = 0; roll < kPedStreamRolls; ++roll) {
            std::vector<double> kw(2, 0.0);
            for (int k = 0; k < 2; ++k) kw[k] = byKind[k].empty() ? 0.0 : slotW[k];
            int kind = roulette(kw, rng);
            if (kind < 0) {
                for (int k = 0; k < 2; ++k) if (!byKind[k].empty()) { kind = k; break; }
                if (kind < 0) break;
            }
            std::vector<double> gw;
            for (const auto* cg : byKind[kind])
                gw.push_back(night ? cg->nightChance.value_or(0.f) : cg->dayChance.value_or(0.f));
            int gi = roulette(gw, rng);
            if (gi < 0) gi = 0;
            const auto* g = groupOf(*byKind[kind][static_cast<size_t>(gi)]);
            std::vector<std::string> avail;
            for (const auto& ch : g->characters) if (resolvable(ch)) avail.push_back(lower(ch));
            rng = mix(rng);
            const std::string row = avail[static_cast<size_t>(rng % avail.size())];
            if (std::find(set.rows.begin(), set.rows.end(), row) != set.rows.end()) continue;
            if (!peds_.count(row) && peds_.size() >= kMaxPedModels) continue;  // cap
            if (!loadedPed(row)) continue;
            set.rows.push_back(row);
        }
        if (set.rows.empty())
            for (const auto& kv : peds_) set.rows.push_back(kv.first);
        return pedSets_.emplace(key, std::move(set)).first->second;
    }

    bool resolvable(const std::string& characterRow) const {
        auto r = charRowToDef_.find(lower(characterRow));
        if (r == charRowToDef_.end()) return false;
        auto d = defs_.find(r->second);
        if (d == defs_.end()) return false;
        return charStr2_.count(d->second.name) != 0;
    }

    AmbientPedModel* loadedPed(const std::string& row) {
        auto it = peds_.find(row);
        if (it != peds_.end()) {
            ++stats_.cacheHits;
            return it->second.get();
        }
        if (failedPeds_.count(row) || peds_.size() >= kMaxPedModels) return nullptr;
        ++stats_.cacheMisses;
        const auto t0 = std::chrono::steady_clock::now();
        auto m = std::make_unique<AmbientPedModel>();
        m->characterRow = row;
        std::string diag;
        const bool ok = buildPed(*m, diag);
        stats_.loadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!ok) {
            std::printf("ambient-models: ped '%s' failed to load: %s\n", row.c_str(), diag.c_str());
            failedPeds_.insert(row);
            ++stats_.pedLoadFailures;
            return nullptr;
        }
        ++stats_.pedModelsLoaded;
        noteMemory();
        return peds_.emplace(row, std::move(m)).first->second.get();
    }

    // Same pipeline as setupBrad()/setupRenderAssets() (bind-pose skin, upload, per-material diffuse), per model,
    // against that character's own nested `<definition>.str2_pc` container (a small walk per texture).
    bool buildPed(AmbientPedModel& m, std::string& diag) {
        const DefInfo& def = defs_.at(charRowToDef_.at(m.characterRow));
        m.definition = def.name;
        m.stem = def.stem;
        try {
            vpp::Container cont = charContainer_->openNested(charStr2_.at(def.name));
            std::vector<uint8_t> cb, gb, rb, unused, hcb, hgb;
            if (!findEntry(cont, def.stem + ".ccmesh_pc", cb, gb, def.stem + ".gcmesh_pc") || gb.empty()) {
                diag = def.stem + ".ccmesh_pc/.gcmesh_pc not found";
                return false;
            }
            if (!findEntry(cont, def.stem + ".rig_pc", rb, unused, "")) { diag = def.stem + ".rig_pc not found"; return false; }
            const vpp::ByteView cbv(cb.data(), cb.size());
            sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(cbv);
            sr3geometry::GeometryBlock geometry = sr3geometry::GeometryBlock::parse(cbv, material);
            if (!geometry.hasMeshSubBlock()) { diag = "no Mesh sub-block"; return false; }
            sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(cbv, geometry.meshSubBlockOffset(), vpp::ByteView(gb.data(), gb.size()));
            if (mesh.channels().empty() || !sr3mesh::layoutInfoFor(mesh.channels()[0].layoutCode).hasSkinning) {
                diag = "channel 0 missing or not skinned";
                return false;
            }
            sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
            const auto& bones = rig.bones();
            const SkinningModeResult mode = resolveSkinningMode(mesh, false);
            if (isSkinningRefusal(mode.mode)) { diag = mode.refusalMessage; return false; }
            const bool palette = mode.mode == SkinningMode::kPaletteSingleSet || mode.mode == SkinningMode::kPaletteMultiSet;
            std::vector<uint32_t> parentIdx(bones.size());
            std::vector<std::array<float, 3>> restMesh(bones.size());
            for (size_t i = 0; i < bones.size(); ++i) {
                parentIdx[i] = bones[i].parentIndex;
                restMesh[i] = palette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                      : sr3rig::rigToMeshSpace(bones[i].restPosition);
            }
            const auto bindSkin = sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
            std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(0);
            if (verts.empty()) { diag = "no vertices"; return false; }
            sr3render::VertexDuplication dup;
            bool haveDup = false;
            if (mode.mode == SkinningMode::kPaletteMultiSet) {
                sr3rig::BlendIndexRemapStats remap;
                sr3rig::VertexDuplicationResult d = applyMultiSetSkinning(mesh, verts, bones.size(), 0, remap);
                if (!d.usable) { diag = d.problem; return false; }
                if (d.duplicatesCreated != 0) {
                    dup.sourceIndex = d.duplicateSourceIndex;
                    dup.redirectPerRange = d.redirectPerRange;
                    haveDup = true;
                }
            } else if (palette) {
                sr3rig::BlendIndexRemapStats remap;
                verts = sr3rig::remapBlendIndicesThroughPalette(verts, mesh.bonePalette(), bones.size(), &remap);
            }
            const auto bindPositions = sr3rig::skinVertices(verts, bindSkin, nullptr);
            m.body = std::make_unique<sr3render::MeshRenderer>();
            std::string err;
            if (!m.body->initialise(device_, err) ||
                !m.body->upload(device_, mesh, 0, 0, err, 0, &bindPositions, haveDup ? &dup : nullptr)) {
                diag = "body upload: " + err;
                return false;
            }
            // body textures (the model's own container: its cpeg/gpeg live next to the mesh)
            try {
                sr3geometry::MaterialBindings b = sr3geometry::MaterialBindings::parse(
                    cbv, geometry.offset(), geometry.meshSubBlockOffset() + mesh.cLength());
                if (b.located()) {
                    m.bodyTex.resize(b.materialCount());
                    m.bodySrv.assign(b.materialCount(), nullptr);
                    for (uint32_t i = 0; i < b.materialCount(); ++i) {
                        const std::string* d = b.diffuseFor(i);
                        std::string foundIn;
                        if (d && findAndUploadTexture(cont, *d, device_, m.bodyTex[i], foundIn)) m.bodySrv[i] = m.bodyTex[i].srv;
                    }
                    m.bodyTextured = true;
                }
            } catch (const std::exception&) {}
            // head (lod) - optional
            if (!def.headLod.empty() && findEntry(cont, def.headLod + ".ccmesh_pc", hcb, hgb, def.headLod + ".gcmesh_pc") &&
                !hgb.empty())
                buildHead(m, def, cont, hcb, hgb, bones, parentIdx);
            m.ready = true;
            return true;
        } catch (const std::exception& ex) {
            diag = ex.what();
            return false;
        }
    }

    void buildHead(AmbientPedModel& m, const DefInfo& def, const vpp::Container& cont, const std::vector<uint8_t>& hcb,
                   const std::vector<uint8_t>& hgb, const std::vector<sr3rig::Bone>& bones,
                   const std::vector<uint32_t>& parentIdx) {
        try {
            const vpp::ByteView hv(hcb.data(), hcb.size());
            sr3geometry::MaterialBlock hm = sr3geometry::MaterialBlock::parse(hv);
            sr3geometry::GeometryBlock hg = sr3geometry::GeometryBlock::parse(hv, hm);
            if (!hg.hasMeshSubBlock()) return;
            sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(hv, hg.meshSubBlockOffset(), vpp::ByteView(hgb.data(), hgb.size()));
            if (mesh.channels().empty() || !sr3mesh::layoutInfoFor(mesh.channels()[0].layoutCode).hasSkinning) return;
            const SkinningModeResult mode = resolveSkinningMode(mesh, false);
            if (isSkinningRefusal(mode.mode)) return;
            const bool palette = mode.mode == SkinningMode::kPaletteSingleSet || mode.mode == SkinningMode::kPaletteMultiSet;
            std::vector<std::array<float, 3>> restMesh(bones.size());
            for (size_t i = 0; i < bones.size(); ++i)
                restMesh[i] = palette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                      : sr3rig::rigToMeshSpace(bones[i].restPosition);
            const auto bindSkin = sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
            std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(0);
            if (verts.empty()) return;
            sr3render::VertexDuplication dup;
            bool haveDup = false;
            if (mode.mode == SkinningMode::kPaletteMultiSet) {
                sr3rig::BlendIndexRemapStats remap;
                sr3rig::VertexDuplicationResult d = applyMultiSetSkinning(mesh, verts, bones.size(), 0, remap);
                if (!d.usable) return;
                if (d.duplicatesCreated != 0) {
                    dup.sourceIndex = d.duplicateSourceIndex;
                    dup.redirectPerRange = d.redirectPerRange;
                    haveDup = true;
                }
            } else if (palette) {
                sr3rig::BlendIndexRemapStats remap;
                verts = sr3rig::remapBlendIndicesThroughPalette(verts, mesh.bonePalette(), bones.size(), &remap);
            }
            const auto bindPositions = sr3rig::skinVertices(verts, bindSkin, nullptr);
            auto head = std::make_unique<sr3render::MeshRenderer>();
            std::string err;
            if (!head->initialise(device_, err) ||
                !head->upload(device_, mesh, 0, 0, err, 0, &bindPositions, haveDup ? &dup : nullptr))
                return;
            m.head = std::move(head);
            sr3geometry::MaterialBindings b = sr3geometry::MaterialBindings::parse(
                hv, hg.offset(), hg.meshSubBlockOffset() + mesh.cLength());
            if (!b.located()) return;
            // matlib attach + role-suffix texture pick: the SAME CHOSEN heuristic the player's head uses
            // (setupRenderAssets(); which texture replaces which slot is not spec-confirmed, 12.1c/12.1f).
            if (!def.headName.empty()) {
                auto loadByName = [&](const std::string& composed, std::vector<uint8_t>& out) {
                    std::vector<uint8_t> u;
                    return findEntry(cont, composed, out, u, "");
                };
                try {
                    sr3geometry::CharacterMatlibAttachment att =
                        sr3geometry::ResolveCharacterMatlibAttachment(def.stem, def.headName, loadByName);
                    if (att.attached()) {
                        const std::string hint = "head";
                        const std::string* rd = sr3geometry::PickMatlibTextureByRoleSuffix(att.resource.textureNames, hint, {"_d", "_dp"});
                        const std::string* rn = sr3geometry::PickMatlibTextureByRoleSuffix(att.resource.textureNames, hint, {"_n"});
                        for (sr3geometry::MaterialBinding& mb : b.materialsMutable())
                            for (sr3geometry::TextureRef& tr : mb.textures) {
                                if (rd && (tr.paramHash == sr3geometry::kParamHashDiffuseA || tr.paramHash == sr3geometry::kParamHashDiffuseB))
                                    tr.name = *rd;
                                else if (rn && tr.paramHash == sr3geometry::kParamHashNormalMap)
                                    tr.name = *rn;
                            }
                    }
                } catch (const std::exception&) {}
            }
            m.headTex.resize(b.materialCount());
            m.headSrv.assign(b.materialCount(), nullptr);
            for (uint32_t i = 0; i < b.materialCount(); ++i) {
                const std::string* d = b.diffuseFor(i);
                std::string foundIn;
                if (d && findAndUploadTexture(cont, *d, device_, m.headTex[i], foundIn)) m.headSrv[i] = m.headTex[i].srv;
            }
            m.headTextured = true;
        } catch (const std::exception&) {
        }
    }

    bool ready_ = false;
    ID3D11Device* device_ = nullptr;
    std::string wheelsPath_;
    HoodCategoryFn hoodCategory_;
    const MappedBytes* charBytes_ = nullptr;
    MappedBytes vehMap_;
    std::unique_ptr<vpp::Container> vehContainer_, charContainer_;
    std::unordered_set<std::string> vehicleNames_;
    std::unordered_map<std::string, size_t> charStr2_;
    std::unique_ptr<sr3xtbl::Document> charDoc_, defDoc_;
    std::unordered_map<std::string, sr3tables_progression::SpawnCategory> categories_;
    std::unordered_map<std::string, sr3tables_progression::SpawnGroup> groups_;
    std::unordered_map<std::string, DefInfo> defs_;
    std::unordered_map<std::string, std::string> charRowToDef_;
    std::unordered_map<std::string, StreamedVehicles> vehSets_;
    std::unordered_map<std::string, StreamedPeds> pedSets_;
    std::map<std::string, std::unique_ptr<AmbientVehicleModel>> vehicles_;
    std::map<std::string, std::unique_ptr<AmbientPedModel>> peds_;
    std::set<std::string> failedVehicles_, failedPeds_;
    AmbientModelStats stats_;
};

}  // namespace sr3viewer

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
//   * ped pose: the specced walk clip per ped (ambient-ped-anim task; poseWalk() below lists the spec used and the CHOSEN
//     stand-ins: male clip set for all, bone-name retarget, frame-0 hold when stationary, per-frame cost bounds).
//     Peds already face their movement direction (ActorPlacement::heading);
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
#include "sr3airuntime/ped_navigator.h"
#include "sr3animpose/clip_pose_bridge.h"
#include "sr3rig/animated_pose.h"
#include "viewer_walk_support.h"
#include <cmath>
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
    // Walk animation (ambient-ped-anim task): skinning inputs kept so the shared per-model buffers can be re-posed
    // per ped. Vertices are the post-remap (and multi-set-duplicated) decode, i.e. exactly what bind-skinning used.
    std::vector<sr3mesh::Vertex> bodyVerts, headVerts;
    bool bodyPalette = false, headPalette = false;
    std::vector<uint32_t> parentIdx;
    std::vector<std::array<float, 3>> restRig;  // rig-space rest positions (same input the player path uses)
    std::vector<std::string> boneNames;         // lower-case bone names, rig order (name retarget of clips)
    bool poseable = false;                      // dynamic buffers created and skinning inputs stored
    int64_t lastPoseKey = INT64_MIN;            // (clip record, 30 Hz tick) last uploaded to the shared buffers
};

struct AmbientVehicleModel {
    std::string key;  // "<name>_<variant>"
    PlaytestVehicleRig rig;
    // AI_Max_Braking_Decel (+0x770) of this model's own `<name>_veh.xtbl` vehicle-info row; nullopt when the row could
    // not be found / the lookup is not installed / the value is absent (caller falls back to a CHOSEN constant).
    std::optional<double> aiMaxBrakingDecel;
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
    // ped walk animation
    size_t poseUpdates = 0, poseCacheSkips = 0, pedsNoClip = 0, pedDrawsAnimated = 0, pedDrawsStale = 0, pedDrawsStill = 0;
    double poseMs = 0.0;
    std::set<std::string> clipMissing, clipBoneMismatch, clipRetargeted;
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
    void setNoWalkAnim(bool v) { noWalkAnim_ = v; }  // --no-ped-anim (before init)
    // Installs the vehicle-row lookup: model name (no "_<variant>" suffix) -> that row's AI_Max_Braking_Decel, or
    // nullopt. Optional; without it every model's aiMaxBrakingDecel stays nullopt.
    using BrakingDecelLookupFn = std::function<std::optional<double>(const std::string&)>;
    void setBrakingDecelLookup(BrakingDecelLookupFn f) { brakingDecelLookup_ = std::move(f); }
    const AmbientModelStats& stats() const { return stats_; }

    bool init(ID3D11Device* device, const std::string& vehiclesArchivePath, const std::string& wheelsArchivePath,
              const MappedBytes* characterArchive, const std::function<sr3xtbl::Document(const std::string&)>& docOf,
              HoodCategoryFn hoodCategory, const std::string& animArchivePath = std::string()) {
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
        if (!animArchivePath.empty() && !noWalkAnim_) {
            animMap_ = mapFileLargeC(animArchivePath);
            if (!animMap_.empty()) {
                try {
                    animContainer_ = std::make_unique<vpp::Container>(vpp::ByteView(animMap_.data(), animMap_.size()));
                } catch (const std::exception& ex) {
                    std::printf("ambient-models: animation archive unreadable (%s) - peds stay in bind pose\n", ex.what());
                    animContainer_.reset();
                }
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

    // ---- police models (feature/police-response) --------------------------------------------------------
    // The police car and cop character, looked up by name in the same archives (exempt from the streamed-model caps:
    // a dispatched unit must not depend on the ambient roll). Spec names (spec-ai-runtime.md 20.6.10 test vector):
    // `car_4dr_police02` (vehicle), `npc_cop` (character). The character.xtbl ROW name for the cop is not given by the
    // spec (generic_characters `Police Officer` -> `npc_cop` is the spawn name), so rows containing "cop"/"police" are
    // searched (CHOSEN lookup, logged by the caller). Returns nullptr when nothing loads (caller draws a placeholder).
    AmbientVehicleModel* policeCarModel(std::string* chosenKey = nullptr) {
        if (!vehContainer_) return nullptr;
        std::vector<std::string> cands = {"car_4dr_police02", "car_4dr_police01"};
        for (const auto& n : vehicleNames_)
            if (n.find("police") != std::string::npos && n.find("car") != std::string::npos) cands.push_back(n);
        for (const auto& c : cands) {
            const bool plain = vehicleNames_.count(c) != 0;
            if (!plain && !vehicleNames_.count(c + "_0")) continue;
            if (AmbientVehicleModel* m = loadedVehicle(plain ? vehicleKey(c) : c + "_0", true)) {
                if (chosenKey) *chosenKey = m->key;
                return m;
            }
        }
        return nullptr;
    }
    AmbientPedModel* policePedModel(std::string* chosenRow = nullptr) {
        if (!charContainer_) return nullptr;
        std::vector<std::string> cands = {"npc_cop", "police officer", "police_officer"};
        for (const auto& kv : charRowToDef_)
            if (kv.first.find("cop") != std::string::npos || kv.first.find("police") != std::string::npos) cands.push_back(kv.first);
        for (const auto& c : cands) {
            if (!resolvable(c)) continue;
            if (AmbientPedModel* m = loadedPed(c, true)) {
                if (chosenRow) *chosenRow = c;
                return m;
            }
        }
        return nullptr;
    }

    // ---- homie models (feature/homies) ------------------------------------------------------------------
    // A homies.xtbl `Humans/Character` row and `Vehicles/Vehicle` name looked up by name (exempt from the streamed caps:
    // a called homie must not depend on the ambient roll). nullptr when it does not resolve (caller draws a placeholder).
    AmbientPedModel* homiePedModel(const std::string& characterRow) {
        if (!charContainer_ || !resolvable(characterRow)) return nullptr;
        return loadedPed(lower(characterRow), true);  // registry keys are lower-case
    }
    AmbientVehicleModel* homieVehicleModel(const std::string& vehicleName, std::string* chosenKey = nullptr) {
        if (!vehContainer_) return nullptr;
        const std::string n = lower(vehicleName);
        const bool plain = vehicleNames_.count(n) != 0;
        if (!plain && !vehicleNames_.count(n + "_0")) return nullptr;
        if (AmbientVehicleModel* m = loadedVehicle(plain ? n : n + "_0", true)) {
            if (chosenKey) *chosenKey = m->key;
            return m;
        }
        return nullptr;
    }

    // AW-15: every vehicle mesh name in the archive (lower case, no extension), for --spawn-vehicle=list.
    std::vector<std::string> vehicleNameList() const { return std::vector<std::string>(vehicleNames_.begin(), vehicleNames_.end()); }

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
        std::printf("ambient-ped-anim-trace: clips_ready=%d draws_animated=%zu draws_still_frame0=%zu draws_stale=%zu "
                    "pose_updates=%zu pose_cache_skips=%zu pose_ms_total=%.1f pose_ms_per_update=%.3f peds_without_clip=%zu "
                    "(max %d updates/frame, radius %.0f m)\n",
                    clipsReady() ? 1 : 0, stats_.pedDrawsAnimated, stats_.pedDrawsStill, stats_.pedDrawsStale, stats_.poseUpdates,
                    stats_.poseCacheSkips, stats_.poseMs, stats_.poseUpdates ? stats_.poseMs / static_cast<double>(stats_.poseUpdates) : 0.0,
                    stats_.pedsNoClip, kMaxPoseUpdatesPerFrame, kAnimRadiusM);
        for (const auto& s : stats_.clipMissing) std::printf("ambient-ped-anim-trace: clip missing in archive: %s\n", s.c_str());
        for (const auto& s : stats_.clipRetargeted) std::printf("ambient-ped-anim-trace: retarget by bone name: %s\n", s.c_str());
        for (const auto& s : stats_.clipBoneMismatch) std::printf("ambient-ped-anim-trace: bone-count mismatch, clip not used: %s\n", s.c_str());
    }

    // ---- ped walk animation (ambient-ped-anim task) ------------------------------------------------------
    // SPEC USED: spec-ai-runtime.md 20.6.9(a) clip table (state -> clip -> |root translation| -> speed), spec-anim-format.md
    // (header +0x0B = target rig bone count, +0x1C root translation, +0x06/30 = duration seconds, 6b track->bone table
    // handled by sampleClipAtTime), spec-player-runtime 5.4.3 step 4 (speed = clip root speed, so a cycle covers |T| metres).
    // The per-ped variant is the pool slot nearest to the speed ped navigation already picked (the pools in
    // ped_navigator.h are listed in clip order: base, _b, _c, _d, _e, _f[, pfl1]).
    // CHOSEN: no gender source exists on a ped (NearFieldPed has none, navigation draws from the MALE pool), so the male
    // clips (PEDM) are used for every ped; a female ped model therefore walks with the male set.
    // CHOSEN: no ped stand/idle clip is specced in 20.6.9; a stationary ped holds walk frame 0.
    // Clips bind to bones by track INDEX (spec-anim-format 6a), in the clip's own rig order (header +0x0B = that rig's bone
    // count, spec-rig-format 8). When a ped rig's bone count differs from the clip's, the engine's own cross-instance
    // retarget is BY BONE NAME (spec-rig-format 11.12.1, 0x004bcb30: name looked up in the second rig). CHOSEN stand-in for
    // the unspecced reference skeleton: the clip's native rig is taken to be any loaded ped rig whose bone count equals the
    // clip's +0x0B (index order = clip track order); each clip bone is mapped to the same-named bone of the target rig
    // (case-insensitive); target bones with no source stay at rest (identity rotation, zero delta). Counts are in the trace.
    // CHOSEN cost bounds: kMaxPoseUpdatesPerFrame distinct re-poses per frame (nearest peds first), kAnimRadiusM from the
    // camera; peds beyond either keep whatever pose their model's shared buffer last held.
    static constexpr int kMaxPoseUpdatesPerFrame = 24;
    static constexpr double kAnimRadiusM = 80.0;

    struct WalkChoice {
        int32_t record = -1;
        float durationUnits = 0.0f;  // clip header +0x06 (30 Hz units)
        float strideM = 0.0f;        // |root translation| per cycle
        bool variant = false;        // not the base clip
        size_t clipBones = 0;        // the clip's own bone count (+0x0B)
        std::shared_ptr<const std::vector<int>> boneMap;  // clip bone -> target bone (-1 none); null = identity (same rig size)
    };

    bool clipsReady() const { return static_cast<bool>(animContainer_); }

    // Resolves (and caches) the walk clip for `m` given the ped's picked speed. record < 0 = no usable clip.
    const WalkChoice& walkChoiceFor(AmbientPedModel& m, double speedMs) {
        size_t best = 0;
        double bd = 1e30;
        for (size_t i = 0; i < sr3airuntime::kPedWalkPoolMaleSize; ++i) {
            const double d = std::fabs(sr3airuntime::kPedWalkPoolMale[i] - speedMs);
            if (d < bd) { bd = d; best = i; }
        }
        const auto key = std::make_pair(&m, static_cast<int>(best));
        auto it = walkChoice_.find(key);
        if (it != walkChoice_.end()) return it->second;
        WalkChoice c;
        bool retryLater = false;
        const size_t order[2] = {best, 0};
        for (size_t k = 0; k < 2 && c.record < 0; ++k) {
            if (k == 1 && best == 0) break;
            const std::string name = std::string(kMaleWalkClips[order[k]]) + ".anim_pc";
            const int32_t rec = animContainer_ ? clips_.load(*animContainer_, name) : -1;
            const sr3animpose::ResolvedClip* rc = rec >= 0 ? clips_.lookup(rec) : nullptr;
            if (!rc) {
                stats_.clipMissing.insert(name);
                continue;
            }
            const size_t clipBones = rc->animation->field_0x0B_rawCount();
            c.clipBones = clipBones;
            if (clipBones != m.restRig.size()) {
                const AmbientPedModel* ref = nullptr;
                for (const auto& kv : peds_)
                    if (kv.second->ready && kv.second->boneNames.size() == clipBones) { ref = kv.second.get(); break; }
                if (!ref) {
                    stats_.clipBoneMismatch.insert(name + "(" + std::to_string(clipBones) + " bones) vs " + m.stem + "(" +
                                                   std::to_string(m.restRig.size()) + "): no reference rig of that size loaded yet");
                    retryLater = true;  // a compatible model may load later: do not cache this miss
                    continue;
                }
                std::unordered_map<std::string, int> targetByName;
                for (size_t i = 0; i < m.boneNames.size(); ++i) targetByName.emplace(m.boneNames[i], static_cast<int>(i));
                auto map = std::make_shared<std::vector<int>>(clipBones, -1);
                size_t matched = 0;
                for (size_t i = 0; i < clipBones; ++i) {
                    auto t = targetByName.find(ref->boneNames[i]);
                    if (t != targetByName.end()) { (*map)[i] = t->second; ++matched; }
                }
                stats_.clipRetargeted.insert(name + ": " + ref->stem + "(" + std::to_string(clipBones) + ") -> " + m.stem + "(" +
                                             std::to_string(m.restRig.size()) + ") by name, " + std::to_string(matched) + "/" +
                                             std::to_string(clipBones) + " bones matched");
                if (matched * 10 < clipBones * 8) {  // < 80% matched: not a plausible skeleton pair
                    stats_.clipBoneMismatch.insert(name + " vs " + m.stem + ": only " + std::to_string(matched) + "/" +
                                                   std::to_string(clipBones) + " bone names match - not used");
                    continue;
                }
                c.boneMap = std::move(map);
            }
            c.record = rec;
            c.durationUnits = static_cast<float>(rc->animation->durationTotal());
            const auto& t = rc->animation->rootTranslation();
            c.strideM = std::sqrt(t.x * t.x + t.z * t.z);
            if (c.strideM < 0.05f) c.strideM = static_cast<float>(sr3airuntime::kPedWalkPoolMale[order[k]]) * c.durationUnits / 30.0f;
            c.variant = order[k] != 0;
        }
        if (c.record < 0 && retryLater) {
            scratchChoice_ = c;
            return scratchChoice_;
        }
        if (c.record < 0) ++stats_.pedsNoClip;
        return walkChoice_.emplace(key, c).first->second;
    }

    // Re-poses the model's shared body/head buffers to (clip, whole 30 Hz tick). Returns false when nothing could be
    // posed (the buffers then keep their previous pose). `uploaded` reports whether a skin+upload actually ran.
    bool poseWalk(AmbientPedModel& m, ID3D11DeviceContext* ctx, const WalkChoice& c, int tick, bool allowUpload, bool& uploaded) {
        uploaded = false;
        if (!m.poseable || c.record < 0) return false;
        const int64_t key = (static_cast<int64_t>(c.record) << 20) | static_cast<int64_t>(tick);
        if (key == m.lastPoseKey) { ++stats_.poseCacheSkips; return true; }
        if (!allowUpload) return false;
        const auto t0 = std::chrono::steady_clock::now();
        const size_t nb = m.restRig.size();
        sr3animpose::ActiveLayer al;
        al.record = c.record;
        al.weight = 1.0f;
        al.group = 0;
        al.additive = false;
        al.clipTimeUnits = static_cast<float>(tick);
        const size_t nbClip = c.clipBones ? c.clipBones : nb;
        const sr3animpose::PoseComposeResult pr = sr3animpose::ComposeClipPose({al}, clips_, nbClip);
        if (pr.layersActive == 0) return false;
        std::vector<sr3rig::Rotation3> rots(nb, sr3rig::identityRotation3());
        std::vector<std::array<float, 3>> deltas(nb, std::array<float, 3>{0.0f, 0.0f, 0.0f});
        for (size_t b = 0; b < nbClip; ++b) {
            const int tb = c.boneMap ? (*c.boneMap)[b] : static_cast<int>(b);
            if (tb < 0 || static_cast<size_t>(tb) >= nb) continue;
            const sr3anim::BoneSample& s = pr.bones[b];
            rots[static_cast<size_t>(tb)] = sr3rig::quatToRotation3(s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w);
            deltas[static_cast<size_t>(tb)] = s.translationDelta;
        }
        const std::vector<sr3rig::Mat3x4> skinRig = sr3rig::computeAnimatedSkinningMatrices(m.parentIdx, m.restRig, &rots, &deltas);
        auto skinWith = [&](bool palette, const std::vector<sr3mesh::Vertex>& verts) {
            std::vector<sr3rig::Mat3x4> skinMesh(nb);
            for (size_t b = 0; b < nb; ++b)
                skinMesh[b] = palette ? sr3rig::conjugateToMeshSpaceInverted(skinRig[b]) : sr3rig::conjugateToMeshSpace(skinRig[b]);
            return sr3rig::skinVertices(verts, skinMesh, nullptr);
        };
        bool ok = m.body->updatePositions(ctx, skinWith(m.bodyPalette, m.bodyVerts));
        if (m.head && !m.headVerts.empty()) ok = m.head->updatePositions(ctx, skinWith(m.headPalette, m.headVerts)) && ok;
        if (ok) m.lastPoseKey = key;
        uploaded = true;
        ++stats_.poseUpdates;
        stats_.poseMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return ok;
    }

    AmbientModelStats& mutableStats() { return stats_; }

    // Drawing (same lighting parameters the placeholder draws used).
    static void DrawPed(AmbientPedModel& m, ID3D11DeviceContext* ctx, const float vp[16], const float world[16],
                        const float* sun, const float* amb, const float* sunCol,
                        const float* lightVP = nullptr, ID3D11ShaderResourceView* shadowSrv = nullptr,
                        ID3D11SamplerState* shadowSmp = nullptr) {
        if (!m.ready) return;
        if (m.bodyTextured)
            m.body->drawTextured(ctx, vp, m.bodySrv, world, sun, amb, sunCol, lightVP, shadowSrv, shadowSmp);
        else
            m.body->draw(ctx, vp, sr3render::MeshDrawMode::Checker, nullptr, world, nullptr, sun, amb, sunCol, lightVP, shadowSrv, shadowSmp);
        if (m.head) {
            if (m.headTextured)
                m.head->drawTextured(ctx, vp, m.headSrv, world, sun, amb, sunCol, lightVP, shadowSrv, shadowSmp);
            else
                m.head->draw(ctx, vp, sr3render::MeshDrawMode::Checker, nullptr, world, nullptr, sun, amb, sunCol, lightVP, shadowSrv, shadowSmp);
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

    AmbientVehicleModel* loadedVehicle(const std::string& key, bool exemptFromCap = false) {
        auto it = vehicles_.find(key);
        if (it != vehicles_.end()) {
            ++stats_.cacheHits;
            return it->second.get();
        }
        if (failedVehicles_.count(key) || (!exemptFromCap && vehicles_.size() >= kMaxVehicleModels)) return nullptr;
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
        if (brakingDecelLookup_) {
            std::string base = key;
            if (base.size() > 2 && base.compare(base.size() - 2, 2, "_0") == 0) base.resize(base.size() - 2);
            try {
                m->aiMaxBrakingDecel = brakingDecelLookup_(base);
            } catch (const std::exception&) {
            }
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

    AmbientPedModel* loadedPed(const std::string& row, bool exemptFromCap = false) {
        auto it = peds_.find(row);
        if (it != peds_.end()) {
            ++stats_.cacheHits;
            return it->second.get();
        }
        if (failedPeds_.count(row) || (!exemptFromCap && peds_.size() >= kMaxPedModels)) return nullptr;
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
            m.body->setDynamicVertices(true);
            m.bodyVerts = verts;
            m.bodyPalette = palette;
            m.parentIdx = parentIdx;
            m.restRig.resize(bones.size());
            m.boneNames.resize(bones.size());
            for (size_t i = 0; i < bones.size(); ++i) {
                m.restRig[i] = bones[i].restPosition;
                m.boneNames[i] = lower(bones[i].name);
            }
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
                        if (std::getenv("SR3_TRACE_PEDTEX")) {
                            std::printf("pedtex: %s body mat %u diffuse='%s' -> %s %ux%u %s |", def.stem.c_str(), i, d ? d->c_str() : "(none)",
                                        m.bodySrv[i] ? "bound" : "UNRESOLVED", m.bodyTex[i].width, m.bodyTex[i].height,
                                        m.bodyTex[i].dxgiFormatName.c_str());
                            for (const auto& tr : b.materialsMutable()[i].textures)
                                std::printf(" [%08x %s]", static_cast<unsigned>(tr.paramHash), tr.name.c_str());
                            std::printf("\n");
                        }
                    }
                    m.bodyTextured = true;
                }
            } catch (const std::exception&) {}
            // head (lod) - optional
            if (!def.headLod.empty() && findEntry(cont, def.headLod + ".ccmesh_pc", hcb, hgb, def.headLod + ".gcmesh_pc") &&
                !hgb.empty())
                buildHead(m, def, cont, hcb, hgb, bones, parentIdx);
            m.poseable = true;
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
            head->setDynamicVertices(true);
            std::string err;
            if (!head->initialise(device_, err) ||
                !head->upload(device_, mesh, 0, 0, err, 0, &bindPositions, haveDup ? &dup : nullptr))
                return;
            m.head = std::move(head);
            m.headVerts = verts;
            m.headPalette = palette;
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
    bool noWalkAnim_ = false;
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
    BrakingDecelLookupFn brakingDecelLookup_;
    std::map<std::string, std::unique_ptr<AmbientPedModel>> peds_;
    std::set<std::string> failedVehicles_, failedPeds_;
    AmbientModelStats stats_;
    WalkChoice scratchChoice_;
    MappedBytes animMap_;
    std::unique_ptr<vpp::Container> animContainer_;
    RealClipRegistry clips_;
    std::map<std::pair<const AmbientPedModel*, int>, WalkChoice> walkChoice_;
    static constexpr const char* kMaleWalkClips[6] = {"ped_bs_walk", "ped_bs_walk_b", "ped_bs_walk_c", "ped_bs_walk_d", "ped_bs_walk_e", "ped_bs_walk_f"};
};

}  // namespace sr3viewer

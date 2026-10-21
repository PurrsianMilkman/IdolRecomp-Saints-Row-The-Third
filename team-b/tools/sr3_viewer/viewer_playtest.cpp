#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_playtest_session.h"

using namespace sr3viewer;

namespace sr3viewer {

// ===========================================================================
// WALKABLE GROUND QUERY (TEAM B task G1, 2026-10-18) - spec-zone-data-format.md Sec13.5.1 port recipe step 3.
// Ground at (x, z) from a start height y0: cast a ray from (x, y0 + 0.05, z) to (x, y0 - 500, z) and take the closest
// hit = the highest surface at or below y0 + 0.05 (the engine's ground snap 0x0075dfe0, FINAL). On a miss the caller
// leaves Y unchanged. The triangles are tile.collisionBvh (viewer_city_ground.cpp loadCityGroundTileCollision(): the
// world-space `0x2237` tail collision mesh of `~s<CCRR>`, strip stand-in, CHOSEN). The 0x2244 heightmap is NOT a source
// (aircraft clearance, 13.5.1(d)).
// CHOSEN: a hit triangle only counts as walkable ground when |normal.y| >= 0.7 (spec item 5: walkable normal y >= 0.715,
// 0.7 when not moving; |.| because the strip stand-in's alternating winding gives no usable facing).
// ===========================================================================
constexpr double kGroundRayUp = 0.05;       // FINAL: 0x01115d6c
constexpr double kGroundRayDown = 500.0;    // FINAL: 0x01153aa8 (-500)
constexpr double kWalkableNormalY = 0.7;    // spec 13.5.1 item 5

static void sampleGroundMeshBvhNode(const CollisionBvh& bvh, int nodeIdx, double x, double z, double rayY0,
                                    double rayY1, bool& found, double& bestY) {
    if (nodeIdx < 0) return;
    const CollisionBvhNode& node = bvh.nodes[static_cast<size_t>(nodeIdx)];
    if (x < node.min[0] || x > node.max[0] || z < node.min[2] || z > node.max[2]) return;
    if (node.left < 0) {  // leaf
        for (int i = node.triStart; i < node.triStart + node.triCount; ++i) {
            const CollisionTriangle& t = bvh.tris[static_cast<size_t>(i)];
            double f;
            sr3player::Vec3 n;
            if (!sr3player::RayVsTriangle3D(x, rayY0, z, x, rayY1, z, static_cast<double>(t.v0[0]),
                                             static_cast<double>(t.v0[1]), static_cast<double>(t.v0[2]),
                                             static_cast<double>(t.v1[0]), static_cast<double>(t.v1[1]),
                                             static_cast<double>(t.v1[2]), static_cast<double>(t.v2[0]),
                                             static_cast<double>(t.v2[1]), static_cast<double>(t.v2[2]), f, n))
                continue;
            // geometric normal from the vertices (orientation-independent)
            const double ux = static_cast<double>(t.v1[0] - t.v0[0]), uy = static_cast<double>(t.v1[1] - t.v0[1]),
                         uz = static_cast<double>(t.v1[2] - t.v0[2]);
            const double vx = static_cast<double>(t.v2[0] - t.v0[0]), vy = static_cast<double>(t.v2[1] - t.v0[1]),
                         vz = static_cast<double>(t.v2[2] - t.v0[2]);
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 1e-12 || std::fabs(ny) / len < kWalkableNormalY) continue;
            const double y = rayY0 + (rayY1 - rayY0) * f;
            if (!found || y > bestY) {  // closest hit from the top = highest surface at/below the start
                bestY = y;
                found = true;
            }
        }
        return;
    }
    sampleGroundMeshBvhNode(bvh, node.left, x, z, rayY0, rayY1, found, bestY);
    sampleGroundMeshBvhNode(bvh, node.right, x, z, rayY0, rayY1, found, bestY);
}

// Searches every resident tile's walkable collision for the ground at (x, z) from start height `startY`.
// Returns false (outY untouched) on a miss.
bool SampleGroundMeshHeight(const std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>>& tiles, double x,
                            double z, double startY, double& outY) {
    bool found = false;
    double bestY = 0.0;
    const double rayY0 = startY + kGroundRayUp, rayY1 = startY - kGroundRayDown;
    for (const auto& kv : tiles) {
        const CollisionBvh& bvh = kv.second->collisionBvh;
        if (bvh.empty()) continue;
        sampleGroundMeshBvhNode(bvh, bvh.root, x, z, rayY0, rayY1, found, bestY);
    }
    if (found) outY = bestY;
    return found;
}

// CHOSEN hole fill, SPAWN ONLY (never used for per-frame support): the strip stand-in (spec 13.5.1(c) GUESS) leaves holes
// (2 of the spec's 6 test points miss), and a spawn has no earlier Y to hold. When the direct ray misses, probe rings of
// radius 1, 2, 3, 4 m (8 azimuths each) from the same start height and use the median hit Y of the first ring that hits.
// Returns the ring radius used (0 = none found).
double SampleGroundMeshHoleFill(const std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>>& tiles, double x,
                                double z, double startY, double& outY) {
    for (int r = 1; r <= 4; ++r) {
        std::vector<double> ys;
        for (int k = 0; k < 8; ++k) {
            const double a = 0.7853981633974483 * k;
            double y = 0.0;
            if (SampleGroundMeshHeight(tiles, x + r * std::cos(a), z + r * std::sin(a), startY, y)) ys.push_back(y);
        }
        if (!ys.empty()) {
            std::sort(ys.begin(), ys.end());
            outY = ys[ys.size() / 2];
            return static_cast<double>(r);
        }
    }
    return 0.0;
}

// Topmost walkable surface at (x, z) over every resident tile (spec item 4's "collision AABB max Y + 1" start height).
bool SampleGroundMeshTopHeight(const std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>>& tiles, double x,
                               double z, double& outY) {
    double top = -1.0e30;
    bool any = false;
    for (const auto& kv : tiles) {
        const CollisionBvh& bvh = kv.second->collisionBvh;
        if (bvh.empty()) continue;
        const CollisionBvhNode& r = bvh.nodes[static_cast<size_t>(bvh.root)];
        if (x < r.min[0] || x > r.max[0] || z < r.min[2] || z > r.max[2]) continue;
        any = true;
        top = (std::max)(top, static_cast<double>(r.max[1]));
    }
    if (!any) return false;
    return SampleGroundMeshHeight(tiles, x, z, top + 1.0 - kGroundRayUp, outY);
}


// Loads ONE tile's real Heightmap (0x2244) record (spec-zone-data-format.md
// Sec13.5) - the live-playtest ground-following fix's own elevation-data
// source (runPlaytest(), below, via sr3player/ground_follow.h). Separate,
// additive cache/loader from loadCityGroundTile() above - this project's
// own "the Heightmap handler stores the record's absolute body pointer
// unconditionally... no inline decode of any kind" (Sec13.5) means
// Heightmap is a DIFFERENT content kind from 0x2237 (ground mesh), with its
// own dedicated registry, not a reuse of the same record this function's
// sibling already reads.
//
// Mirrors loadCityGroundTile()'s own base/`~s` fallback priority exactly
// (findTileZoneEntryName()'s "base" then "s_base" candidates, in that
// order) - both are classified HeightmapContainerKind::Ordinary, since
// neither is the tile's own `~al` "Level Always Loaded" companion
// (Sec13.5's own CONFIRMED spacing discriminator: `~al` means 40m/8x7,
// ordinary means 10m/32x28 - see zone_heightmap.h's own file comment).
// Deliberately does NOT fall back to an `~al`/variant candidate - the same
// reasoning loadCityGroundTile()'s own top comment gives for ground mesh
// applies here too: `~al` is a conceptually different, always-resident
// container, not this tile's own streamed-in terrain, and silently
// promoting it to "this tile's elevation" would draw a wrong-provenance
// answer rather than honestly reporting none was found.
//
// Returns false with `why` set if no real Heightmap record is found for
// this tile in any given archive - the caller (runPlaytest()) falls back
// to the one-time measured `finalGroundY` constant in that case, exactly
// as it already did for every tile before this task (see this function's
// own caller for the fallback wiring).
bool loadCityHeightmapTile(int col, int row, std::vector<std::unique_ptr<CityArchive>>& archives,
                            sr3zone::HeightmapRecord& outRecord, std::string& why) {
    for (size_t ai = 0; ai < archives.size(); ++ai) {
        CityArchive& a = *archives[ai];
        std::string cznName;
        std::vector<TileZoneCandidate> candidates;
        const bool haveBase = findTileZoneEntryName(*a.top, col, row, cznName, &candidates);
        std::string sBaseName;
        for (const auto& c : candidates) {
            if (c.kind == "s_base") {
                sBaseName = c.entryName;
                break;
            }
        }
        std::vector<std::string> tryNames;
        if (haveBase) tryNames.push_back(cznName);
        if (!sBaseName.empty()) tryNames.push_back(sBaseName);

        for (const auto& name : tryNames) {
            std::vector<uint8_t> cznBytes, siblingUnused;
            if (!findEntry(*a.top, name, cznBytes, siblingUnused, "") || cznBytes.empty()) continue;
            const std::vector<sr3zone::HeightmapRecord> found = sr3zone::Heightmap::locate(
                vpp::ByteView(cznBytes.data(), cznBytes.size()), sr3zone::HeightmapContainerKind::Ordinary);
            if (!found.empty()) {
                outRecord = found.front();
                return true;
            }
        }
    }
    why = "no real Heightmap (0x2244) record found for tile " + std::to_string(col) + std::to_string(row) +
          " in any given archive";
    return false;
}


// Narrows a resolved localisation string (sr3locstr::LeStringsLookup's own
// u16string text) back into the narrow std::string shape drawHudLabel's
// text_tag path expects - the exact inverse of decodeRawTextTag's own
// byte-for-byte widen (above: "static_cast<char16_t>(static_cast<unsigned
// char>(c))"), so a resolved English string round-trips through
// hudSetLabel -> drawHudLabel -> decodeRawTextTag unchanged (every shipped
// US text is ASCII/Latin-1 range, spec-localization-format.md's own "the
// longest US text is 369 code units" census never cites anything past that
// range). Same byte-truncation shape as vint_text.cpp's own anonymous-
// namespace narrowLow() (not reachable from here - this is a local, not a
// shared, helper).
std::string narrowLocText(const std::u16string& s) {
    std::string out;
    out.reserve(s.size());
    for (char16_t c : s) out.push_back(static_cast<char>(static_cast<uint8_t>(c & 0xff)));
    return out;
}

} // namespace sr3viewer

int runPlaytest(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: sr3_viewer playtest <characters.vpp_pc> <misc_tables.vpp_pc> "
                     "<sr3_city_0.vpp_pc> [<more city archives>...]\n"
                     "                           [--headless] [--no-input] [--frames N]\n"
                     "                           [--capture <out.png>] [--capture-frame N]\n"
                     "                           [--capture-frames F0,F1,...] [--size W H]\n"
                     "                           [--spawn X Z] [--far F] [--anim-archive <path>]\n"
                     "                           [--mouse-sensitivity F]\n"
                     "                           [--script-forward-frames N] [--script-turn-frames N]\n"
                     "                           [--hud <registration_list.txt> <misc.vpp_pc>]\n"
                     "                           [--no-vehicle] [--vehicle-archive <path>]\n"
                     "                           [--vehicle-name <name.ccar_pc>] [--wheels-archive <path>]\n"
                     "                           [--interface-archive <path>]\n"
                     "                           [--vehicle-spawn X Z] [--script-vehicle]\n"
                     "                           [--script-vehicle-drive-frames N] [--no-collision] [--no-node-ground] [--zone-variant=NAME]\n"
                     "                           [--script-enter-ambient-at=FRAME] [--script-enter-ambient-drive-frames=N]\n"
                     "                           [--script-enter-ambient-count=N] [--script-enter-ambient-reenter]\n"
                     "                           [--script-roadblock] [--script-fire] [--script-weapons]\n"
                     "                           [--gps-waypoint=X,Z] [--gps-waypoint-at=FRAME] [--gps-waypoint-mission]\n"
                     "                           [--script-jump-at=FRAME]... [--script-crouch-frames=A-B] [--jump-height=M]\n"
                     "                           [--script-hud-prompt-response] [--auto-answer-hud-prompts [N]]\n"
                     "                           [--mission=<stem>] [--mission-registration-list <path>]\n"
                     "                           [--mission-package-tsv <path>] [--mission-timeout=<seconds>]\n"
                     "                           [--auto-objective [N]] [--auto-combat] [--auto-seat-mission-vehicle] [--auto-skip-cutscenes]\n"
                     "                           [--no-audio] [--sfx-archive <path>]\n"
                     "       Playtest v0 - the first genuinely interactive build. See this\n"
                     "       command's own doc comment (above runPlaytest() in this file) for the\n"
                     "       full REAL/CHOSEN/SCOPE breakdown. --headless forces the window hidden\n"
                     "       (for the ctest smoke test only - interactive use wants the real window,\n"
                     "       the one deliberate exception to this project's hide-by-default rule, see\n"
                     "       sr3render/window.h's own doc comment); --no-input replaces live\n"
                     "       keyboard/mouse with a deterministic scripted sequence (same convention\n"
                     "       runWalkDemo()'s own --no-input already uses for its debug camera) so\n"
                     "       ctest can run this command headlessly and deterministically. One real\n"
                     "       vehicle is parked near the spawn point and driveable (walk up, press E) -\n"
                     "       --no-vehicle disables it entirely; --vehicle-archive/--vehicle-name/\n"
                     "       --wheels-archive override its real .ccar_pc/wheel-catalogue source (CHOSEN\n"
                     "       default: derived from <characters.vpp_pc>'s own directory, same\n"
                     "       dlc1.vpp_pc::car_4dr_genki_0.ccar_pc/vehicles_preload.vpp_pc pairing\n"
                     "       runDriveDemo() uses); --vehicle-spawn overrides where it parks (CHOSEN\n"
                     "       default: a few metres from --spawn); --script-vehicle adds a deterministic\n"
                     "       enter/drive/exit sequence under --no-input (ctest use, see\n"
                     "       real_playtest_vehicle_smoke_test.cpp); --script-vehicle-drive-frames\n"
                     "       overrides that sequence's own scripted drive duration (default 120 frames,\n"
                     "       unchanged) for a long deterministic drive, e.g. for a world-streaming-\n"
                     "       reconcile long-drive proof (spec-world-streaming.md Sec10.4/Sec11); the\n"
                     "       vehicle now has REAL world collision (PlaytestVehicleRig::Advance(), spec-\n"
                     "       physics-runtime.md Sec10a) so a long straight-line script CAN now stop\n"
                     "       early against real geometry along its path, correctly - pick a clear\n"
                     "       heading/spawn when a long uninterrupted drive is wanted. --no-collision\n"
                     "       leaves the player's pm.collisionHook unset (free, unobstructed movement) -\n"
                     "       a CHOSEN test-only accommodation for a long deterministic WALK proof of the\n"
                     "       same streaming behaviour, since the real city's building collision geometry\n"
                     "       blocks a straight scripted walk after only ~36m from the default spawn.\n"
                     "       Near-field ambient vehicle traffic is ALWAYS ON (spec-ai-runtime.md\n"
                     "       Sec17.4c) - no flag needed. The police roadblock spawn (spec-ai-runtime.md\n"
                     "       Sec19.7) is driven by the police notoriety level (2-5, Sec20.6.10) and can also be\n"
                     "       force-armed live with 'B' (debug gate kept alongside the notoriety trigger;\n"
                     "       R is reload). --script-roadblock scripts a deterministic\n"
                     "       arm-then-disarm 'B' press pair under --no-input (ctest use, see\n"
                     "       real_playtest_traffic_roadblock_smoke_test.cpp).\n"
                     "       JUMP/CROUCH: Space jumps, C crouches (held); keys are CHOSEN (see\n"
                     "       --script-jump-at / --script-crouch-frames).\n"
                     "       WEAPON HANDLING (gap #17; keys are CHOSEN, the spec gives none): R reload,\n"
                     "       1-8 select Inv_Slot k-1, 9 last weapon, Q previous, Tab next; --script-weapons scripts fire-until-\n"
                     "       empty -> reload -> switch -> fire and prints weapon-state-trace lines (the\n"
                     "       starting inventory is CHOSEN).\n"
                     "       WEAPON FIRE (THIS TASK, spec-weapons-runtime.md Sec2.2c/Sec4.4): Brad is\n"
                     "       equipped with the first real pistol-class (else first non-vehicle-class) row\n"
                     "       parsed from the real weapons.xtbl in <misc_tables.vpp_pc> - left mouse button\n"
                     "       (on foot only) fires it from the live follow-camera's own eye/forward ray\n"
                     "       through the real hitscan dispatch chain (sr3weapons::WeaponInstance::fire(),\n"
                     "       already-merged feature/hitscan-fire-dispatch) against the always-on near-field\n"
                     "       ambient pedestrian population; a hit routes through\n"
                     "       sr3luahost::EngineStateLethalDamageSink into the real\n"
                     "       EngineState::applyGuaranteedLethalHit() death/ragdoll mechanism (spec-player-\n"
                     "       runtime.md Sec15.3a) and the real gunfire-to-flee reaction chain\n"
                     "       (sr3airuntime::EvaluateGunfireReaction/ChooseFleeOrCower, spec-ai-runtime.md\n"
                     "       Sec20.4). --script-fire scripts a single deterministic left-click under\n"
                     "       --no-input (ctest use, see real_playtest_weapon_fire_smoke_test.cpp).\n"
                     "       --mission=<stem> (Phase 1 mission-host debug flag, OFF unless given): drives\n"
                     "       the real shipped mission Lua script for <stem> (e.g. m01) once per rendered\n"
                     "       frame, through the SAME Host/EngineState --hud already builds (constructed if\n"
                     "       not already, from --mission-registration-list, or from --hud's own list when\n"
                     "       --hud is also given) - see this function's own doc comment for the full\n"
                     "       CONFIRMED/CHOSEN/OPEN breakdown. --mission-registration-list <path> is\n"
                     "       required unless --hud is also given (then --hud's own list is reused).\n"
                     "       --mission-package-tsv <path> overrides where tools/mission_package_per_\n"
                     "       container.tsv is read from (CHOSEN default: next to the registration list,\n"
                     "       same directory convention tools/lua_host_run.cpp's own toolsDir uses).\n"
                     "       --mission-timeout=<seconds> overrides the CHOSEN default 30s real-time safety\n"
                     "       cutoff (0 disables it - not recommended for an interactive session). A stem\n"
                     "       whose script lives outside the given city archives (a DLC mission) needs its\n"
                     "       own dlc1/dlc2/dlc3.vpp_pc passed as one of this command's own [<more city\n"
                     "       archives>...] tokens. --script-hud-prompt-response (only meaningful with\n"
                     "       --mission=<stem>): each tick, any NEWLY recorded live hud_prompt(target,\n"
                     "       promptId, callbackName) request (spec-lua-api-behaviour.md Sec13.3) is answered\n"
                     "       by spawning a coroutine for its own callbackName (the SAME BareThreadTable\n"
                     "       allocate()-then-let-the-scheduler-resume-it mechanism Sec1.14's confirmed\n"
                     "       on_trigger/on_death/etc dispatch already uses, reused here rather than\n"
                     "       invented separately) - the scripted stand-in for a real player pressing the\n"
                     "       prompted button, e.g. m05's M05_JUMP prompt / m05_jump_local.\n"
                     "       --auto-answer-hud-prompts [N] (THIS TASK, hud-prompt-autoanswer-harness): a\n"
                     "       --no-input-friendly generalization of --script-hud-prompt-response - same\n"
                     "       underlying dispatch (any CURRENTLY showing hud_prompt is answered by spawning a\n"
                     "       coroutine for its own registered callbackName, generic, not specific to M05_JUMP\n"
                     "       or any other one prompt), but only after N frames of that SAME prompt request\n"
                     "       having been showing (default 15, i.e. roughly half a second at the --no-input\n"
                     "       fixed 1/30s step) rather than answering on the very tick it first appears. Lets\n"
                     "       a scripted/automated --no-input run (e.g. a mission-drive verification harness,\n"
                     "       see results/mission_drive_*.md) get PAST a real player-input-gated HUD-prompt\n"
                     "       wait without a real button press, so that run can keep going and surface\n"
                     "       whatever the mission's own NEXT stall point is, instead of tick-budget-stalling\n"
                     "       forever on the prompt. Implies --script-hud-prompt-response's own dispatch\n"
                     "       mechanism (do not pass both; --auto-answer-hud-prompts wins if both are given).\n"
                     "       N is an optional trailing integer (e.g. --auto-answer-hud-prompts 30); omitted,\n"
                     "       the CHOSEN default of 15 frames applies.\n"
                     "       PAUSE MENU (THIS TASK, spec-frontend-menu-runtime.md Sec7/Sec9 item 2): Esc\n"
                     "       opens/closes the real pause-menu-open binding (CBA_GAC_PAUSE_MENU/0x55 ->\n"
                     "       gate -> shared game-pause push (4,1) -> \"dialog_pause_show\") instead of\n"
                     "       quitting - press F10 to quit instead (the window's title-bar/X button also\n"
                     "       still works regardless). --interface-archive overrides where the real\n"
                     "       pause_menu_common.vint_doc is read from for the overlay's own best-effort\n"
                     "       real-document load (CHOSEN default: derived from <characters.vpp_pc>'s own\n"
                     "       directory, same convention --vehicle-archive's own default uses).\n"
                     "       AUDIO (audio-playback task, updated by footstep-real-event task 2026-10-10):\n"
                     "       a real XAudio2 output device opens by default (interactive runs only - see\n"
                     "       below). The real Wwise footstep event (the literal string \"FOOTSTEP\", from\n"
                     "       anim_files.xtbl Sounds/Sound rows, spec-audio-runtime.md Sec11a.8) is looked\n"
                     "       up by name with CHOSEN switches MATERIAL_TYPE:CONCRETE+SHOE_TYPE:SNEAKER (no\n"
                     "       author-baked material context exists for open-world walking); resolving that\n"
                     "       identity to a specific Wwise media id is still OPEN (no switch-container HIRC\n"
                     "       layout reverse-engineered anywhere in this project), so the SAME real,\n"
                     "       independently decode-verified SFX media findFirstPlayableSfxClip() resolves\n"
                     "       (by raw HIRC id, Sec9.8) still plays - now correctly labelled as standing in\n"
                     "       for a KNOWN real identity, not an arbitrary pick. Timing: fires on the real\n"
                     "       left-foot/right-foot anim-clip Trigger Frame crossing when that data is found\n"
                     "       for the walk clip actually played (Sec11a.8), else a footstep-shaped CADENCE\n"
                     "       fallback. Flat (non-attenuated) volume - distance is computed and logged from the\n"
                     "       real emitter/listener positions (Sec11a.1/Sec11a.2) but the actual Wwise\n"
                     "       distance-attenuation curve is confirmed to live entirely inside Wwise's own\n"
                     "       statically-linked code (Sec11a.5) - not reachable, not guessed. --no-audio\n"
                     "       disables the device entirely (still an interactive run, just silent).\n"
                     "       --headless and --no-input NEVER open the device regardless of --no-audio (CI\n"
                     "       smoke tests must stay genuinely silent). --sfx-archive overrides which real\n"
                     "       `.vpp_pc` the demo clip is resolved from (CHOSEN default: sounds.vpp_pc next to\n"
                     "       <characters.vpp_pc> - empirically the smallest real archive this task found\n"
                     "       with a kind-2 Sound item that actually resolves+decodes through this\n"
                     "       mechanism; cutscene_sounds.vpp_pc, this project's own independently oracle-\n"
                     "       verified 493/493 Vorbis corpus, was tried first but is pure cutscene DIALOGUE -\n"
                     "       spec-audio-runtime.md Sec9.8 confirms voice lines never touch the embedded\n"
                     "       Sound descriptor this mechanism reads, so nothing there ever resolves).\n");
        return 1;
    }
    // Heap-allocated: the session holds what used to be ~hundreds of stack locals.
    auto sessHolder = std::make_unique<PlaytestSession>();
    return runPlaytestSession(*sessHolder, argc, argv);
}

namespace sr3viewer {

std::atomic<const char*> g_curStep{"startup"};
std::atomic<unsigned long long> g_lastTickMs{0};

int runPlaytestSession(PlaytestSession& sess, int argc, char** argv) {
    g_curStep.store("setup");
    g_lastTickMs.store(GetTickCount64());
    if (int rc = setupArgs(sess, argc, argv); rc != -1) return rc;
    if (int rc = setupBrad(sess); rc != -1) return rc;
    if (int rc = setupTables(sess); rc != -1) return rc;
    if (int rc = setupAnimRuntime(sess); rc != -1) return rc;
    if (int rc = setupPoseAndInput(sess); rc != -1) return rc;
    if (int rc = setupCityArchives(sess); rc != -1) return rc;
    if (int rc = setupWindowDevice(sess); rc != -1) return rc;
    if (sess.hooks.afterWindow) sess.hooks.afterWindow(sess);
    if (int rc = setupAudio(sess); rc != -1) return rc;
    if (int rc = setupRenderAssets(sess); rc != -1) return rc;
    if (int rc = setupHudAndMission(sess); rc != -1) return rc;
    setupActivity(sess);
    setupCollect(sess);
    if (sess.hooks.beforeWorld) {
        if (int rc = sess.hooks.beforeWorld(sess); rc != -1) return rc;
    }
    if (int rc = setupNpcAiState(sess); rc != -1) return rc;
    if (int rc = setupStreamingGround(sess); rc != -1) return rc;
    if (int rc = setupVehicle(sess); rc != -1) return rc;
    if (int rc = setupTraffic(sess); rc != -1) return rc;
    if (int rc = setupRoadblockOccupant(sess); rc != -1) return rc;
    if (int rc = setupLoopState(sess); rc != -1) return rc;

    sr3render::Window& window = *sess.window_;
    const bool traceSteps = std::getenv("SR3_TRACE_STEPS") != nullptr;
    auto TR = [&](const char* n) { g_curStep.store(n); if (traceSteps && sess.pauseMenuOpen) std::printf("step: %s\n", n); };
    while (window.pumpMessages()) {
        PlaytestFrame frm;
        g_lastTickMs.store(GetTickCount64());
        TR("stepFrameBegin"); if (stepFrameBegin(sess, frm)) break;
        TR("stepInput"); if (stepInput(sess, frm)) break;
        TR("stepPlayerVitals"); stepPlayerVitals(sess, frm);
        TR("stepActivity"); stepActivity(sess, frm);
        TR("stepCollect"); stepCollect(sess, frm);
        TR("stepVehicleControls"); stepVehicleControls(sess, frm);
        TR("stepWorld"); stepWorld(sess, frm);
        TR("stepTrafficSignals"); stepTrafficSignals(sess, frm);
        TR("stepTrafficVehicles"); stepTrafficVehicles(sess, frm);
        TR("stepTrafficPeds"); stepTrafficPeds(sess, frm);
        TR("stepVehicleCollisions"); stepVehicleCollisions(sess, frm);
        TR("stepTrafficRoadblock"); stepTrafficRoadblock(sess, frm);
        TR("stepPolice"); stepPolice(sess, frm);
        TR("stepHomies"); stepHomies(sess, frm);
        TR("stepPlayerView"); try { stepPlayerView(sess, frm); } catch (const std::exception& e) { std::printf("EXC: %s\n", e.what()); throw; }
        TR("stepMissionBridge"); stepMissionBridge(sess, frm);
        TR("stepMissionDrive"); stepMissionDrive(sess, frm);
        TR("stepGps"); stepGps(sess, frm);
        TR("stepRender"); if (stepRender(sess, frm)) break;
    }
    if (sess.hooks.afterLoop) sess.hooks.afterLoop(sess);
    writeSaveAtExit(sess);
    reportPlaytest(sess);
    return 0;
}

}  // namespace sr3viewer

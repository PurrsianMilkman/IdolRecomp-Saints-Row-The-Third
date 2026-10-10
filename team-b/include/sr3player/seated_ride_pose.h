#pragma once

// sr3player/seated_ride_pose.h - replaces the standing BIND POSE a vehicle
// occupant currently renders with (tools/sr3_viewer.cpp's playtest command)
// with a real, data-driven SEATED/DRIVING pose, per spec-player-runtime.md
// Sec7.4b ("Driving animation state: the seated/driving pose and enter/exit
// sequence"). Scope: the STEADY ride pose once seated (state 0x22, Sec8.5) -
// NOT the animated enter/exit transition clips (states 0xa-0x13/0x1a-0x21,
// explicitly out of scope per this task's own brief) and NOT the rider-
// parameter writer's 11 continuous controller params (steering/MPH/lean/
// wheelie, also out of scope - this is a single static ride pose, not a
// live animated blend).
//
// THE REAL MECHANISM THIS FILE REPRODUCES (CONFIRMED, spec-player-runtime.md
// Sec7.4b, cross-checked directly against this session's own extraction of
// the real shipped vi_ride.xtbl/vehicle_interaction_info.xtbl from
// misc_tables.vpp_pc - see ChosenRealClipForRideAnimation()'s own comment
// for exactly what was verified and how): there is no hardcoded car/
// motorcycle/watercraft branch anywhere in the real engine for this. A
// vehicle's descriptor carries a named Vehicle_Interaction_Info row
// (spec-vehicle-data.md +0xA38/+0xA7C); that row's per-seat Ride_Animations
// field is itself just a NAME, resolved against vi_ride.xtbl's own named
// Vehicle_Interaction_Animation_Set rows (sr3tables_vehicle_world::
// VehicleInteractionAnimationSet, already parsed by this project's existing
// xtbl reader, src/tables_vehicle_world.cpp) down to a resolved Animation
// STRING (e.g. "drive network").
//
// IMPORTANT CORRECTION (caught verifying this file against spec-player-
// runtime.md Sec9.0 item2/Sec9.1/Sec9.4.1/Sec9.6.4 directly, 2026-10-09 -
// an earlier draft of this comment wrongly claimed ride Elements are
// matched by "the SAME wildcard rule" as enter/exit): they are NOT. The
// spec is explicit that the enter/exit wildcard matcher (0x00b0a650,
// sr3player::MatchElementKey() below) is "ride-only... not by this rule" -
// i.e. it is NEVER used for vi_ride.xtbl in the real engine (vehicle_
// occupancy.h's own MatchElementKey() doc comment already says so: "this
// rule never reads vi_ride.xtbl at all"). The REAL ride matcher is a
// DIFFERENT, EXACT six-token compare (0x00b0a580, no wildcards), driven by
// 0x00b08aa0 (which also retries with token2 forced to "turn left"/"turn
// right") and its retry wrapper 0x00b08b80 (which, on any miss, relaxes the
// key in four fixed steps: clear token5; clear tokens 3+4; force token1 =
// "forward"; force token0 = "passenger" - Sec9.6.4), falling back to built-
// in animation ids only if every relaxed attempt still misses.
//
// WHY THIS FILE STILL REUSES MatchElementKey() WITH AN ALL-"ANY" KEY (a
// CHOSEN implementation shortcut, not a port of 0x00b08aa0/0x00b08b80's own
// logic - that retry cascade was never re-derived here, same as the file's
// pre-existing "never traced in detail" note below): REAL DATA CONFIRMS a
// car's ride set is almost always a SINGLE Element (Sec7.4b: "An ordinary
// car's ride set ('standard driver') is a single Element ('drive network')
// covering the entire seated loop") - verified directly this session:
// misc_tables.vpp_pc's real vi_ride.xtbl has exactly one <Element> under
// <Name>standard driver</Name>, with <Animation>drive network</Animation>;
// vehicle_interaction_info.xtbl's real "truck_2dr_classic01" row (an
// ordinary car) has <Seat>Driver</Seat> with <Ride_Animations>standard
// driver</Ride_Animations>. For a single-Element set whose own token 2 is
// one of {none, turn left, turn right} (true of 454/454 shipped ride
// Elements, Sec9.6.4's position-2 tally), the real retry cascade's final
// step is GUARANTEED to converge on token0=passenger/token1=forward/
// token3=none/token4=none/token5=none plus one of the three token2
// variants 0x00b08aa0 always tries - i.e. the real algorithm always ends up
// selecting that one Element regardless of the live key. An all-"any" key
// against MatchElementKey() reproduces that guaranteed single-Element
// outcome directly, without re-implementing the cascade; it is NOT claimed
// to be the same mechanism, only the same final answer for this shape of
// data. It is NOT a faithful port of the real per-frame combat/aim-variant
// reselection among a richer multi-Element set's later Elements (motorcycle/
// watercraft - explicitly out of scope, see this file's own scope note
// above): a richer set's first Element is still returned (documented CHOSEN
// default below), which is only guaranteed correct when the live state
// matches that first Element's own tokens.
//
// WHAT IS CHOSEN, NOT SPEC-CONFIRMED (spec-player-runtime.md Sec7.4b's own
// words: "The literal instruction-level link from a specific Animation
// registry string ... to driving_sm itself was not traced by this or any
// prior pass ... the single biggest open item"): the final hop from a
// resolved Animation string to one of THIS PROJECT's own real,
// brad.rig_pc-compatible .anim_pc clips. See ChosenRealClipForRideAnimation()
// below for the exact bridge table and how each entry was found.
//
// WHAT THIS FILE DOES NOT DO: load any archive or decode any .anim_pc bytes
// itself (that stays the caller's job, via a caller-supplied
// sr3animpose::ClipSource - exactly RealClipRegistry's own existing shape in
// tools/sr3_viewer.cpp, so the SAME real-data loading code paying for the
// walk-demo's locomotion clips now also serves the seated pose); run the
// per-frame rider-parameter writer (out of scope); or touch the condition-
// row field (+0xcc8, already correctly modelled as VehicleInteractionRegistry
// ::ConditionRow() - see vehicle_occupancy.h's own INTEGRATION NOTE,
// deliberately a SEPARATE occupant-keyed map from
// sr3luahost::CharacterState::lifeState, which models the SAME physical
// field for the unrelated 5/6 dead/downed values; this file reads neither,
// it only needs to know WHICH seat an occupant holds).

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3animpose/clip_pose_bridge.h"
#include "sr3animpose/pose_compose.h"
#include "sr3tables_vehicle_world/tables.h"

namespace sr3player {

// The shared 8-seat resolver (spec-tables-vehicle-world.md Sec3.1,
// FUN_00AC1BA0 - already modelled as the two alias arrays
// sr3tables_vehicle_world::kSeatResolverGenericNames/
// kSeatResolverVehicleSpecificNames; this project's own hide-occupant work
// on the sibling, not-yet-merged feature/vehicle-occupant-hide-flag branch
// independently needed the SAME lookup under the name
// FindSeatByResolverIndex() - see this header's own closing banner note).
// Returns the VehicleInteractionSeatInfo whose <Seat> text matches either
// alias list's name at `resolverIndex` (case-insensitive), or nullptr.
const sr3tables_vehicle_world::VehicleInteractionSeatInfo* FindSeatByResolverIndex(
    const sr3tables_vehicle_world::VehicleInteractionInfo& info, int resolverIndex);

// Seat resolver index 0 = driver (kSeatResolverGenericNames[0] = "driver",
// kSeatResolverVehicleSpecificNames[0] = "front driver" - Sec3.1/Sec8.0
// item2's own "condition row +0xcc8 = 8 (seat 0) / 9 (elsewhere)").
inline constexpr int kDriverSeatResolverIndex = 0;

// Step 1-3 of the real lookup (file banner): seat's Ride_Animations name ->
// matching vi_ride.xtbl Vehicle_Interaction_Animation_Set (case-insensitive
// name match, same convention as every other named-table lookup in this
// project) -> MatchElementKey() with an all-"any" key -> resolved Animation
// string. Returns nullopt at any step that fails to resolve (no seat, no
// Ride_Animations text, no matching set, or an empty set) - never fabricates
// (Sec9.4.2/Sec9.7 item5's own "silent miss" convention, reused here).
std::optional<std::string> ResolveRideAnimationString(
    const sr3tables_vehicle_world::VehicleInteractionInfo& info, int seatResolverIndex,
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationSet>& rideSets);

// THE CHOSEN BRIDGE (file banner's "what is chosen, not spec-confirmed").
// Maps a resolved Animation registry string to a REAL .anim_pc clip name
// this project can actually load from the real shipped preload_anim.vpp_pc -
// found this task (2026-10-09) via a scratch probe (same "clean population"
// compatibility rule tools/validate_anim_pose_compose_population.cpp already
// uses: full payload walk, every touched bone index < brad.rig_pc's 68) run
// directly against this machine's own real characters.vpp_pc/
// preload_anim.vpp_pc (packfiles/pc/cache, the same real-data source
// src/tables_vehicle_world.cpp's existing real_*_test.cpp files already use
// via CRREISH_GAME_CACHE_DIR):
//
//   "drive network" -> "auto_ridel.anim_pc" (46 tracks, max bone idx 45,
//     458 time-units / ~15.3s - a long idle LOOP, not a one-shot; real,
//     clean, brad-rig-compatible)
//
// WHY THIS ONE, stated plainly (same honesty bar RealClipRegistry's own
// "plym_ps_run/jog/wlk" picks already set for the walk demo): the real
// preload_anim.vpp_pc has no clip literally named "drive network" or
// anything resolving through driving_sm (expected - Sec7.4b's own point is
// that this exact link was never traced, and preload_anim.vpp_pc is a
// persistent-load subset, not the full per-mission streaming clip set a
// real driving_sm node graph likely draws from). "auto_ridel"/"auto_rider"
// are the ONLY two bare (unsuffixed) members of the real "auto_ride*"/
// "auto_rider*" clip family (every other member is a one-shot variant -
// "_ak_fire_*", "_bm_thrw_*", "_ps_aim*", "_collsn", "_close", ...,
// confirmed by this task's own probe output) - i.e. the family's own two
// base/idle loops. The "auto_" prefix is this archive's own car-family
// naming (parallel to "crsr_" for motorcycle/cruiser and "brute_" for the
// BRUTE, both confirmed present as separate ride-clip families by the same
// probe), matching vi_ride.xtbl's own vehicle-type grouping. The "l"/"r"
// suffix matches a left-hand-drive car's own driver(left)/passenger(right)
// seat-SIDE convention - the exact axis vi_ride.xtbl's "standard driver" vs
// "standard passenger left"/"standard passenger right" set names already
// encode. This is a materially stronger real-naming correlation than the
// walk-demo's own run/jog/walk substitution had (those were cross-FAMILY
// thematic picks; these are the literal base/idle members of the one real
// clip family this engine already groups by vehicle type and seat side) -
// but it is still NOT spec-confirmed to be the literal clip driving_sm
// itself would select; Sec7.4b's own open item stands.
//
// Returns nullopt for any Animation string not in this (deliberately small,
// honestly incomplete) table - the caller then keeps the existing bind-pose
// fallback rather than fabricating a clip name. Only the driver-seat case
// ("drive network") is in scope for this task; a future pass extending
// per-seat passenger poses would add "auto_rider.anim_pc" for the matching
// passenger Animation string(s) the same way (not done here - no passenger
// Ride_Animations was exercised by this task's own single-seat verification
// scope).
std::optional<std::string> ChosenRealClipForRideAnimation(const std::string& animationString);

// The one owner of seated-pose RESOLUTION + APPLICATION for a vehicle
// occupant (this task's own "one clearly scoped module" ask). Ties together
// ResolveRideAnimationString() + ChosenRealClipForRideAnimation() with
// sr3animpose's existing U4 compose pipeline (pose_compose.h/
// clip_pose_bridge.h, UNMODIFIED) to produce one posed skeleton - the exact
// bridge tools/sr3_viewer.cpp's playtest command now calls instead of
// re-uploading the character's bind-pose skin while seated.
class SeatedRidePoseApplier {
public:
    // `clips` must already have whichever ChosenRealClipForRideAnimation()
    // names the caller could load registered under that exact name via
    // whatever `recordByClipName` maps to (mirrors RealClipRegistry's own
    // load()/lookup() split in tools/sr3_viewer.cpp - this class does no
    // archive I/O itself, see file banner).
    struct Result {
        bool resolved = false;                // false -> caller should keep the bind-pose fallback
        std::string animationString;          // the resolved vi_ride.xtbl Animation text (diagnostic/logging)
        std::string chosenClipName;           // the CHOSEN real .anim_pc name actually used (diagnostic/logging)
        sr3animpose::PoseComposeResult pose;   // valid only when resolved
    };

    // `loopPhaseSeconds`: the caller's own running clock for this seat (a
    // free-running seconds counter is enough - the chosen clips are long
    // idle loops, Sec7.4b's steady-ride scope has no real per-frame
    // controller-parameter blend to reproduce, see file banner). Wrapped
    // against the resolved clip's own real duration internally.
    Result ComposeSeatedPose(const sr3tables_vehicle_world::VehicleInteractionInfo& info, int seatResolverIndex,
                              const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationSet>& rideSets,
                              const std::unordered_map<std::string, int32_t>& recordByClipName,
                              sr3animpose::ClipSource& clips, double loopPhaseSeconds, size_t boneCount) const;
};

// MERGE NOTE (recorded honestly, not fixed here - this task's own "no
// invented fixes" convention): the sibling, not-yet-merged
// feature/vehicle-occupant-hide-flag branch (e28ccde, main-adjacent but NOT
// an ancestor of this branch's own base at the time this file was written)
// independently added a same-shaped FindSeatByResolverIndex()/
// ResolveDriverHideOccupant() pair to sr3tables_vehicle_world for an
// unrelated purpose (the real per-seat Hide Occupant flag). Both branches
// will very likely define the SAME seat-by-resolver-index lookup under
// slightly different homes (that branch's is a sr3tables_vehicle_world free
// function; this file's is sr3player::FindSeatByResolverIndex(), kept here
// instead to avoid taking an unreviewed link dependency on the other
// branch's own in-flight work) - a straightforward de-duplication for
// whoever merges both, not a conflict in behaviour (both implement the same
// confirmed Sec3.1 alias-list lookup).

} // namespace sr3player

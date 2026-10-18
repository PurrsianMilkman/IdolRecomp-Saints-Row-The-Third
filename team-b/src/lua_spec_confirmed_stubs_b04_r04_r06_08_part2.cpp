// lua_spec_confirmed_stubs_b04_r04_r06_08_part2.cpp
//
// Second half of lua_spec_confirmed_stubs_b04_r04_r06_08.cpp's stub functions, moved
// here unchanged to keep each file under ~1,500 lines (Team C refactor, 2026-10-17). The
// names/registration pair stays in that file; see its header comment for scope, sources
// and policy.

#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace stubs_b04 {

// Shared catalog-outfit index resolution for pcu_wear_store_outfit/
// pcu_purchase_outfit below (Sec40.5/Sec40.6) - see EngineState::
// PcuCatalogOutfits' own doc comment (engine_state.h) for the full "two
// independent counting rules, deliberately not unified" reasoning and the
// "excluded bit read as bit 0" interpretive note. Scans test-populated
// list positions 0.. in order (this project's own minimal stand-in for
// "the end of the real catalog list" - a test marks the list's real
// extent simply by not populating past it, the SAME idiom
// readCharacterNameArray above already uses for "the first nil ends the
// array"), counting only positions whose raw flag word satisfies the
// requested rule, and returns the list POSITION of the `wantIndex`-th
// (0-based) qualifying node, or -1 if fewer than `wantIndex`+1 qualify.
int resolvePcuCatalogOutfitIndex(EngineState::PcuCatalogOutfits& catalog, int64_t wantIndex,
                                  bool countByAnyBitExcludingBit0) {
    int64_t seen = 0;
    for (int pos = 0; pos < 100000; ++pos) {
        std::string key = std::to_string(pos);
        if (!catalog.flagsByIndex.known(key)) break;
        uint32_t flags = catalog.flagsByIndex.get(key);
        bool qualifies = countByAnyBitExcludingBit0 ? ((flags & ~0x1u) != 0) : ((flags & 0x1u) != 0);
        if (qualifies) {
            if (seen == wantIndex) return pos;
            ++seen;
        }
    }
    return -1;
}

// pcu_wear_store_outfit (Sec40.5/Sec40.6) - MANDATED "implement each
// function's own rule separately": numbers catalog-outfit nodes by
// testing "any non-zero flag bit, EXCLUDING one specific bit" (read here
// as bit 0 - see resolvePcuCatalogOutfitIndex's own doc comment). 1
// number (catalog-outfit index - CONFIRMED kind of index for this entry,
// Sec40.5/Sec40.6; exact further argument count beyond this is OPEN, see
// this batch's own header comment on the condensed write-up). No return
// (an apply-style action, same shape as every other setter in this
// document). The actual apply step (the shared outfit-apply routine
// 0x00825cf0, which "mutates its source outfit record's own piece
// ordering in place") is OPEN - not modeled; this host instead records
// the resolved list position as a test-observable result
// (EngineState::pcuInventory().lastWornCatalogPosition), -1 if the index
// doesn't resolve to any qualifying node (OPEN: not confirmed whether the
// real engine has its own distinct not-found behavior here).
int stub_pcu_wear_store_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_wear_store_outfit", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    int pos = resolvePcuCatalogOutfitIndex(st->pcuCatalogOutfits(), idx, /*countByAnyBitExcludingBit0=*/true);
    st->pcuInventory().lastWornCatalogPosition = pos; // CHOSEN test-observable result, see comment above
    return 0;
}

// pcu_purchase_outfit (Sec40.5/Sec40.6) - MANDATED faithful quirk AND
// MANDATED "implement each function's own rule separately": numbers the
// SAME catalog-outfit list by testing "bit 0 only" - a DIFFERENT rule
// from pcu_wear_store_outfit above, deliberately not unified (the same
// raw index can therefore resolve to a DIFFERENT list position here than
// it does there - a CONFIRMED real indexing inconsistency, directly
// exercised by this pair's own tests). 1 number (catalog-outfit index) +
// an implicit price (CHOSEN 2nd-argument shape - the condensed write-up
// gives the counting rule and the charge-regardless quirk precisely but
// not this function's own full argument list, see this batch's own
// header comment; a 2nd numeric "price" argument mirrors this document's
// own established sibling shape, e.g. vcust_purchase_wheels's "1 number
// (price)", Sec38.5). CONFIRMED real logic defect (the mandated quirk):
// "ignores the inventory-append function's 'full' return value - a
// player who is already at capacity (128 outfits) is charged the full
// price and receives nothing silently." Reproduced here exactly: the
// charge always happens; the saved-outfit append is skipped (and logged)
// once at capacity, never rejected up front. The additional "drops
// individual outfit pieces past the 2048-item cap one at a time" nuance
// needs an outfit-to-pieces association this project does not have (no
// real item/outfit catalog beyond the raw flag words above) - not
// modeled, a stated gap, not invented.
int stub_pcu_purchase_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_purchase_outfit", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    double price = lua_gettop(L) >= 2 ? lua_tonumber(L, 2) : 0.0; // CHOSEN 2nd-arg price, see comment above
    EngineState* st = upState(L);
    int pos = resolvePcuCatalogOutfitIndex(st->pcuCatalogOutfits(), idx, /*countByAnyBitExcludingBit0=*/false);
    auto& inv = st->pcuInventory();
    inv.lastPurchasedOutfitCatalogPosition = pos; // resolved per this function's OWN rule - see its own doc comment
                                                   // (PcuInventory, engine_state.h) for why this is a separate field
                                                   // from pcu_wear_store_outfit's lastWornCatalogPosition above
    inv.cashBalance -= price; // CONFIRMED: charged regardless of append outcome
    if (inv.savedOutfitCount >= EngineState::PcuInventory::kSavedOutfitCapacity) {
        ++inv.purchaseOutfitChargedWhileFullCount; // CONFIRMED quirk: charged even though the append is refused
        logCall(L, upLog(L), "pcu_purchase_outfit:CHARGED_WHILE_FULL", upStateTag(L));
    } else {
        ++inv.savedOutfitCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// pause_map_tutorial_mode (Sec32.1, 0x00a594e0). 1 boolean, no nil-gate
// (absent/nil -> false, lua_toboolean's own real semantics). No return.
// CONFIRMED: writes the pause-map tutorial-mode flag 0x0229a318 (the SAME
// global Sec31.1 table item 5's `pause_map_is_tutorial_mode` reads).
// ---------------------------------------------------------------------
int stub_pause_map_tutorial_mode(lua_State* L) {
    logCall(L, upLog(L), "pause_map_tutorial_mode", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->pauseMapTutorialMode().set(v);
    return 0;
}

// ---------------------------------------------------------------------
// set_time_of_day (Sec32.1, 0x00a5e370). 2 numbers, no nil-gates (hour,
// minute; truncated via 0x00ea2596). No return. See
// EngineState::setTimeOfDay's own doc comment for the CONFIRMED forward-
// only delta arithmetic.
// ---------------------------------------------------------------------
int stub_set_time_of_day(lua_State* L) {
    logCall(L, upLog(L), "set_time_of_day", upStateTag(L));
    int64_t newHour = truncateEa2596(lua_tonumber(L, 1));
    int64_t newMinute = truncateEa2596(lua_tonumber(L, 2));
    upState(L)->setTimeOfDay(newHour, newMinute); // OPEN (current hour/minute) propagates
    return 0;
}

// ---------------------------------------------------------------------
// satellite_weapon_mode_exit (Sec32.1, 0x00a5df20). 1 optional number,
// nil-gated, default 3 - a player-selector bitmask (bit 0x1 = local
// player, bit 0x2 = remote co-op player via 0x009df3d0). No return.
// **The 0x009df3d0 correction applies directly here**: bit 0x2 only
// selects a player when playerRig().coopPlayerPresent is true (CONFIRMED,
// Sec14.31/Sec32.8 - "the first live-roster entry that is NOT the local
// player... returning 0 only when every roster entry equals the local
// player", not a "matching" lookup that could wrongly resolve to the
// local player itself in single player). With no co-op session, bit 0x2
// is therefore a correct no-op, never a fallback onto the local player.
// Local player (bit 0x1) is always resolvable (0x009da4e0, a zero-
// argument getter - no name resolution involved).
// ---------------------------------------------------------------------
int stub_satellite_weapon_mode_exit(lua_State* L) {
    logCall(L, upLog(L), "satellite_weapon_mode_exit", upStateTag(L));
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    EngineState* st = upState(L);
    if (mask & 1) st->satelliteWeaponExit(/*remote=*/false);
    if ((mask & 2) && st->playerRig().coopPlayerPresent.get()) { // CONFIRMED false by default (applySpecInitialState item 13) - no-op in single player per the 0x009df3d0 correction above
        st->satelliteWeaponExit(/*remote=*/true);
    }
    return 0;
}

// pcu_purchase_slot (Sec40.5) - MANDATED faithful quirk. 1 number (a slot
// id, 0-23 - CONFIRMED kind of index for "several" PCU entries including
// this one, Sec40.5) + an implicit price (CHOSEN 2nd-argument shape, same
// reasoning as pcu_purchase_outfit above). CONFIRMED real logic defect
// (the mandated quirk): "ignores the inventory-append function's 'full'
// return value - a player who is already at capacity (2048 items) is
// charged the full price and receives nothing silently." The function's
// OWN separate crash-shaped defects (an unconditional dereference of a
// global pointer two lines after its own null check, an unchecked
// variant-lookup result, an unchecked store-entry variant pointer, a
// 3-slot stack-array colour copy with no clamp of its own) all need a
// store-entry/variant-lookup model this project does not have in scope -
// not modeled, a stated gap, not invented (none of them are this task's
// own mandated guards for THIS function - only the charge-while-full
// quirk is).
int stub_pcu_purchase_slot(lua_State* L) {
    logCall(L, upLog(L), "pcu_purchase_slot", upStateTag(L));
    int64_t slotId = truncateEa2596(lua_tonumber(L, 1)); // CONFIRMED: a slot id (0-23), Sec40.5
    double price = lua_gettop(L) >= 2 ? lua_tonumber(L, 2) : 0.0; // CHOSEN 2nd-arg price, see comment above
    (void)slotId; // not otherwise modeled - no real per-slot item/variant table in scope
    EngineState* st = upState(L);
    auto& inv = st->pcuInventory();
    inv.cashBalance -= price; // CONFIRMED: no affordability check (same shape as store_weapon_purchase_ammo/vcust_purchase_wheels)
    if (inv.ownedItemCount >= EngineState::PcuInventory::kOwnedItemCapacity) {
        ++inv.purchaseSlotChargedWhileFullCount; // CONFIRMED quirk: charged even though the append is refused
        logCall(L, upLog(L), "pcu_purchase_slot:CHARGED_WHILE_FULL", upStateTag(L));
    } else {
        ++inv.ownedItemCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// set_seatbelt_flag / set_trailing_aim_flag / set_never_turn_on_player
// (Sec32.2). All three: 1 mandatory character name (reuses
// objectResolves(), this project's own shared-resolver simplification), 1
// optional boolean, nil-gated, default true. No return. CONFIRMED
// record-and-replicate shape, collapsed to "always apply the write, call
// the no-op replicate stand-in" per this project's own established
// set_ignore_ai_flag precedent (replicateStateChange's own doc comment).
// ---------------------------------------------------------------------
int stub_set_seatbelt_flag(lua_State* L) {
    logCall(L, upLog(L), "set_seatbelt_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->seatbeltForceFlag()[name] = value;
        EngineState::replicateStateChange("set_seatbelt_flag", name);
    }
    return 0;
}

int stub_set_trailing_aim_flag(lua_State* L) {
    logCall(L, upLog(L), "set_trailing_aim_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->trailingAimForceFlag()[name] = value;
        EngineState::replicateStateChange("set_trailing_aim_flag", name);
    }
    return 0;
}

int stub_set_never_turn_on_player(lua_State* L) {
    logCall(L, upLog(L), "set_never_turn_on_player", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->neverTurnOnPlayerFlag()[name] = value;
        EngineState::replicateStateChange("set_never_turn_on_player", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_revive (Sec32.3, 0x00a599b0). 1 string ("#PLAYER#" accepted), no
// nil-gate, reuses objectResolves(). No return. CONFIRMED: only acts if
// the character's life-state field (+0xcc8) reads 6 ("downed" - the same
// test Sec7.13 `human_is_downed` uses); "always broadcasts first"
// regardless of authority.
// ---------------------------------------------------------------------
int stub_player_revive(lua_State* L) {
    logCall(L, upLog(L), "player_revive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.lifeState.get() == 6) { // OPEN until set; CONFIRMED test value
            ++c.reviveCount;
            EngineState::replicateStateChange("player_revive", name); // CONFIRMED: always broadcasts first
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_warp_to_shore_disable (Sec32.3, 0x00a59a90). 1 string
// ("#PLAYER#" accepted), reuses objectResolves(). No return. CONFIRMED:
// always writes true (hardcoded, not a Lua argument) to +0x28ad bit 0x2.
// Passed WITHOUT a null check to the real setter, but that setter itself
// returns at once on a null receiver - a harmless real no-op, matching
// this project's own OPEN-refusal-on-unresolved-name convention (not a
// crash).
// ---------------------------------------------------------------------
int stub_player_warp_to_shore_disable(lua_State* L) {
    logCall(L, upLog(L), "player_warp_to_shore_disable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->warpToShoreDisabled()[name] = true; // CONFIRMED: always true, hardcoded
        EngineState::replicateStateChange("player_warp_to_shore_disable", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// skydive_setup_tank_bailout (Sec32.3, 0x00a5e510). 1 number (stage), no
// nil-gate, truncated. No return. A mission-18 set-piece helper: no name
// resolve at all (both vehicles are hardcoded literals). CONFIRMED:
// always broadcasts first regardless of stage or vehicle existence; stage
// 1 arms, stage 2 starts the named anim; any other stage is a local no-op
// (record still sent).
// ---------------------------------------------------------------------
int stub_skydive_setup_tank_bailout(lua_State* L) {
    logCall(L, upLog(L), "skydive_setup_tank_bailout", upStateTag(L));
    int64_t stage = truncateEa2596(lua_tonumber(L, 1));
    auto& s = upState(L)->skydiveTankBailout();
    ++s.broadcastCount; // CONFIRMED: always, every stage
    if (stage == 1) ++s.armedCount;
    else if (stage == 2) ++s.animStartedCount;
    return 0;
}

// ---------------------------------------------------------------------
// qte_human_is_used (Sec32.3, 0x00a5b180). 1 string, reuses
// objectResolves(). Return: 1 boolean, always. CONFIRMED: an unresolved
// character is false (same "unresolved -> false" convention as
// get_char_vehicle_is_in_air above); else see
// EngineState::qteHumanIsUsed's own doc comment.
// ---------------------------------------------------------------------
int stub_qte_human_is_used(lua_State* L) {
    logCall(L, upLog(L), "qte_human_is_used", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool used = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        used = st->qteHumanIsUsed(name);
    }
    lua_pushboolean(L, used ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// audio_any_conversation_playing (Sec33.4, 0x00a3c640). No arguments.
// Returns 1 boolean: true if any session member currently has a valid,
// non-0xff slot on the global "mission_conv" per-session channel
// (CONFIRMED - this settles this document's own previously-OPEN identity
// question for that channel). This project tracks no per-member array
// (see EngineState::conversationChannelActive's own doc comment) -
// collapsed to one flag per channel name, OPEN until a test sets it.
// ---------------------------------------------------------------------
int stub_audio_any_conversation_playing(lua_State* L) {
    logCall(L, upLog(L), "audio_any_conversation_playing", upStateTag(L));
    bool active = upState(L)->missionConvActive(); // e6691f1: false (0xff) until played
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// party_add_do (Sec32.4, 0x00a5af70). Arg 1 leader name; arg 2 table of
// follower names (length via 0x0083dff0); arg 3 boolean, no nil-gate; arg
// 4 optional boolean, nil-gated, default false. No return. CONFIRMED
// real stack-overrun hazard (6th-or-later follower) is flagged, not
// reproduced (see PartyState::overflowDetectedCount's own doc comment) -
// processing is clamped to the real 5-slot bound. A failed leader
// resolution now triggers the SAME closest-player redirect
// (resolveClosestPlayerSentinel(), lua_spec_confirmed_stubs_common.h) a
// failed leader resolve is treated as a safe no-op here,
// ai_add_enemy_target/Sec3.9 uses (2026-10-08, backlog cross-reference
// pass: this entry's own text additionally names the literal "#PLAYER#"
// as an equally-valid trigger, "picks whichever co-op player is nearest
// THAT SPECIFIC FOLLOWER" - in this project's single-player-only scope
// every such per-follower retry yields the identical result regardless of
// which follower is being processed, since no second candidate ever
// exists, so the per-follower loop collapses to one retry here, same
// reasoning as that helper's own doc comment). arg3's own capacity/force-
// override gate and arg4's leader-kind-bit AI/registration setup have no
// real data this project can reproduce and are also not modeled (read for
// correct stack shape only).
// ---------------------------------------------------------------------
int stub_party_add_do(lua_State* L) {
    logCall(L, upLog(L), "party_add_do", upStateTag(L));
    std::string leader = argString(L, 1);
    int64_t followerCount = tableLength(L, 2);
    (void)(lua_toboolean(L, 3) != 0);          // arg3: no nil-gate; capacity/force-override gate not modeled (OPEN internals)
    (void)optionalBoolDefault(L, 4, false);    // arg4: leader-kind-bit AI/registration setup not modeled (OPEN internals)
    EngineState* st = upState(L);
    if (!st->objectResolves().get(leader)) { // OPEN until set
        // CONFIRMED (Sec32.4): a failed leader resolve retries via the
        // closest-player mechanism when the leader name is
        // "#CLOSEST_PLAYER#" OR the literal "#PLAYER#" - both explicitly
        // named by this entry's own text as triggers for this fallback.
        bool triggersClosestPlayerFallback =
            caseInsensitiveEquals(leader, "#CLOSEST_PLAYER#") || caseInsensitiveEquals(leader, "#PLAYER#");
        if (!triggersClosestPlayerFallback) return 0; // any other unresolved name: CONFIRMED safe no-op
        leader = "#PLAYER1#"; // CONFIRMED redirect target - see resolveClosestPlayerSentinel()'s own doc comment
        if (!st->objectResolves().get(leader)) return 0; // OPEN until set - the retry can still fail
    }
    int64_t processed = followerCount < 0 ? 0 : std::min<int64_t>(followerCount, 5);
    if (followerCount > 5) ++st->party().overflowDetectedCount; // CONFIRMED real hazard, not reproduced
    for (int64_t i = 0; i < processed; ++i) {
        lua_rawgeti(L, 2, static_cast<int>(i + 1));
        std::string follower = argString(L, -1);
        lua_pop(L, 1);
        if (!follower.empty() && st->objectResolves().get(follower)) { // OPEN until set
            st->party().membersByLeader[leader].push_back(follower);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_is_in_party (Sec32.4, 0x00a562a0). 1 string (spec's own text: "1
// string (#FOLLOWER#/#FOLLOWER1#-#FOLLOWER3# accepted), via an inlined
// 0x00a28150-equivalent chain" - wired 2026-10-08, follower-sentinel-
// resolver pass, via resolveFollowerAwareObject()). Return: 1 boolean.
// CONFIRMED: false for an unresolved/null character; else HYPOTHESIS "has
// any party leader" (not specific to the local player's own party) - this
// project checks membership across every leader's own follower list. A
// #FOLLOWER...# literal that resolves (resolveFollowerAwareObject()
// returns true) is, by construction, already a member of SOME leader's
// list (it came from reading one) - so the membership loop below still
// runs, unchanged, over the RESOLVED name rather than the raw literal.
// ---------------------------------------------------------------------
int stub_npc_is_in_party(lua_State* L) {
    logCall(L, upLog(L), "npc_is_in_party", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    bool inParty = false;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set - generic/#PLAYER#-then-#FOLLOWER# chain (0x00a28150); may throw OpenStateError if name is a #FOLLOWER...# literal with no index convention set
        for (const auto& kv : st->party().membersByLeader) {
            const auto& followers = kv.second;
            if (std::find(followers.begin(), followers.end(), resolvedName) != followers.end()) {
                inParty = true;
                break;
            }
        }
    }
    lua_pushboolean(L, inParty ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// boss_battle_matt_begin (Sec33.5, 0x00a407c0). 1 optional boolean,
// default true. No return, no replication (CONFIRMED). Resets the
// BossBattleMatt cluster's cheat-slot/id/numeric fields to their neutral
// values (this project's own modelled subset - see BossBattleMatt's own
// doc comment, Sec28.24/Sec28.25; the real "four sub-objects" this spec
// also mentions have no modelled fields here, not guessed past; the
// retry counter 0x012ec720 is NOT confirmed reset by this specific entry
// - distinct from 0x012ec71c, the static limit - left untouched), then
// arms the first deadline (5000ms true / 0 false) and marks it active.
// CONFIRMED: also looks up the character literally named "Matt" and, if
// found and alive, sets bit 0x4000 of its dword +0xec (meaning OPEN).
// Every branch-determining read happens before any write (same "no
// partial update on an OPEN refusal" convention as set_ignore_ai_flag).
// ---------------------------------------------------------------------
int stub_boss_battle_matt_begin(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_begin", upStateTag(L));
    bool value = true; // CONFIRMED default true
    if (lua_gettop(L) >= 1 && lua_type(L, 1) != LUA_TNIL) value = lua_toboolean(L, 1) != 0;

    EngineState* st = upState(L);
    bool mattResolved = st->objectResolves().get("Matt"); // OPEN until set - read before any write
    bool mattAlive = mattResolved && !st->getOrCreateCharacter("Matt").isDeadHighConfidence;

    auto& matt = st->bossBattleMatt();
    matt.cheatSlot.set(-1);
    matt.lastId = -1;
    matt.lastN2 = 0.0;
    matt.lastN3 = 0.0;
    matt.deadlineMs = value ? 5000 : 0; // CONFIRMED
    matt.active = true; // CONFIRMED

    if (mattAlive) {
        st->getOrCreateCharacter("Matt").flagsEc.setBits(0x4000, 0x4000); // CONFIRMED structure, meaning OPEN
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_go_idle (Sec32.4, 0x00a554b0). 1 string, reuses objectResolves()
// (no "#PLAYER#" step, per spec - this project's shared resolver is
// name-agnostic, so no code-level distinction is needed). No return.
// CONFIRMED: resets the AI-orders sub-object (not individually modeled,
// only counted) and sets the action-override state to 0x19 if in a
// vehicle (stateEnum == 3), else 0 - "the exact rule
// set_ignore_ai_flag already applies" (Sec3.4, reused directly here).
// ---------------------------------------------------------------------
int stub_npc_go_idle(lua_State* L) {
    logCall(L, upLog(L), "npc_go_idle", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        ++c.aiOrdersResetCount;
        c.actionOverrideId.set(c.stateEnum.get() == 3 ? 0x19 : 0); // OPEN (stateEnum) until set; CONFIRMED rule
    }
    return 0;
}

// ---------------------------------------------------------------------
// auto_pickup_disable (Sec33.5, 0x00a3c8a0). No arguments, no return.
// CONFIRMED: writes a named, session-synchronized variable
// "allow_weapon_auto_pickup" off (session-wide, not per-player).
// ---------------------------------------------------------------------
int stub_auto_pickup_disable(lua_State* L) {
    logCall(L, upLog(L), "auto_pickup_disable", upStateTag(L));
    upState(L)->allowWeaponAutoPickup() = false;
    return 0;
}

// ---------------------------------------------------------------------
// waiting_for_player_dialog (Sec33.5, 0x00a68ea0). 1 boolean, no
// nil-gate. No return. CONFIRMED reference-counted show/hide (nested
// shows need an equal number of hides; a hide at counter 0 does
// nothing); showing increments the counter even when suppressed (this
// project has no "suppressing game-state check" to evaluate - stated
// simplification, see WaitingForPlayerDialog's own doc comment). On the
// final hide, host+coop-active broadcasts an opcode-0x23 record; the
// host also sends the matching show broadcast when the display actually
// appears (same gating applied by symmetry, HIGH CONFIDENCE). Open
// hazard DELIBERATELY reproduced: neither path ever clears "currently
// shown" here (CONFIRMED - only an untraced third writer, out of scope,
// does), so after one real display this stays true for the rest of the
// run.
// ---------------------------------------------------------------------
int stub_waiting_for_player_dialog(lua_State* L) {
    logCall(L, upLog(L), "waiting_for_player_dialog", upStateTag(L));
    bool show = lua_toboolean(L, 1) != 0; // CONFIRMED: no nil-gate
    EngineState* st = upState(L);
    auto& d = st->waitingForPlayerDialog();
    if (show) {
        ++d.refCount; // CONFIRMED: increments even when suppressed
        if (!d.currentlyShown) {
            d.currentlyShown = true;
            if (st->coopIsActive() && st->coopLocalIsHost()) ++d.showBroadcastCount;
        }
        // else: CONFIRMED open hazard - already shown, display branch silently skipped (deliberately reproduced)
    } else if (d.refCount > 0) { // CONFIRMED: a hide at counter 0 does nothing
        --d.refCount;
        if (d.refCount == 0 && st->coopIsActive() && st->coopLocalIsHost()) {
            ++d.hideBroadcastCount; // CONFIRMED: host, while co-op is active
        }
        // CONFIRMED open hazard: "currently shown" is NOT cleared here either.
    }
    return 0;
}

// ---------------------------------------------------------------------
// object_destroy (Sec32.5, 0x00a57630). 1 string; non-string/nil -> no-
// op (CONFIRMED). No return. Reuses objectResolves(); the liveness check,
// capability-predicate gate and per-kind destroy virtual are OPEN/not
// modeled - only that a destroy was processed is recorded.
// ---------------------------------------------------------------------
int stub_object_destroy(lua_State* L) {
    logCall(L, upLog(L), "object_destroy", upStateTag(L));
    if (lua_type(L, 1) != LUA_TSTRING) return 0; // CONFIRMED: non-string/nil -> no-op
    std::string name = argString(L, 1);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        upState(L)->destroyedObjects().insert(name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// object_indicator_remove_do (Sec32.5, 0x00a59190). Arg 1 string; arg 2
// optional number, nil-gated, default 3 (bit 0x1 = apply locally, bit 0x2
// = send a record). No return. CONFIRMED: bit 0x1 removes EVERY indicator
// attached to the object, not just one (reuses
// CharacterState::objectIndicators, Sec10.6); bit 0x2 opens an opcode-
// 0x40 sub-tag-4 record (counted only, no networking layer modeled).
// ---------------------------------------------------------------------
int stub_object_indicator_remove_do(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_remove_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (mask & 1) c.objectIndicators.clear(); // CONFIRMED: loop-until-none removal
        if (mask & 2) ++st->indicatorRemoveRecordCount();
    }
    return 0;
}

// ---------------------------------------------------------------------
// minimap_icon_remove_do (Sec32.5, 0x00a54430). Arg 1 string; arg 2
// optional number, nil-gated, default 3 (same bitmask convention). No
// return. CONFIRMED real crash hazard (an unranged 4-entry function-
// pointer table indexed directly by the mask byte) is flagged, not
// reproduced (Sec32.8) - this project clears every recorded minimap icon
// on the resolved object (reuses CharacterState::minimapIcons, Sec10.5)
// regardless of which real per-kind dispatch path the mask would select.
// ---------------------------------------------------------------------
int stub_minimap_icon_remove_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_remove_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    (void)mask; // the real per-kind dispatch/unranged table access (Sec32.5/Sec32.8) is not modeled
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        c.minimapIcons.clear();
    }
    return 0;
}

// ---------------------------------------------------------------------
// shop_enable_nearest (Sec32.5, 0x00a5f650). Arg 1 optional string
// (location name, nil-gated default ""); arg 2 optional boolean, nil-
// gated, default true (enable). No return. CONFIRMED rule: the FIRST shop
// within 15 units is taken immediately, else the closest within 75 units.
// This project has no spatial/world-position system - see
// EngineState::Shop's own doc comment for the test-fixture stand-in (arg
// 1's own role in seeding the search origin is not modeled; read only for
// correct stack shape).
// ---------------------------------------------------------------------
int stub_shop_enable_nearest(lua_State* L) {
    logCall(L, upLog(L), "shop_enable_nearest", upStateTag(L));
    (void)optionalStringDefault(L, 1, ""); // location name - not modeled, see this function's own top comment
    bool enable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    auto& shops = st->shops();
    int picked = -1;
    for (size_t i = 0; i < shops.size(); ++i) {
        if (shops[i].withinFifteen) { picked = static_cast<int>(i); break; }
    }
    if (picked < 0) {
        for (size_t i = 0; i < shops.size(); ++i) {
            if (shops[i].withinSeventyFive) { picked = static_cast<int>(i); break; } // "closest" tie-breaking beyond the threshold rule itself is not modeled
        }
    }
    if (picked >= 0) {
        shops[static_cast<size_t>(picked)].disabled = !enable;
        ++st->shopEnableRecordCount();
    }
    return 0;
}

// ---------------------------------------------------------------------
// item_show (Sec32.6, 0x00a51d50). 1 string; nil -> no-op (CONFIRMED).
// No return. CONFIRMED record-and-replicate shape, collapsed per this
// project's own established convention (replicateStateChange): always
// clears the "hidden" bit (+0x3b bit 0x1) on a resolved item.
// ---------------------------------------------------------------------
int stub_item_show(lua_State* L) {
    logCall(L, upLog(L), "item_show", upStateTag(L));
    if (lua_type(L, 1) == LUA_TNIL) return 0; // CONFIRMED: nil -> no-op
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->itemHidden()[name] = false; // CONFIRMED: item_show always shows (clears "hidden")
        EngineState::replicateStateChange("item_show", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// item_anim_play (Sec32.6, 0x00a51ad0). Arg 1 item name; arg 2 animation
// name (nil -> id -1); arg 3 optional boolean, nil-gated, default false;
// arg 4 optional string, nil-gated, default none. No return, no network
// record (local-only, CONFIRMED). CONFIRMED branch: if arg 4 resolves, a
// two-state blend-transition (0.3s) and arg 3 is ignored; else if arg 2
// resolves, a single-state start with flag 0x40 (arg3 true) or 0x10
// (arg3 false); neither resolving is a no-op.
// ---------------------------------------------------------------------
int stub_item_anim_play(lua_State* L) {
    logCall(L, upLog(L), "item_anim_play", upStateTag(L));
    std::string item = argString(L, 1);
    std::string animArg2 = (lua_type(L, 2) == LUA_TSTRING) ? argString(L, 2) : std::string();
    bool arg3 = optionalBoolDefault(L, 3, false);
    std::string animArg4 = optionalStringDefault(L, 4, "");
    EngineState* st = upState(L);
    if (st->objectResolves().get(item)) { // OPEN until set
        auto& rec = st->itemAnimState()[item];
        if (!animArg4.empty()) {
            rec.lastBlendTransitionAnim = animArg4; // arg3 ignored on this path, CONFIRMED
            ++rec.blendTransitionCount;
        } else if (!animArg2.empty()) {
            rec.lastSingleStateAnim = animArg2;
            rec.lastFlag = arg3 ? 0x40 : 0x10; // CONFIRMED branch values; HYPOTHESIS meaning
            ++rec.singleStateStartCount;
        }
        // neither resolving: no-op (CONFIRMED)
    }
    return 0;
}

// ---------------------------------------------------------------------
// radio_set_station (Sec32.7, 0x00a5c1c0). Arg 1 vehicle-or-character
// name (reuses objectResolves() - the vehicle resolver redirects through
// a character's cached vehicle); arg 2 number (station), no nil-gate,
// truncated to a signed byte. No return. CONFIRMED: the opcode record is
// ALWAYS sent once the vehicle resolves, even when the local 1-based
// apply (station <= 0 or above count, or itself marked refused, is
// rejected - never switches the radio off).
// ---------------------------------------------------------------------
int stub_radio_set_station(lua_State* L) {
    logCall(L, upLog(L), "radio_set_station", upStateTag(L));
    std::string target = argString(L, 1);
    int64_t stationWide = truncateEa2596(lua_tonumber(L, 2));
    int8_t station = static_cast<int8_t>(stationWide); // CONFIRMED: truncated to a signed byte
    EngineState* st = upState(L);
    if (st->objectResolves().get(target)) { // OPEN until set
        auto& radio = st->vehicleRadio(target);
        ++radio.recordSentCount; // CONFIRMED: always sent once the vehicle resolves
        if (radio.hasRadio.get() && station >= 1 && station <= radio.stationCount.get() && // OPEN until set
            !radio.stationRefused.get(std::to_string(static_cast<int>(station)))) {        // OPEN until set
            radio.currentStation = station;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// helicopter_shoot_vehicle (Sec32.7, 0x00a4d790). Arg 1 helicopter name;
// arg 2 target name (vehicle resolver); arg 3 optional boolean, nil-
// gated, default true (apply spread); arg 4 optional number, nil-gated,
// default 1.0 (spread radius); arg 5 optional boolean, nil-gated, default
// true. Return: 1 boolean - CONFIRMED false if either name fails to
// resolve or the helicopter fails its AI-drive-state gate; otherwise the
// shared dispatcher's own result, which is itself OPEN (its return value
// is invisible in the decompiled view, per spec) and refused rather than
// fabricated.
// ---------------------------------------------------------------------
int stub_helicopter_shoot_vehicle(lua_State* L) {
    logCall(L, upLog(L), "helicopter_shoot_vehicle", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string target = argString(L, 2);
    (void)optionalBoolDefault(L, 3, true);       // apply spread - not modeled (spread offset has no Lua-visible return effect)
    (void)optionalNumberDefault(L, 4, 1.0);      // spread radius - not modeled
    (void)optionalBoolDefault(L, 5, true);       // arg 5 - OPEN role, not modeled
    EngineState* st = upState(L);
    if (!st->objectResolves().get(heli) || !st->objectResolves().get(target)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!st->helicopter(heli).aiDriveStateOk.get()) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, st->helicopterFireDispatcherResult().get() ? 1 : 0); // OPEN until set - the real dispatcher's own return value is not visible in the decompiled view (Sec32.7)
    return 1;
}

// ---------------------------------------------------------------------
// Batch 2026-10-02 (spec-lua-api-behaviour.md Sec35, the `teleport_coop`
// investigation): teleport_check_done / turn_to_check_done /
// move_to_check_done / vehicle_pathfind_check_done. The real, documented
// reason this batch exists: 3 real missions (dlc2_m01, m13, m19) sit
// suspended forever inside `teleport_coop` (game_lib.lua:2956, a script
// helper - NOT a native), because this project previously left
// teleport_check_done an unregistered generic stub (logged-nil) and a
// script's own `repeat thread_yield() until teleport_check_done(...)`
// idiom can never be satisfied by nil (host.h's own top comment already
// flags this exact real idiom). All 4 share one engine module/convention
// (Sec35.2) - see EngineState's own "teleport_check_done/..." section
// (engine_state.h) for the shared pool's full doc comment; only the
// per-function wrapper shape differs below.
// ---------------------------------------------------------------------

// teleport_check_done (Sec15.17, re-derived 2026-10-02 from the raw
// instruction stream, correcting an earlier inverted decompile-based
// reading). Arguments: 1 mandatory string - an object/character name
// (Sec15.17 itself resolves this via 0x00a281a0; this project has no real
// handle resolver - engine_state.h's own top note - so the raw name
// string IS the lookup key, same convention as CharacterState). Return: 1
// boolean, always (CONFIRMED). Body (CONFIRMED): the name and the literal
// kind 4 (teleport, Sec35.2) are looked up in the shared scripted-request
// pool; the pushed boolean is `status != 0` - true for "done" or "no
// matching request," false only while a request this host is actually
// tracking for this name is genuinely pending.
int stub_teleport_check_done(lua_State* L) {
    logCall(L, upLog(L), "teleport_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindTeleport);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// turn_to_check_done (0x00a61dd0; Sec35.2). This specific wrapper's own
// argument/return shape was NOT independently re-read this pass beyond
// "confirmed to use the SAME shared status function, kind 0" (Sec35.2's
// own closing sentence) - so only what IS confirmed is implemented here:
// a 1-mandatory-string lookup against the shared pool (kind 0 = turn-to),
// the identical shape to the sibling teleport_check_done/
// move_to_check_done. HIGH CONFIDENCE (not CONFIRMED) for the exact
// argument position/count; CONFIRMED for the shared status function and
// the done/pending polarity itself (Sec35.2).
int stub_turn_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "turn_to_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindTurnTo);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// move_to_check_done (Sec22.15, Sec35.2). The real function takes 8
// arguments (a previously-allocated request id; arg 2 the character name;
// a target/group-member selector; a movement-style code; 3 booleans; a
// group-member index) and its fuller body additionally matches a
// per-character cached request-id tag, resolves a target or group member,
// and - only once genuinely arrived (a distance-under-1-unit-plus-
// predicate test this project does not model) - dispatches an arrival
// animation before clearing its own per-character sub-record (+0xc30/
// +0xc34). None of that fuller pipeline is modeled here: deliberately out
// of this task's own narrow scope (Sec35's brief is the shared pool's
// done/pending CONVENTION fix, not a reimplementation of every wrapper's
// full body - the same "don't invent a generic entity shape" boundary
// engine_state.h's own top note already states for CharacterState).
// What IS implemented, CONFIRMED (Sec35.2/Sec22.15's own corrected status-
// code table): arg 2 (the character name) is used as a genuine per-
// request lookup key - NOT ignored - against the SAME shared pool
// teleport_check_done/turn_to_check_done/vehicle_pathfind_check_done
// consult (kind 2 = move-to/pathfind), with the corrected polarity
// (status != 0 -> true, Sec35.2).
int stub_move_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "move_to_check_done", upStateTag(L));
    std::string name = argString(L, 2);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindMoveOrPathfind);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// vehicle_pathfind_check_done (Sec9.10) - the REFERENCE reading Sec35.2's
// own cross-check cites as "already correct", i.e. this one was never
// mis-read in the first place. Arguments: 1 mandatory string - a vehicle
// reference. Return: 1 NUMBER (NOT a boolean, CONFIRMED - lua_pushnumber
// then return 1) - the raw 3-way status code itself: 0 = pending, 1 =
// done (consumed), 2 = no active request OR the vehicle failed to resolve
// OR it has no occupant/part in slot 0 (most plausibly a driver check,
// Sec9.10's own text) - both the "no vehicle" and "no active request"
// routes converge on the SAME literal fallback constant, 2.0. This
// project has no vehicle-resolution/occupant registry (deliberately, same
// boundary as every other entity this project does not model) - every
// name defaults unresolvable, itself the real, CONFIRMED fallback for an
// unmodeled vehicle, not a guess (see
// EngineState::vehiclePathfindCheckDoneCode's own doc comment).
int stub_vehicle_pathfind_check_done(lua_State* L) {
    logCall(L, upLog(L), "vehicle_pathfind_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    lua_pushnumber(L, upState(L)->vehiclePathfindCheckDoneCode(name));
    return 1;
}

} // namespace stubs_b04
} // namespace sr3luahost

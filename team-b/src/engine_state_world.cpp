// EngineState method definitions moved verbatim out of include/sr3luahost/engine_state.h
// (structural refactor, step 1b: no behaviour change). Area: world.

#include "sr3luahost/engine_state.h"

namespace sr3luahost {

uint64_t EngineState::npcAiHandleOf(const std::string& name) const {  // 0 = not tracked (yet)
        auto it = npcAiHandles_.find(name);
        return it == npcAiHandles_.end() ? 0 : it->second;
    }

void EngineState::setOnVehicleExitHook(const std::string& targetName, const std::string& callbackName) {
        if (callbackName.empty()) onVehicleExitByTarget_.erase(targetName); else onVehicleExitByTarget_[targetName] = callbackName;
    }

std::vector<std::pair<std::string, std::string>> EngineState::drainShieldPollQueries() {
        std::vector<std::pair<std::string, std::string>> out;
        out.swap(shieldPollQueries_);
        return out;
    }

std::vector<std::pair<std::string, std::string>> EngineState::drainRcDriverPollQueries() {
        std::vector<std::pair<std::string, std::string>> out;
        out.swap(rcPollQueries_);
        return out;
    }

// Design notes moved verbatim from EngineState::installVehicleTypeClassTable's declaration in engine_state.h
// (refactor step 3a):
// NEW 2026-10-10 (vehicle-class xtbl wiring task): the REAL class-by-
// type-name table sr3vehicleinfo::ParseAllVehicles already computes
// correctly (include/sr3vehicleinfo/vehicle_entry.h,
// VehicleTypeUnion::vehicleClass - 0 Automobile/1 Motorcycle/
// 2 Airplane/3 Helicopter/4 Vtol/5 Watercraft, the SAME numeric space
// VehicleState::flyingType's own CONFIRMED values use, Sec30.5/Sec33.1),
// keyed by sr3luahost::AsciiLowerFold(VehicleEntry::name) - the exact
// fold convention zone_object_resolver.h already established for this
// project's one shared case-insensitive-name lookup. Installed by a
// caller (tools/lua_host_run.cpp's real-cache install step,
// installVehicleTypeClassTableFromCache) from every real
// `<name>_veh.xtbl` found in misc_tables.vpp_pc + dlc1/2/3.vpp_pc
// (spec-vehicle-data.md Sec7.9: "123 total"). Stored as a plain
// unordered_map<string,int32_t> rather than sr3vehicleinfo's own
// VehicleClass enum so this header does not need to include
// sr3vehicleinfo/vehicle_type.h just for a one-field install function -
// the caller casts VehicleClass to int32_t when building the map it
// passes in.
//
// loadZoneObjectsSection() below is the ONE consumer: a zone-placed
// `Script_Vehicle` record's own `script_vehicle_type_name` property
// (sr3zone::ScriptVehicleTypeName(), include/sr3zone/zone_objects.h) is
// looked up here (case-folded) to populate that instance's
// VehicleState::flyingType and, when the resolved class is 3 (heli) or
// 4 (Vtol), HelicopterFlyTo::qualifies - replacing the single hardcoded
// "m05_heli_pierce" name special case getOrCreateVehicle() used to
// carry (removed this same task; see that method's own history in
// src/lua_engine_state.cpp's git log) with the real, general,
// data-driven mechanism that special case's own doc comment said this
// project was missing. Empty (the default - every synthetic unit test,
// and any run that never installs it) means loadZoneObjectsSection()
// can still resolve a script_vehicle record's own NAME (objectResolves())
// but never its type's class - flyingType/qualifies stay OPEN exactly as
// before this task, matching this project's standing "empty install = no
// behavior change" convention (installSpawnRankHitPoints/
// installZsceneTable's own doc comments).
void EngineState::installVehicleTypeClassTable(std::unordered_map<std::string, int32_t> classByLowerTypeName) {
        vehicleTypeClassByLowerName_ = std::move(classByLowerTypeName);
    }

void EngineState::installVehicleSpecialTypeTable(std::unordered_map<std::string, int32_t> specialByLowerTypeName) {
        vehicleSpecialTypeByLowerName_ = std::move(specialByLowerTypeName);
    }

EngineState::LiveObjectKind EngineState::liveObjectKind(const std::string& name) const {
        auto it = liveObjectKinds_.find(name);
        return it == liveObjectKinds_.end() ? LiveObjectKind::kNone : it->second;
    }

// Design notes moved verbatim from EngineState::bridgeLiveCharacterPosition's declaration in engine_state.h
// (refactor step 3a):
// Writes a REAL, currently-simulated position into the SAME
// CharacterState::worldPosition / VehicleState::worldPosition field
// get_dist and the other position-shaped natives already read
// (character_state.h/vehicle_state.h's own doc comments on
// `worldPosition`) - this bridges into existing storage, it does not
// create a second one. Marks `name` live (kCharacter/kVehicle) for the
// resolver above. Call once per rendered frame, before the mission's
// own per-frame scheduler pass runs, so a coroutine resumed this frame
// observes THIS frame's position, never a stale one.
void EngineState::bridgeLiveCharacterPosition(const std::string& name, const WorldPosition& pos) {
        getOrCreateCharacter(name).worldPosition.set(pos);
        liveObjectKinds_[name] = LiveObjectKind::kCharacter;
    }

void EngineState::bridgeLiveVehiclePosition(const std::string& name, const WorldPosition& pos) {
        getOrCreateVehicle(name).worldPosition.set(pos);
        liveObjectKinds_[name] = LiveObjectKind::kVehicle;
    }

// Design notes moved verbatim from EngineState::scriptedRequestCheckDone's declaration in engine_state.h
// (refactor step 3a):
// teleport_check_done (Sec15.17) / turn_to_check_done / move_to_check_done
// (Sec22.15 - see that stub's own doc comment in
// lua_spec_confirmed_stubs.cpp for what of its fuller 8-argument body is
// deliberately NOT modeled): the shared boolean convention all three
// push, CONFIRMED (Sec35.2): `status != 0` - true for "done" or "no
// request," false only while genuinely pending.
bool EngineState::scriptedRequestCheckDone(const std::string& name, int kind) {
        return scriptedRequestStatusCode(name, kind) != 0;
    }

WorldPosition EngineState::worldPositionOr(const OpenValue<WorldPosition>& v, const WorldPosition& fallback) {
        if (v.known()) return v.get();
        if (worldPositionProbeEnabled_) {
            ++worldPositionProbeReadCount_;
            return fallback;
        }
        return v.get(); // throws OpenStateError: real OPEN gap, diagnostic not enabled
    }

// Design notes moved verbatim from EngineState::tryResolveWorldPositionFromZoneData's declaration in engine_state.h
// (refactor step 3a):
// REAL fallback (2026-10-10, m07 get_dist/npc_leash_to_nav live-
// playtest follow-up - NOT the diagnostic probe above): `worldPosition`
// is written live for exactly one name, "#PLAYER1#"
// (bridgeLiveCharacterPosition()'s own doc comment - deliberately the
// ONLY one). Any OTHER resolved name (an NPC, a navpoint, a trigger
// anchor, ...) that a get_dist/npc_leash_to_nav/teleport_vehicle/
// get_object_pos/turn_to_do/vehicle_rc_get_signal_strength call reaches
// has never had anything write its worldPosition this way, so it reads
// as OPEN even though the name itself resolved correctly - a real gap
// in COVERAGE, not evidence the field's own reasoning (character-
// shaped, Sec4.2) is wrong. But the real engine's own zone data
// (.czn_pc) already gives a REAL, on-disk position for exactly this
// class of name - the SAME sr3zone::ObjectRecord/ObjectPosition()
// source this project's own in-world-trigger auto-registration
// already uses live (runPlaytest()'s tryAutoRegister lambda, spec-
// lua-api-behaviour.md Sec1.14) - confirmed (not assumed) to cover
// navpoints too: tests/real_zone_objects_test.cpp's own
// navpointWithPosition counter. So before surfacing OpenStateError,
// try that same real source: if `name` resolves to a zone-object
// record with a position property, that IS a real, confirmed position
// for it (not a fabricated 0,0,0 stand-in) - cache it into `v` via one
// real .set() so repeat reads are O(1) and consistent, and report a
// genuine hit. Returns false (never throws itself) when `name` either
// isn't a real zone object or has no position property on its own
// record - the caller's existing `v.get()`/`worldPositionOr()` call
// is the correct, honest fallback for that genuinely-unresolved case
// (e.g. the one demo playtest vehicle, which Sec11.10's own closed
// 4-producer list confirms the real engine's name map never gets a
// live entry for either).
bool EngineState::tryResolveWorldPositionFromZoneData(const std::string& name, OpenValue<WorldPosition>& v) {
        if (v.known()) return true;
        const sr3zone::ObjectRecord* rec = resolveZoneObject(name);
        std::optional<sr3zone::Position3f> pos = rec ? sr3zone::ObjectPosition(*rec) : std::nullopt;
        if (!pos) return false;
        v.set(WorldPosition{static_cast<double>(pos->x), static_cast<double>(pos->y), static_cast<double>(pos->z)});
        ++zoneDataWorldPositionHitCount_;
        return true;
    }

void EngineState::recordGetDistCall(const std::string& a, const std::string& b,
                            const WorldPosition& pa, bool knownA,
                            const WorldPosition& pb, bool knownB) {
        const bool samePair = (a == getDistStall_.nameA && b == getDistStall_.nameB) ||
                               (a == getDistStall_.nameB && b == getDistStall_.nameA);
        getDistStall_.repeatCount = samePair ? (getDistStall_.repeatCount + 1) : 1;
        getDistStall_.nameA = a;
        getDistStall_.nameB = b;
        getDistStall_.posA = pa;
        getDistStall_.knownA = knownA;
        getDistStall_.posB = pb;
        getDistStall_.knownB = knownB;
    }

void EngineState::attachGamePauseScheduler(BareThreadTable* threads, lua_State* gameplayL) {
        gamePauseThreads_ = threads;
        gamePauseGameplayL_ = gameplayL;
    }

// Design notes moved verbatim from EngineState::groupFirstNpcName's declaration in engine_state.h
// (refactor step 3a):
// Sec28.5 `group_get_next_npc` / Sec28.6 `group_get_first_npc`: the group's own
// +0x40 member-list head and each member's own +0x9c "next" link (Sec28.6/
// Sec18.14 - NOT the light-group's unrelated +0x40 flags byte, Sec17.24,
// which this project never touches here). Re-derived directly from the SAME
// ordered member roster `group_members_alive` (Sec18.14) already maintains
// (`groupMemberNames_`/`registerGroupMemberForTesting`) instead of a second,
// separately-seeded OPEN map - this project has only ever modelled one
// "which characters belong to which named group" registry, and Sec28.5/
// Sec28.6 read the exact same list Sec18.14 counts, just from the other end.
// An empty/unregistered group is a well-defined "no members known" answer
// (nothing pushed) - the same "collapses to 0" precedent groupMembersAliveCount
// already established for this registry - not a fabricated name and not OPEN.
const std::string* EngineState::groupFirstNpcName(const std::string& groupName) const {
        auto it = groupMemberNames_.find(groupName);
        if (it == groupMemberNames_.end() || it->second.empty()) return nullptr;
        return &it->second.front();
    }

const std::string* EngineState::groupNextNpcName(const std::string& groupName, const std::string& currentName) const {
        auto it = groupMemberNames_.find(groupName);
        if (it == groupMemberNames_.end()) return nullptr;
        const auto& members = it->second;
        for (size_t i = 0; i < members.size(); ++i) {
            if (members[i] == currentName) {
                if (i + 1 < members.size()) return &members[i + 1];
                return nullptr; // wraps back to the group head: stop, push nothing
            }
        }
        return nullptr;
    }

int EngineState::resolveTeamId(const std::string& name) const {
        auto it = teamIdByName_.find(name);
        return it != teamIdByName_.end() ? it->second : 9; // CONFIRMED sentinel, 0x0094cc60
    }

bool EngineState::setTeamRelationIfInBounds(int id1, int id2, int value) {
        if (id1 < 0 || id1 >= kTeamRelationMatrixDim || id2 < 0 || id2 >= kTeamRelationMatrixDim) return false;
        teamRelation_[static_cast<size_t>(id1)][static_cast<size_t>(id2)] = value;
        teamRelation_[static_cast<size_t>(id2)][static_cast<size_t>(id1)] = value;
        if (id1 == 5) { teamRelation_[6][static_cast<size_t>(id2)] = value; teamRelation_[static_cast<size_t>(id2)][6] = value; }
        if (id2 == 5) { teamRelation_[6][static_cast<size_t>(id1)] = value; teamRelation_[static_cast<size_t>(id1)][6] = value; }
        return true;
    }

int EngineState::teamRelationForTesting(int id1, int id2) const {
        if (id1 < 0 || id1 >= kTeamRelationMatrixDim || id2 < 0 || id2 >= kTeamRelationMatrixDim) return -2;
        return teamRelation_[static_cast<size_t>(id1)][static_cast<size_t>(id2)];
    }

void EngineState::setPauseMapHoveredZoneForTesting(uint32_t zoneHandle, bool hasParentDistrict, int32_t fieldPlus0x54) {
        pauseMapHoveredZone_ = PauseMapHoveredZone{zoneHandle, hasParentDistrict, fieldPlus0x54};
    }

void EngineState::registerZoneMemberForTesting(uint32_t zoneHandle, float weight, bool owned) {
        zoneMembersByZone_[zoneHandle].push_back(ZoneMember{weight, owned});
    }

const std::vector<ZoneMember>* EngineState::zoneMembersForTesting(uint32_t zoneHandle) const {
        auto it = zoneMembersByZone_.find(zoneHandle);
        return it == zoneMembersByZone_.end() ? nullptr : &it->second;
    }

// Design notes moved verbatim from EngineState::selectVcustComponentForTesting's declaration in engine_state.h
// (refactor step 3a):
// Test-only hook for vcust_purchase_component's own stale-selection
// HARDENING (see VcustPreview::selectedComponentItemIndex/Price above):
// simulates "a fresh selection was just made" so a test can exercise
// BOTH the valid-input faithful path (price charged) and, by NOT
// calling this (or calling vcust_purchase_component twice), the
// hardened-refusal path (the real engine's own -1-reset trigger).
void EngineState::selectVcustComponentForTesting(int32_t index, double price) {
        vcustPreview_.selectedComponentItemIndex = index;
        vcustPreview_.selectedComponentItemPrice.set(std::to_string(index), price);
    }

const PlacedObjectState* EngineState::findPlacedObject(const std::string& name) const {
        auto it = placedObjects_.find(name);
        return it != placedObjects_.end() ? &it->second : nullptr;
    }

bool EngineState::cityZoneSwapValue(const std::string& name) const {
        auto it = cityZoneSwaps_.find(name);
        return it != cityZoneSwaps_.end() && it->second;
    }

bool EngineState::trafficLanesEnabledValue(const std::string& name) const {
        auto it = trafficLanesEnabled_.find(name);
        return it != trafficLanesEnabled_.end() && it->second;
    }

bool EngineState::sidewalkNodesDisabledValue(const std::string& name) const {
        auto it = sidewalkNodesDisabled_.find(name);
        return it != sidewalkNodesDisabled_.end() && it->second;
    }

void EngineState::recordWaypointRemove(int32_t mode) {
        ++waypointRemoveCount_;
        lastWaypointRemoveMode_ = mode;
    }

void EngineState::setDrawbridgeAreaDisabled(int32_t area1Based, bool disabled) {
        if (area1Based < 1 || area1Based > 32) return;
        uint32_t bit = 1u << static_cast<uint32_t>(area1Based - 1);
        if (disabled) drawbridgeDisabledMask_ |= bit;
        else drawbridgeDisabledMask_ &= ~bit;
    }

// Design notes moved verbatim from EngineState::setGuardianAngelGroupRegistered's declaration in engine_state.h
// (refactor step 3a):
// guardian_angel_register_script_group/guardian_angel_unregister_
// script_group (Sec6.6/Sec6.8). CONFIRMED: structurally identical -
// same resolver (0x005eab60, a further member of the SAME shared
// type-gated by-name resolver family objectResolves() already models,
// same underlying generic resolver 0x004588f0/class-descriptor table
// 0x02cc9900), same liveness check, same position-pair read off the
// resolved object; differ only in their terminal call (register vs
// unregister). Those terminal calls' (0x00627260/0x00627290) own
// internal behaviour is OPEN - this project records only the resolve-
// gated register/unregister boolean per name, the same "resolve + flag,
// no deeper replay" simplification as traffic_disable_lanes/sidewalk_
// disable_nodes above (not the resolved object's own world position,
// which no other Sec6 consumer reads back either).
void EngineState::setGuardianAngelGroupRegistered(const std::string& name, bool registered) {
        guardianAngelGroups_[name] = registered;
    }

bool EngineState::guardianAngelGroupRegistered(const std::string& name) const {
        auto it = guardianAngelGroups_.find(name);
        return it != guardianAngelGroups_.end() && it->second;
    }

void EngineState::registerNotorietyAliasTrackCodeForTesting(const std::string& name, int32_t code) {
        notorietyAliasTrackCodes_[AsciiLowerFold(name)] = code;
    }

int32_t EngineState::lookupNotorietyAliasTrackCode(const std::string& name) const {
        auto it = notorietyAliasTrackCodes_.find(AsciiLowerFold(name));
        return it != notorietyAliasTrackCodes_.end() ? it->second : kNotorietyAliasTrackNotFound;
    }

// Design notes moved verbatim from EngineState::looksLikeVehicleNameForPlayerClassGate's declaration in engine_state.h
// (refactor step 3a):
// CHOSEN (not spec-confirmed - see playerClassChainResolves(name)'s own
// doc comment above for the full citation chain and why no better
// classification source exists in this project today) heuristic: a name
// is treated as vehicle-shaped, and so defaults to FALSE on the
// player-class gate, when it either contains "vehicle" (case-
// insensitive, matches anywhere - covers 'debris_vehicle 001',
// 'Starting_Vehicles', 'Script_Vehicle 001') or begins with the short
// token "veh" followed by a space/underscore/hyphen/end-of-string
// (case-insensitive - covers 'Veh CP SJ Spotlight Heli' without
// matching any character name in this project's own probe corpus, none
// of which begin with "veh"). Every other name defaults to TRUE
// (character/human class). Static, pure string match - no engine state,
// so it is trivially test-observable and does not need its own
// OpenValueMap entry.
bool EngineState::looksLikeVehicleNameForPlayerClassGate(const std::string& name) {
        std::string lower = AsciiLowerFold(name);
        if (lower.find("vehicle") != std::string::npos) return true;
        if (lower.size() >= 3 && lower.compare(0, 3, "veh") == 0) {
            if (lower.size() == 3) return true;
            char next = lower[3];
            if (next == ' ' || next == '_' || next == '-') return true;
        }
        return false;
    }

bool EngineState::playerClassChainResolves(const std::string& name) {
        if (playerClassChainResolves_.known(name)) return playerClassChainResolves_.get(name);
        return !looksLikeVehicleNameForPlayerClassGate(name);
    }

// Design notes moved verbatim from EngineState::recordObjectiveTextClear's declaration in engine_state.h
// (refactor step 3a):
// objective_text_clear (Sec1.4). CONFIRMED: no resolve/handle-lookup
// step at all (unlike every other function in the §1 cluster) - just a
// slot index, a zero-argument housekeeping call (0x00d1e0c0, no
// observable effect in this project's scope) and a dispatch
// (0x007eda00 -> 0x007ed7d0, opcode 9) clearing the global objective-
// text/HUD slot (spec-lua-api-behaviour.md Sec1.4's own text: "clears
// the named objective-text/HUD slot", independently strengthened by
// direct disassembly of 0x007eda00 itself - not merely inferred from
// address proximity to Sec1.3's setter).
//
// UPDATED (spec-hud-runtime.md §6.5 HUD-objective-overlay task,
// 2026-10-09): when this doc comment was first written, objective_text
// (Sec1.3) was NOT yet one of this batch's implemented names, so only
// the raw call itself (which slot index, how many times) was test-
// observable - there was no slot state to actually clear yet. Sec1.3 is
// now implemented (lua_spec_confirmed_stubs_b08_batch1_core.cpp,
// objectiveTextSlots()), so this call now ALSO erases the slot from
// that map, completing the CONFIRMED "clears the slot" behaviour rather
// than leaving it a counter-only stub forever. Sec1.4's own text
// documents no bounds check on the slot index at all (unlike Sec1.3's
// setter's CONFIRMED "< 4"), so no range check is added here either -
// std::unordered_map::erase() on any int64_t key, in or out of [0,4),
// is always safe-by-construction (a no-op if the key is absent), with
// no hardening divergence to label or count.
void EngineState::recordObjectiveTextClear(int64_t slotIndex) {
        ++objectiveTextClearCounts_[slotIndex];
        objectiveTextSlots_.erase(slotIndex);
    }

uint64_t EngineState::objectiveTextClearCount(int64_t slotIndex) const {
        auto it = objectiveTextClearCounts_.find(slotIndex);
        return it != objectiveTextClearCounts_.end() ? it->second : 0;
    }

void EngineState::registerNotorietyTrackCodeForTesting(const std::string& name, int32_t code) {
        notorietyTrackCodes_[name] = code;
    }

int32_t EngineState::lookupNotorietyTrackCode(const std::string& name) const {
        auto it = notorietyTrackCodes_.find(name);
        return it != notorietyTrackCodes_.end() ? it->second : kNotorietyTrackCodeNotFound;
    }

bool EngineState::notorietyTrackFlagValue(uint32_t trackBaseAddress) const {
        auto it = notorietyTrackFlags_.find(trackBaseAddress);
        return it != notorietyTrackFlags_.end() && it->second;
    }

uint64_t EngineState::pegUnloadCount(const std::string& name) const {
        auto it = pegUnloadCount_.find(name);
        return it != pegUnloadCount_.end() ? it->second : 0;
    }

bool EngineState::hasActiveScriptRequest(const std::string& objectName, int channel) const {
        return activeScriptRequestId_.count(std::make_pair(objectName, channel)) != 0;
    }

int32_t EngineState::activeScriptRequestIdOf(const std::string& objectName, int channel) const {
        auto it = activeScriptRequestId_.find(std::make_pair(objectName, channel));
        return it != activeScriptRequestId_.end() ? it->second : 0;
    }

int32_t EngineState::allocateMoveToActionId() {
        int32_t next = (moveToActionIdCounter_.get() + 1) & 0xFFFF; // OPEN until set
        if (next == 0) next = 1; // CONFIRMED: skips 0
        moveToActionIdCounter_.set(next);
        return next;
    }

int32_t EngineState::lookupActionNameIndex(const std::string& name) const {
        for (const auto& kv : actionNameIndices_) {
            if (kv.first.size() != name.size()) continue;
            bool eq = true;
            for (size_t i = 0; i < name.size() && eq; ++i) {
                if (std::tolower(static_cast<unsigned char>(kv.first[i])) != std::tolower(static_cast<unsigned char>(name[i])))
                    eq = false;
            }
            if (eq) return kv.second;
        }
        return kActionNameNotFound;
    }

EngineState::NotorietySpawnGroupState& EngineState::getOrCreateNotorietySpawnGroup(const std::string& name) {
        return notorietySpawnGroups_[name];
    }

std::vector<EngineState::CustomizationItemWearRequest>& EngineState::customizationItemWearRequests() {
        return customizationItemWearRequests_;
    }

int32_t EngineState::lookupBehaviorSetCode(const std::string& name) const {
        auto it = behaviorSetCodes_.find(name);
        return it != behaviorSetCodes_.end() ? it->second : kBehaviorSetNotFound;
    }

bool EngineState::conversationSlotActive(const std::string& slotKey) const {
        return conversationSlotPlaying_.known(slotKey) ? conversationSlotPlaying_.get(slotKey) : false;
    }

std::string EngineState::pcrMorphKey(const std::string& sessionLower, const std::string& slotKey) {
        return sessionLower + "\x1f" + slotKey;
    }

const char* EngineState::teamNameForId(int id) {
        static const char* const kNames[] = {"Playas", "Luchadores", "Deckers", "Morningstar",
                                             "Neutral Gang", "Police", "STAG", "Civilian"};
        return (id >= 0 && id <= 7) ? kNames[id] : "None";
    }

const char* EngineState::canonicalTeamName(const std::string& authored) {
        for (int id = 0; id <= 7; ++id) {
            const std::string name = teamNameForId(id);
            if (name.size() != authored.size()) continue;
            bool same = true;
            for (size_t i = 0; i < name.size() && same; ++i) {
                char a = name[i], b = authored[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                same = (a == b);
            }
            if (same) return teamNameForId(id);
        }
        return "None";
    }

int32_t EngineState::resolveInventorySlotId(const std::string& slotName) const {
        auto it = slotNameToId_.find(slotName);
        if (it != slotNameToId_.end()) return it->second;
        std::string lower = slotName;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return std::tolower(ch); });
        if (lower == "best") return 0xd;
        if (lower == "best-firearm") return 0xe;
        if (lower == "best-melee") return 0xf;
        if (lower == "best-non-lethal") return 0x10;
        if (lower == "best-non-explosive") return 0x11;
        return 0xff; // CONFIRMED "not found" sentinel
    }

void EngineState::normalizeWindVector(double x, double y, double z, double& outX, double& outY, double& outZ) {
        double mag = std::sqrt(x * x + y * y + z * z);
        if (!(mag >= 1.0E-7) || !std::isfinite(mag)) {
            outX = 1.0;
            outY = 0.0;
            outZ = 0.0;
            return;
        }
        outX = x / mag;
        outY = y / mag;
        outZ = z / mag;
    }

int32_t EngineState::ingameEffectIndex(const std::string& triggerName) const {
        auto it = ingameEffectIndices_.find(triggerName);
        return it != ingameEffectIndices_.end() ? it->second : -1;
    }

EngineState::ContinuousSpawnOrder& EngineState::getOrCreateContinuousSpawnOrder(const std::string& groupName) {
        return continuousSpawnOrders_[groupName];
    }

int32_t EngineState::continuousSpawnOrderTypeIndex(const std::string& orderTypeName) {
        if (orderTypeName == "attack") return 0;
        if (orderTypeName == "defend") return 1;
        if (orderTypeName == "moveflee") return 2;
        if (orderTypeName == "idle") return 3;
        return 4;
    }

// Design notes moved verbatim from EngineState::namedHookCallback's declaration in engine_state.h
// (refactor step 3a):
// Shared, uniform stand-in for this project's own named-hook-slot-array
// family (0x005e4520/0x00a1fcc0/0x00a29920, Sec1.1 et al.) for every one
// of this batch's own hook-registration natives - on_projectile_hit
// (Sec22.10, GLOBAL slot 7), on_near_crash (Sec22.11, PER-OBJECT,
// computed sub-record +0x1f00 slot 0xf), on_wieldable_prop_destroy
// (Sec24.20, PER-OBJECT +0x1f00 slot 0xe), on_taunt (Sec24.21, dual-path:
// PER-OBJECT +0x1f00 slot 0xc OR PER-CHARACTER +0xf8 slot 0x11),
// on_notoriety_event (Sec24.23, PER-OBJECT +0x1f00 slot 2, default name
// "#PLAYER1#"), on_hud_screen_exit (Sec24.24, PER-HUD-SCREEN-NAME,
// dedicated registry singleton 0x012fced8, slot 0), on_character_grabbed
// (Sec24.25, dual-path: PER-OBJECT +0x1f00 slot 0xd OR PER-CHARACTER
// +0xf8 slot 0x19). This project does not model the real hook-slot-
// array/sub-record internals (base address, slot index, ref-counting)
// anywhere - only that a callback name was registered (or cleared, "" =
// none) against a resolved key is tracked, honestly, per that native's
// own "record the request" convention (same MinimapIconRecord/
// ObjectIndicatorRecord-style precedent this header uses throughout).
// Keyed by a per-native composite string ("<native>:<key>", or just
// "<native>" for the one genuinely global slot) so two different
// natives' own slots never collide even when they share a resolved
// object name.
std::string EngineState::namedHookCallback(const std::string& compositeKey) const {
        auto it = namedHookCallbacks_.find(compositeKey);
        return it != namedHookCallbacks_.end() ? it->second : std::string();
    }

void EngineState::setNamedHookCallback(const std::string& compositeKey, const std::string& callbackName) {
        namedHookCallbacks_[compositeKey] = callbackName;
    }

double EngineState::setRcMaxSignalRangeReturningPrevious(double newRange) {
        double prev = rcMaxSignalRange_.get();
        rcMaxSignalRange_.set(newRange);
        return prev;
    }

void EngineState::bossBattleKbEnable(bool enable) { // .cpp body kept here (trivial, no resolve/replication to model)
        if (!enable && killbaneQteActive_) killbaneCleanupPerformed_ = true; // CONFIRMED: only this setter's false-path cleans up
        killbaneQteActive_ = enable;
    }

void EngineState::bossBattleMarsKillbaneActive(bool active) {
        killbaneQteActive_ = active; // CONFIRMED: clears/sets with NO cleanup, regardless of previous value
    }

// Design notes moved verbatim from EngineState::spawnOverrideClearOverrideForHood's declaration in engine_state.h
// (refactor step 3a):
// --- spawn_override_clear_override_for_hood (Sec49.2, `0x00a5fb40`) ---
// UPDATED 2026-10-09 (spec sync 3ecbf84, closing this entry's own
// previously-AGG placeholder): CONFIRMED real body - resolves the hood
// through the SAME chain as the SET sibling above, then
// `AND [hood+0x64], 0xfb` (clears override-enabled bit 0x04) and
// `MOV [hood+0x70], 0` (zeroes the stored category pointer) - "not a
// 'remove from a list' or 'reset to a default category' - there is no
// fallback category value involved; it simply resets both override
// fields to unset." Modeled here by erasing the hood from BOTH of the
// SET sibling's own real fields (SpawnOverride::perHoodCategory and
// ::perHoodCategoryNullStored above) - the SAME struct the SET entry
// point actually writes, NOT the disconnected placeholder map this
// accessor used to expose (a real pre-existing bug this sync caught:
// the old clear-for-hood erased from a map SET never wrote to, so
// calling set-then-clear never actually cleared anything). On a host,
// the real function also broadcasts the identical opcode `0x45`/
// sub-type `0x29` record shape the SET native sends, with a "none"
// sentinel hash in place of a resolved category hash, replicated to
// remote machines the same way SET is - NOT modeled here, matching
// this project's own existing scope line for this whole family (the
// SET sibling immediately above does not model replication either;
// adding it to CLEAR alone, when its own sibling lacks it, would be a
// net-new, unasked-for mechanism, not a sibling-parity fix).
void EngineState::spawnOverrideClearOverrideForHood(const std::string& hood) {
        auto& so = spawnOverride();
        so.perHoodCategory.erase(hood);
        so.perHoodCategoryNullStored.erase(hood);
    }

// Design notes moved verbatim from EngineState::damageDealtDifficultyMultiplier's declaration in engine_state.h
// (refactor step 3a):
// --- spec-player-runtime.md Sec15.3a (wave 6, 2026-10-09): the damage
// sink 0x0096fa20 applies "a difficulty-based scalar when the
// instigator is player-class" on top of the caller's own base amount.
// spec-tables-progression.md Sec14.7 gives the real, empirically-
// decoded Damage_Dealt_Mult values for difficulty_levels.xtbl's FIRST
// <Dynamic_Difficulty> block ("Set A"): L0/L1/L2 = 1.1/1.0/0.75 (easy
// deals more damage, hard deals less - the expected shape). Set A is
// used here, not Set B (whose own Damage_Dealt_Mult, 1.0/0.9/0.6, is
// also harsher at L2), per this project's own standing "single player
// only" simplification (controlLockGatePasses's own doc comment) -
// Sec14.7 itself leaves the set-A-vs-set-B semantic split as HYPOTHESIS
// only ("consistent with, but not confirmed as, a single-player-vs-
// co-op split"), so this is a CHOSEN pick between two real, CONFIRMED-
// decoded tables, not an invented value. difficultyLevel_ is already
// kept in {0,1,2} by
// game_difficulty_select's own setter (Sec44.5: "only accepts unsigned
// values below 3"); the clamp below is a defensive no-op against a test
// writing difficultyLevel() directly out of range.
float EngineState::damageDealtDifficultyMultiplier() const {
        static constexpr float kSetADamageDealtMult[3] = {1.1f, 1.0f, 0.75f};
        int32_t lvl = difficultyLevel_;
        if (lvl < 0 || lvl > 2) lvl = 1; // defensive only - see doc comment above
        return kSetADamageDealtMult[lvl];
    }

bool EngineState::lightGroupResolve(const std::string& name) const {
        return lightGroupResolves_.known(name) ? lightGroupResolves_.get(name) : false;
    }


bool EngineState::inventoryItemDefinitionResolve(const std::string& name) const {
        if (inventoryItemDefinitionResolves_.known(name)) return inventoryItemDefinitionResolves_.get(name);
        if (inventoryItemTableComplete_) {
            // items_inventory.xtbl (+DLC) installed: _stricmp over the COMPLETE row set; no match -> null.
            const std::string f = AsciiLowerFold(name);
            return inventoryItemDefinitionResolves_.known(f) ? inventoryItemDefinitionResolves_.get(f) : false;
        }
        if (AsciiLowerFold(name) == "none") return false;
        return inventoryItemDefinitionResolves_.get(name); // throws OpenStateError
    }

void EngineState::installInventoryItemNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) inventoryItemDefinitionResolves_.set(AsciiLowerFold(n), true);
        inventoryItemTableComplete_ = true;
    }

void EngineState::installItems3dNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) propDefinitionResolves_.set(AsciiLowerFold(n), true);
        items3dTableComplete_ = true;
    }

bool EngineState::propNameResolves(const std::string& propName) {
        if (items3dTableComplete_) {
            const std::string k = AsciiLowerFold(propName);
            return propDefinitionResolves_.known(k) && propDefinitionResolves_.get(k);
        }
        return propDefinitionResolves_.get(propName); // OPEN until set
    }

void EngineState::registerGroupMemberForTesting(const std::string& groupName, const std::string& memberName) {
        groupMemberNames_[groupName].push_back(memberName);
    }

int32_t EngineState::groupMembersAliveCount(const std::string& groupName) {
        auto it = groupMemberNames_.find(groupName);
        if (it == groupMemberNames_.end()) return 0;
        int32_t count = 0;
        for (const auto& memberName : it->second) {
            if (!getOrCreateCharacter(memberName).isDeadHighConfidence) ++count;
        }
        return count;
    }

void EngineState::registerVehicleTeamIdForTesting(const std::string& teamName, int32_t id) {
        vehicleTeamIdByName_[teamName] = id;
    }

int32_t EngineState::lookupVehicleTeamId(const std::string& teamName) const {
        auto it = vehicleTeamIdByName_.find(teamName);
        return it != vehicleTeamIdByName_.end() ? it->second : kVehicleTeamIdDefault;
    }

} // namespace sr3luahost

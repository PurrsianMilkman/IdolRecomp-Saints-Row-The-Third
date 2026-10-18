// sr3tables_progression - implementation. Built ONLY on include/sr3xtbl/xtbl.h's
// accessors; every field read here is cited to its spec-tables-progression.md
// section in the matching header (tables_core.h / tables_world.h / tables_rules.h).
// No game executable, disassembly or decompiled code was consulted (cleanroom
// boundary - see the headers' banner comments).

#include "tables_progression_internal.h"

#include <array>
#include <string>
#include <string_view>

namespace sr3tables_progression {

// ===========================================================================
// gameplay_nags.xtbl / Gameplay_nag_globals.xtbl - spec-tables-progression.md S10.3
// ===========================================================================

const char* const kGameplayNagNames[20] = {
    "human_shield", "cruise_control", "power_attacks", "taunting", "fine_aim",
    "sprinting", "change_clothes", "grenades", "grenades_pc", "city_takeover",
    "hitman", "chop_shop", "weapon_upgraded", "gang_customize", "cell_rewards",
    "crib_stash", "radio_controls", "sprint_recharge", "low_ammo", "notoriety",
};

GameplayNagEntry ParseGameplayNagEntry(const Node* row) {
    GameplayNagEntry e;
    if (const std::string* n = ChildText(row, "Name")) {
        for (size_t i = 0; i < kGameplayNagCount; ++i) {
            if (NameEquals(*n, kGameplayNagNames[i])) {
                e.nagIndex = static_cast<int>(i);
                break;
            }
        }
    }
    e.incrementSeconds = ReadFloatAlways(row, "Increment");
    e.nagTimeSeconds = ReadFloatAlways(row, "Nag_Time");
    return e;
}

std::vector<GameplayNagEntry> ParseGameplayNagsTable(const Document& doc) {
    std::vector<GameplayNagEntry> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Nag_Data"); row; row = NextSibling(table, row, "Nag_Data"))
        out.push_back(ParseGameplayNagEntry(row));
    return out;
}

std::optional<GameplayNagGlobals> ParseGameplayNagGlobals(const Document& doc) {
    const Node* table = doc.table();
    const Node* row = FindChild(table, "Nag_Data");
    if (!row) return std::nullopt;
    GameplayNagGlobals g;
    g.missionPostponeSeconds = ReadInt32Always(row, "Mission_postpone_s");
    g.nagDisplayPostponeSeconds = ReadInt32Always(row, "Nag_display_postpone_s");
    return g;
}

// ===========================================================================
// mission_checkpoints.xtbl - spec-tables-progression.md S10.4
// ===========================================================================

MissionCheckpoint ParseMissionCheckpoint(const Node* row) {
    MissionCheckpoint m;
    m.missionName = OptText(row, "MissionName");
    m.checkpointName = OptText(row, "CheckpointName");
    m.index = ReadInt32Always(row, "Index");
    m.debug = ReadBoolAlways(row, "Debug");
    return m;
}

std::vector<MissionCheckpoint> ParseMissionCheckpointsTable(const Document& doc) {
    std::vector<MissionCheckpoint> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "MissionCheckpoints"); row; row = NextSibling(table, row, "MissionCheckpoints"))
        out.push_back(ParseMissionCheckpoint(row));
    return out;
}

// ===========================================================================
// mission_help.xtbl - spec-tables-progression.md S10.5
// ===========================================================================

std::vector<MissionHelpText> ParseMissionHelpTable(const Document& doc) {
    std::vector<MissionHelpText> out;
    const Node* table = doc.table();
    for (const Node* str = FindChild(table, "String"); str; str = NextSibling(table, str, "String")) {
        const Node* texts = FindChild(str, "Texts");
        if (!texts) continue;
        for (const Node* t = FindChild(texts, "Text"); t; t = NextSibling(texts, t, "Text")) {
            MissionHelpText mh;
            mh.name = OptText(t, "Name");
            mh.english = OptText(t, "English");
            if (!mh.english) mh.english = OptText(t, "DisplayText");
            mh.durationSeconds = ReadFloatAlways(t, "Duration");
            out.push_back(mh);
        }
    }
    return out;
}

// ===========================================================================
// spawn_info_categories.xtbl / spawn_info_groups.xtbl / spawn_info_ranks.xtbl
// - spec-tables-progression.md S10.6
// ===========================================================================

SpawnCategory ParseSpawnCategory(const Node* row) {
    SpawnCategory cat;
    cat.name = OptText(row, "Name");
    if (const Node* groups = FindChild(row, "Groups")) {
        for (const Node* g = FindChild(groups, "Group"); g; g = NextSibling(groups, g, "Group")) {
            SpawnCategoryGroup sg;
            sg.name = OptText(g, "Name");
            sg.dayChance = GetFloat(g, "DayChance");
            sg.nightChance = GetFloat(g, "NightChance");
            sg.dayCap = GetInt32(g, "DayCap");
            sg.nightCap = GetInt32(g, "NightCap");
            sg.vehicleDayCap = GetUInt32(g, "VehicleDayCap");
            sg.vehicleNightCap = GetUInt32(g, "VehicleNightCap");
            sg.itemCarried = OptText(g, "Item_Carried");
            sg.carryPercent = GetUInt32(g, "Carry_Percent");
            sg.groupCategory = OptText(g, "Group_Category");
            cat.groups.push_back(sg);
        }
    }
    if (const Node* flags = FindChild(row, "Flags")) {
        cat.noSpawnOutsideCategory = HasFlag(flags, "No Spawn Outside Category");
        cat.noEnemyGangSpawning = HasFlag(flags, "No Enemy Gang Spawning");
        cat.lawSpawningArea = HasFlag(flags, "Law Spawning Area");
        cat.lockdownArea = HasFlag(flags, "Lockdown Area");
    }
    cat.carDay = ReadFloatAlways(row, "CarDay");
    cat.carNight = ReadFloatAlways(row, "CarNight");
    cat.pedDay = ReadFloatAlways(row, "PedDay");
    cat.pedNight = ReadFloatAlways(row, "PedNight");
    cat.lawSpawnGroup = OptText(row, "Law_Spawn_Group");
    cat.lawCap = ReadInt32Always(row, "LawCap");
    cat.lawDelay = ReadInt32Always(row, "LawDelay");
    cat.generalPedSlotDay = ReadFloatAlways(row, "General_Ped_Slot_Day");
    cat.generalPedSlotNight = ReadFloatAlways(row, "General_Ped_Slot_Night");
    cat.specialPedSlotDay = ReadFloatAlways(row, "Special_Ped_Slot_Day");
    cat.specialPedSlotNight = ReadFloatAlways(row, "Special_Ped_Slot_Night");
    cat.specialVehicleSlotDay = ReadFloatAlways(row, "Special_Vehicle_Slot_Day");
    cat.specialVehicleSlotNight = ReadFloatAlways(row, "Special_Vehicle_Slot_Night");
    return cat;
}

std::vector<SpawnCategory> ParseSpawnInfoCategoriesTable(const Document& doc) {
    std::vector<SpawnCategory> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Category"); row; row = NextSibling(table, row, "Category"))
        out.push_back(ParseSpawnCategory(row));
    return out;
}

SpawnGroup ParseSpawnGroup(const Node* row) {
    SpawnGroup g;
    g.name = OptText(row, "Name");
    if (const Node* gf = FindChild(row, "GeneralFlags")) {
        g.unique = HasFlag(gf, "unique");
        g.vehicleOnly = HasFlag(gf, "vehicle_only");
        g.hasDesignatedDriver = HasFlag(gf, "has_designated_driver");
    }
    g.team = OptText(row, "Team");
    // spec-tables-progression.md 10.6, RESOLVED 2026-10-01: the six-name Spline_Type list is CONFIRMED
    // complete; "All Roads"/"Surface Roads" (58/61 real rows) match none of the six and resolve to -1 at
    // the real loader. Kept here as raw text; no enum resolution applied (see header comment).
    g.splineType = OptText(row, "Spline_Type");
    if (const Node* chars = FindChild(row, "Characters")) {
        for (const Node* ch = FindChild(chars, "Character"); ch; ch = NextSibling(chars, ch, "Character"))
            if (ch->text()) g.characters.push_back(*ch->text());
    }
    g.spawnDrunkPctDay = GetUInt32(row, "spawn_drunk_pct_day");
    g.spawnDrunkPctNight = GetUInt32(row, "spawn_drunk_pct_night");
    if (const Node* vehs = FindChild(row, "Vehicles")) {
        for (const Node* v = FindChild(vehs, "Vehicle"); v; v = NextSibling(vehs, v, "Vehicle")) {
            SpawnGroupVehicle sv;
            sv.name = OptText(v, "Name");
            sv.variant = OptText(v, "Variant");
            g.vehicles.push_back(sv);
        }
    }
    return g;
}

std::vector<SpawnGroup> ParseSpawnInfoGroupsTable(const Document& doc) {
    std::vector<SpawnGroup> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Group"); row; row = NextSibling(table, row, "Group"))
        out.push_back(ParseSpawnGroup(row));
    return out;
}

SpawnRank ParseSpawnRank(const Node* row) {
    SpawnRank r;
    r.name = OptText(row, "Name");
    r.type = OptText(row, "type");
    r.rankNumeric = ReadUInt32Always(row, "rank_numeric");
    r.hitPoints = ReadUInt32Always(row, "Hit_Points");
    r.bleedOutHitPoints = ReadUInt32Always(row, "Bleed_Out_Hit_Points");
    r.knockdownPoints = ReadUInt32Always(row, "Knockdown_Points");
    r.meleeDamageModifier = ReadFloatAlways(row, "Melee_Damage_Modifier");
    r.pctDmgToFlinch = GetFloat(row, "Pct_Dmg_to_Flinch");
    if (const Node* pti = FindChild(row, "per_team_info")) {
        for (const Node* elm = FindChild(pti, "per_team_elm"); elm; elm = NextSibling(pti, elm, "per_team_elm")) {
            PerTeamInfo t;
            t.team = OptText(elm, "Team");
            if (const Node* wl = FindChild(elm, "weapon_loadout")) {
                for (const Node* e = FindChild(wl, "weapon_loadout_elm"); e; e = NextSibling(wl, e, "weapon_loadout_elm")) {
                    WeaponLoadoutEntry we;
                    we.chance = ReadUInt8Always(e, "chance");
                    we.melee = OptText(e, "melee");
                    we.firearm = OptText(e, "firearm");
                    we.thrown = OptText(e, "thrown");
                    t.weaponLoadout.push_back(we);
                }
            }
            if (const Node* pr = FindChild(elm, "personality_rotation")) {
                for (const Node* p = FindChild(pr, "personality"); p; p = NextSibling(pr, p, "personality"))
                    if (p->text()) t.personalityRotation.push_back(*p->text());
            }
            r.perTeamInfo.push_back(t);
        }
    }
    return r;
}

std::vector<SpawnRank> ParseSpawnInfoRanksTable(const Document& doc) {
    std::vector<SpawnRank> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Rank"); row; row = NextSibling(table, row, "Rank"))
        out.push_back(ParseSpawnRank(row));
    return out;
}

// ===========================================================================
// drunk_levels.xtbl - spec-tables-progression.md S10.7
// ===========================================================================

DrunkLevelsEntry ParseDrunkLevelsEntry(const Node* row) {
    DrunkLevelsEntry e;
    e.drugType = OptText(row, "Drug_Type");
    e.maxBoozePoints = ReadFloatAlways(row, "Max_Booze_Points");
    e.maxTimeDrunk = ReadFloatAlways(row, "Max_Time_Drunk");
    if (const Node* levels = FindChild(row, "Levels")) {
        const Node* lvl = FindChild(levels, "Level");
        int kept = 0;
        while (lvl && kept < 5) {
            DrunkLevel dl;
            dl.percentDrunk = ReadFloatAlways(lvl, "Percent_drunk");
            dl.cameraRotationMult = ReadFloatAlways(lvl, "Camera_rotation_mult");
            dl.cameraPitchMult = ReadFloatAlways(lvl, "Camera_pitch_mult");
            dl.randomInputSwitchTime = ReadInt32Always(lvl, "Random_input_switch_time");
            dl.randomInputAmountFoot = ReadFloatAlways(lvl, "Random_input_amount_foot");
            dl.randomInputAmountVehicle = ReadFloatAlways(lvl, "Random_input_amount_vehicle");
            dl.controlDelayOnFoot = ReadInt32Always(lvl, "Control_delay_on_foot");
            dl.controlDelayVehicle = ReadInt32Always(lvl, "Control_delay_vehicle");
            dl.ragdollOnImpactTime = ReadInt32Always(lvl, "Ragdoll_on_impact_time");
            dl.reticleXMaxOffset = ReadFloatAlways(lvl, "Reticle_x_max_offset");
            dl.reticleYMaxOffset = ReadFloatAlways(lvl, "Reticle_y_max_offset");
            dl.reticleXSpeed = ReadFloatAlways(lvl, "Reticle_x_speed");
            dl.reticleYSpeed = ReadFloatAlways(lvl, "Reticle_y_speed");
            dl.sleepy = ReadBoolAlways(lvl, "Sleepy");
            e.levels.push_back(dl);
            ++kept;
            lvl = NextSibling(levels, lvl, "Level");
        }
    }
    return e;
}

std::vector<DrunkLevelsEntry> ParseDrunkLevelsTable(const Document& doc) {
    std::vector<DrunkLevelsEntry> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Drunk_Levels"); row; row = NextSibling(table, row, "Drunk_Levels"))
        out.push_back(ParseDrunkLevelsEntry(row));
    return out;
}

// ===========================================================================
// rank_reactions.xtbl - spec-tables-progression.md S10.8
// ===========================================================================

NpcGroup ParseNpcGroup(const Node* row) {
    NpcGroup g;
    g.name = OptText(row, "Name");
    if (const Node* reactions = FindChild(row, "Reactions")) {
        for (const Node* rp = FindChild(reactions, "ReactionPercentages"); rp; rp = NextSibling(reactions, rp, "ReactionPercentages")) {
            ReactionPercentages r;
            r.playerRank = OptText(rp, "PlayerRank");
            r.ignore = ReadInt32Always(rp, "Ignore");
            r.avoid = ReadInt32Always(rp, "Avoid");
            r.compliment = ReadInt32Always(rp, "Compliment");
            r.flipOff = ReadInt32Always(rp, "FlipOff");
            r.gangSign = ReadInt32Always(rp, "GangSign");
            r.sayAmbient = ReadInt32Always(rp, "SayAmbient");
            r.threaten = ReadInt32Always(rp, "Threaten");
            r.observe = ReadInt32Always(rp, "Observe");
            g.reactions.push_back(r);
        }
    }
    return g;
}

std::vector<NpcGroup> ParseRankReactionsTable(const Document& doc) {
    std::vector<NpcGroup> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "NPCGroup"); row; row = NextSibling(table, row, "NPCGroup"))
        out.push_back(ParseNpcGroup(row));
    return out;
}

// ===========================================================================
// default_global.xtbl - spec-tables-progression.md S10.9 + spec-tables-environment.md S15.5
// (see the header's banner comment for the two-loader-functions, HIGH-CONFIDENCE-same-file nuance)
// ===========================================================================

DefaultGlobal ParseDefaultGlobal(const Document& doc) {
    DefaultGlobal g;
    const Node* root = doc.root();
    // --- S10.9 (0x00bb70b0) ---
    if (const std::string* t = ChildText(root, "skybox_mesh_filename")) g.skyboxMeshFilename = *t;
    if (const std::string* t = ChildText(root, "cloud_mesh_filename")) g.cloudMeshFilename = *t;
    if (const Node* orbitals = FindChild(root, "orbitals")) {
        int kept = 0;
        for (const Node* o = FindChild(orbitals, "orbital"); o && kept < 15; o = NextSibling(orbitals, o, "orbital")) {
            if (const std::string* mn = ChildText(o, "map_name")) {
                if (!mn->empty()) {
                    g.orbitalMapNames.push_back(*mn);
                    ++kept;
                }
            }
        }
    }

    // --- spec-tables-environment.md S15.5 (0x00BB6400) ---
    if (auto v = OptYesTrueOneBool(root, "horizon_mountain_enabled")) g.horizonMountainEnabled = *v;
    if (auto v = OptYesTrueOneBool(root, "fog_camera_follow")) g.fogCameraFollow = *v;
    if (auto v = GetInt32(root, "day_begin")) g.dayBegin = *v;
    if (auto v = GetInt32(root, "day_end")) g.dayEnd = *v;
    // cloud_mesh_horizon_mat/overhead_mat/skyline_mat are children of the SAME cloud_mesh_filename
    // node resolved above for cloudMeshFilename (S15.5), not independent top-level elements.
    if (const Node* cmf = FindChild(root, "cloud_mesh_filename")) {
        if (const std::string* t = ChildText(cmf, "cloud_mesh_horizon_mat")) g.cloudMeshHorizonMat = *t;
        if (const std::string* t = ChildText(cmf, "cloud_mesh_overhead_mat")) g.cloudMeshOverheadMat = *t;
        if (const std::string* t = ChildText(cmf, "cloud_mesh_skyline_mat")) g.cloudMeshSkylineMat = *t;
    }
    // tod_segments/segment: no real numeric capacity is stated anywhere in either spec (S15.5 gives
    // only the capacity's memory address, 0x02915300) - see the header comment - so every <segment>
    // child present is kept, uncapped.
    if (const Node* segs = FindChild(root, "tod_segments")) {
        for (const Node* s = FindChild(segs, "segment"); s; s = NextSibling(segs, s, "segment")) {
            if (auto v = GetInt32(s)) g.todSegments.push_back(*v);
        }
    }
    return g;
}

// ===========================================================================
// tweak_table.xtbl - spec-tables-progression.md S10.10
// ===========================================================================

TweakTableEntry ParseTweakTableEntry(const Node* row) {
    TweakTableEntry e;
    if (const std::string* n = ChildText(row, "Name")) e.name = *n;
    e.value = ReadFloatAlways(row, "Value");
    e.description = OptText(row, "Description");
    e.framework = OptText(row, "Framework");
    return e;
}

std::vector<TweakTableEntry> ParseTweakTable(const Document& doc) {
    std::vector<TweakTableEntry> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Tweak_Table_Entry"); row; row = NextSibling(table, row, "Tweak_Table_Entry"))
        out.push_back(ParseTweakTableEntry(row));
    return out;
}

// ===========================================================================
// metered_sprint.xtbl - spec-tables-progression.md S10.11
// ===========================================================================

SprintEntry ParseSprintEntry(const Node* row) {
    SprintEntry e;
    e.name = OptText(row, "Name");
    e.useTime = ReadInt32Always(row, "UseTime");
    e.rechargeTime = ReadInt32Always(row, "RechargeTime");
    e.delayTime = ReadInt32Always(row, "DelayTime");
    e.pantPercentage = ReadFloatAlways(row, "PantPercentage");
    e.jumpPenalty = ReadFloatAlways(row, "JumpPenalty");
    e.startPenalty = ReadFloatAlways(row, "StartPenalty");
    return e;
}

std::vector<SprintEntry> ParseMeteredSprintTable(const Document& doc) {
    std::vector<SprintEntry> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "SprintEntry"); row; row = NextSibling(table, row, "SprintEntry"))
        out.push_back(ParseSprintEntry(row));
    return out;
}

// ===========================================================================
// activity_types.xtbl - spec-tables-progression.md S10.12
// ===========================================================================

Activity ParseActivity(const Node* row) {
    Activity a;
    if (const std::string* n = ChildText(row, "Name")) a.name = *n;
    a.framework = OptText(row, "Framework");
    if (const Node* uns = FindChild(row, "Unlockables")) {
        for (const Node* u = FindChild(uns, "Unlockable"); u; u = NextSibling(uns, u, "Unlockable"))
            if (u->text()) a.unlockableNames.push_back(*u->text());
    }
    if (const Node* ci = FindChild(row, "Completion_Image")) {
        if (const std::string* fn = ChildText(ci, "Filename")) {
            const std::string& name = *fn;
            size_t dot = name.rfind('.');
            a.completionImageName = (dot == std::string::npos) ? name : name.substr(0, dot);
        }
    }
    if (const Node* tf = FindChild(row, "Activity_type_flags")) {
        a.typeFlags.statsDoesntCount = HasFlag(tf, "stats doesnt count");
        a.typeFlags.keepScreenFaded = HasFlag(tf, "keep screen faded");
        a.typeFlags.autoAdvanceLevel = HasFlag(tf, "auto advance level");
        a.typeFlags.removeNoterietySpawns = HasFlag(tf, "remove noteriety spawns");
        a.typeFlags.resetNoterietyEachLevel = HasFlag(tf, "reset noteriety each level");
        a.typeFlags.usesButtonMashingInterface = HasFlag(tf, "uses button mashing interface");
        a.typeFlags.failOnDeath = HasFlag(tf, "fail on death");
        a.typeFlags.finisherOnlyDeath = HasFlag(tf, "finisher only death");
        a.typeFlags.noVehicleEject = HasFlag(tf, "no vehicle eject");
        a.typeFlags.doInitialWarp = HasFlag(tf, "do initial warp");
        a.typeFlags.racing = HasFlag(tf, "racing");
    }
    if (const Node* df = FindChild(row, "Disable_flags")) {
        ActivityDisableFlags flags;
        flags.turnOffSpawning = HasFlag(df, "turn off spawning");
        flags.disableDistantSpawns = HasFlag(df, "disable distant spawns");
        flags.disableParkingSpawns = HasFlag(df, "disable parking spawns");
        flags.disableAllStores = HasFlag(df, "disable all stores");
        flags.disableCrib = HasFlag(df, "disable crib");
        flags.disableHud = HasFlag(df, "disable HUD");
        flags.allowCopsToShootFromVehicle = HasFlag(df, "allow cops to shoot from vehicle");
        flags.disableHelicopters = HasFlag(df, "disable helicopters");
        flags.disableAttackHelis = HasFlag(df, "disable attack helis");
        flags.disablePlayerSwapCheats = HasFlag(df, "disable player swap cheats");
        flags.disableWarpTriggers = HasFlag(df, "disable warp triggers");
        flags.disableRoadblocks = HasFlag(df, "disable roadblocks");
        a.disableFlags = flags;
    }
    a.soundbankName = OptText(row, "Soundbank_Name");
    return a;
}

std::vector<Activity> ParseActivityTypesTable(const Document& doc) {
    std::vector<Activity> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Activity"); row; row = NextSibling(table, row, "Activity"))
        out.push_back(ParseActivity(row));
    return out;
}


} // namespace sr3tables_progression

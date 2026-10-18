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
// cheats.xtbl - spec-tables-progression.md S8
// ===========================================================================

const char* const kCheatAiActions[4] = {"Evil Cars", "Evil Cars 2", "Ped War", "Drunk"};

const char* const kCheatGameplayPhysicsActions[27] = {
    "Instant Cash (+$100000)", "Instant Respect (+100000)", "God Mode", "Golden Gun",
    "Remove Police Notoriety", "Remove Gang Notoriety", "Add Police Notoriety", "Add Gang Notoriety",
    "Infinite Sprint", "Max Health", "Player Pratfalls", "Super Beer Muscles",
    "Giant Player", "Tiny Player", "Bushwick Bill", "Raining Peds",
    "Never Die", "Super Saints", "Unlimited Clip", "Super Explosions",
    "Low Gravity", "Elevator to Heaven", "Hide Hud", "Bloody Mess",
    "Zombie Peds", "Mascot Peds", "Pimps and Hos Peds",
};

Cheat ParseCheat(const Node* row) {
    Cheat c;
    if (const std::string* n = ChildText(row, "Name")) c.name = *n;
    c.unlockString = OptText(row, "UnlockString");
    c.displayName = OptText(row, "DisplayName");
    c.cheatDescription = OptText(row, "Cheat_Description");
    c.interfaceCategory = OptText(row, "Interface_Category");
    c.dontFlagAsCheating = ReadBoolAlways(row, "Dont_Flag_As_Cheating");
    c.isDlc = ReadBoolAlways(row, "Is_DLC");

    if (const Node* typeNode = FindChild(row, "Type")) {
        if (const Node* ai = FindChild(typeNode, "AI")) {
            c.typeName = "AI";
            c.aiAction = OptText(ai, "Action");
        } else if (const Node* gp = FindChild(typeNode, "Gameplay_Physics")) {
            c.typeName = "Gameplay_Physics";
            c.gameplayPhysicsAction = OptText(gp, "Action");
        } else if (const Node* tm = FindChild(typeNode, "Time")) {
            c.typeName = "Time";
            CheatTimeAction ta;
            if (const Node* act = FindChild(tm, "Action")) {
                if (const Node* sh = FindChild(act, "Set_Hour")) ta.setHour = GetInt32(sh, "Hour");
                ta.stopTime = FindChild(act, "Stop_Time") != nullptr;
                ta.todCycle = FindChild(act, "TOD_Cycle") != nullptr;
            }
            c.timeAction = ta;
        } else if (const Node* veh = FindChild(typeNode, "Vehicles")) {
            c.typeName = "Vehicles";
            CheatVehiclesAction va;
            if (const Node* act = FindChild(veh, "Action")) {
                if (const Node* dr = FindChild(act, "Drop")) {
                    va.hasDrop = true;
                    va.dropVehicleName = OptText(dr, "Vehicle_Name");
                    va.dropVehicleVariant = OptText(dr, "Vehicle_Variant");
                }
                va.infiniteMass = FindChild(act, "Infinite_Mass") != nullptr;
                va.repairVehicle = FindChild(act, "Repair_Vehicle") != nullptr;
                va.playerVehicleNoDamage = FindChild(act, "Player_Vehicle_No_Damage") != nullptr;
                va.playerVehicleSmash = FindChild(act, "Player_Vehicle_Smash") != nullptr;
            }
            c.vehiclesAction = va;
        } else if (const Node* wp = FindChild(typeNode, "Weapon")) {
            c.typeName = "Weapon";
            CheatWeaponAction wa;
            if (const Node* act = FindChild(wp, "Action")) {
                if (const Node* gv = FindChild(act, "Give")) {
                    wa.hasGive = true;
                    wa.giveWeapon = OptText(gv, "Weapon");
                    wa.giveAmmo = GetInt32(gv, "Ammo");
                }
                wa.infiniteAmmo = FindChild(act, "Infinite_Ammo") != nullptr;
                wa.allWeapons = FindChild(act, "All_Weapons") != nullptr;
            }
            c.weaponAction = wa;
        } else if (const Node* we = FindChild(typeNode, "Weather")) {
            c.typeName = "Weather";
            CheatWeatherAction wthr;
            if (const Node* act = FindChild(we, "Action")) {
                wthr.clear = FindChild(act, "Clear") != nullptr;
                if (const Node* st = FindChild(act, "Set_To")) wthr.setToConditions = OptText(st, "Conditions");
                wthr.wrathOfGod = FindChild(act, "Wrath_of_God") != nullptr;
            }
            c.weatherAction = wthr;
        }
    }
    return c;
}

std::vector<Cheat> ParseCheatsTable(const Document& doc) {
    std::vector<Cheat> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Cheats"); row; row = NextSibling(table, row, "Cheats"))
        out.push_back(ParseCheat(row));
    return out;
}

// ===========================================================================
// collectibles.xtbl - spec-tables-progression.md S9
// ===========================================================================

const char* const kCollectibleNames[4] = {"Drug Package", "Money Pallet", "Sex Doll", "Photo Op"};

Collectible ParseCollectible(const Node* row) {
    Collectible c;
    if (const std::string* n = ChildText(row, "Name")) c.name = *n;
    c.titleMessage = OptText(row, "Title_Message");
    c.subtitleMessage = OptText(row, "Subtitle_Message");
    c.respectAward = GetInt32(row, "Respect_Award");
    c.cashAward = GetFloat(row, "Cash_Award");
    if (const Node* trs = FindChild(row, "Threshold_Rewards")) {
        const Node* tr = FindChild(trs, "Threshold_Reward");
        int kept = 0;
        while (tr && kept < 6) {
            ThresholdReward t;
            t.threshold = ReadInt32Always(tr, "Threshold");
            t.respectAward = ReadInt32Always(tr, "Respect_Award");
            t.cashAward = ReadFloatAlways(tr, "Cash_Award");
            t.unlockable = OptText(tr, "Unlockable");
            c.thresholdRewards.push_back(t);
            ++kept;
            tr = NextSibling(trs, tr, "Threshold_Reward");
        }
    }
    return c;
}

std::vector<Collectible> ParseCollectiblesTable(const Document& doc) {
    std::vector<Collectible> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "collectible"); row; row = NextSibling(table, row, "collectible"))
        out.push_back(ParseCollectible(row));
    return out;
}

// ===========================================================================
// store_discounts.xtbl - spec-tables-progression.md S10.1
// ===========================================================================

StoreDiscount ParseStoreDiscount(const Node* row) {
    StoreDiscount d;
    if (const std::string* n = ChildText(row, "Name")) d.name = *n;
    if (const Node* discs = FindChild(row, "Discounts")) {
        for (const Node* de = FindChild(discs, "DiscountElement"); de; de = NextSibling(discs, de, "DiscountElement")) {
            StoreDiscountElement e;
            e.discountName = OptText(de, "DiscountName");
            e.amount = GetFloat(de, "Amount");
            e.radioEvent = OptText(de, "RadioEvent");
            e.hours = GetUInt32(de, "Hours");
            e.triggered = GetFloat(de, "Triggered");
            d.discounts.push_back(e);
        }
    }
    return d;
}

std::vector<StoreDiscount> ParseStoreDiscountsTable(const Document& doc) {
    std::vector<StoreDiscount> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "StoreDiscounts"); row; row = NextSibling(table, row, "StoreDiscounts"))
        out.push_back(ParseStoreDiscount(row));
    return out;
}

// ===========================================================================
// gameplay_constants.xtbl - spec-tables-progression.md S10.2
// ===========================================================================

std::optional<GameplayConstants> ParseGameplayConstants(const Document& doc) {
    const Node* table = doc.table();
    const Node* gc = FindChild(table, "Gameplay_Constants");
    if (!gc) return std::nullopt;
    GameplayConstants c;

    c.panicFallVelocity = ReadFloatAlways(gc, "panic_fall_velocity");
    c.friendlyFirePercentage = ReadFloatAlways(gc, "FriendlyFirePercentage");

    if (const Node* n = FindChild(gc, "Death_and_busted")) c.respawnDelayMs = ReadInt32Always(n, "respawn_delay_ms");

    if (const Node* n = FindChild(gc, "Fat_bones")) {
        c.fatBones.arm = ReadFloatAlways(n, "Fat_Bones_Arm");
        c.fatBones.leg = ReadFloatAlways(n, "Fat_Bones_Leg");
    }
    if (const Node* n = FindChild(gc, "Fire")) {
        c.fire.maxPlayerBurnTimeMs = ReadInt32Always(n, "Max_Player_Burn_Time_Ms");
        c.fire.maxNpcBurnTimeMs = ReadInt32Always(n, "Max_npc_burn_time_ms");
        c.fire.maxCorpseBurnTimeMs = ReadInt32Always(n, "Max_corpse_burn_time_ms");
        c.fire.mpFireDamagePerSecond = ReadFloatAlways(n, "MP_fire_damage_per_second");
        c.fire.mpMaxPlayerBurnTimeMs = ReadInt32Always(n, "MP_max_player_burn_time_ms");
        c.fire.mpMaxNpcBurnTimeMs = ReadInt32Always(n, "MP_max_npc_burn_time_ms");
        c.fire.mpMaxCorpseBurnTimeMs = ReadInt32Always(n, "MP_max_corpse_burn_time_ms");
    }
    if (const Node* n = FindChild(gc, "Dripping_Wet")) {
        c.drippingWet.maxPlayerDripTimeMs = ReadInt32Always(n, "Max_Player_Drip_Time_ms");
        c.drippingWet.maxNpcDripTimeMs = ReadInt32Always(n, "Max_NPC_Drip_Time_ms");
        c.drippingWet.maxCorpseDripTimeMs = ReadInt32Always(n, "Max_Corpse_Drip_Time_ms");
        c.drippingWet.mpMaxPlayerDripTimeMs = ReadInt32Always(n, "Multiplayer_Max_Player_Drip_Time_ms");
        c.drippingWet.mpMaxNpcDripTimeMs = ReadInt32Always(n, "Multiplayer_Max_NPC_Drip_Time_ms");
        c.drippingWet.mpMaxCorpseDripTimeMs = ReadInt32Always(n, "Multiplayer_Max_Corpse_Drip_Time_ms");
    }
    if (const Node* n = FindChild(gc, "sticky_fire")) {
        c.stickyFire.conicSpreadThreshold = ReadFloatAlways(n, "conic_spread_threshold");
        c.stickyFire.conicStretchFactor = ReadFloatAlways(n, "conic_stretch_factor");
        c.stickyFire.circularStretchFactor = ReadFloatAlways(n, "circular_stretch_factor");
        c.stickyFire.conicArc = ReadFloatAlways(n, "conic_arc");
        if (const Node* br = FindChild(n, "burn_radii")) {
            c.stickyFire.burnRadii.large = ReadFloatAlways(br, "large");
            c.stickyFire.burnRadii.small1 = ReadFloatAlways(br, "small_1");
            c.stickyFire.burnRadii.small2 = ReadFloatAlways(br, "small_2");
        }
    }
    if (const Node* n = FindChild(gc, "player_health")) {
        c.playerHealth.restorePerSecSp = ReadFloatAlways(n, "restore_per_sec_sp");
        c.playerHealth.restorePerSecVampireSp = ReadFloatAlways(n, "restore_per_sec_vampire_sp");
        c.playerHealth.restoreMaxPctSp = ReadFloatAlways(n, "restore_max_pct_sp");
        c.playerHealth.restorePerSecMp = ReadFloatAlways(n, "restore_per_sec_mp");
        c.playerHealth.restorePerSecVehicleMp = ReadFloatAlways(n, "restore_per_sec_vehicle_mp");
        c.playerHealth.restoreMaxPctMp = ReadFloatAlways(n, "restore_max_pct_mp");
        c.playerHealth.restoreWaitTimeMs = ReadInt32Always(n, "restore_wait_time_ms");
        c.playerHealth.restoreWaitTimeMsMp = ReadInt32Always(n, "restore_wait_time_ms_mp");
        c.playerHealth.fullRestoreTimeMs = ReadInt32Always(n, "full_restore_time_ms");
    }
    if (const Node* n = FindChild(gc, "Object_Glow_Colors")) {
        auto readColor = [](const Node* p, const char* name) {
            GcColor col;
            Vec3Result r = ReadVec3Child(p, name);
            col.x = r.x;
            col.y = r.y;
            col.z = r.z;
            return col;
        };
        c.objectGlowColors.weapons = readColor(n, "Weapons");
        c.objectGlowColors.cash = readColor(n, "Cash");
        c.objectGlowColors.drugs = readColor(n, "Drugs");
    }
    if (const Node* n = FindChild(gc, "Ho_Pimp_AI")) {
        c.hoPimpAi.slapChance = ReadFloatAlways(n, "Slap_Chance");
        c.hoPimpAi.customerSellChance = ReadFloatAlways(n, "Customer_Sell_Chance");
        c.hoPimpAi.goCustomerChance = ReadFloatAlways(n, "Go_Customer_Chance");
    }
    if (const Node* n = FindChild(gc, "ragdoll_damage_factors")) {
        auto& r = c.ragdollDamageFactors;
        r.global = ReadFloatAlways(n, "global");
        r.normal = ReadFloatAlways(n, "normal");
        r.sliding = ReadFloatAlways(n, "sliding");
        r.head = ReadFloatAlways(n, "head");
        r.upperBody = ReadFloatAlways(n, "upper_body");
        r.body = ReadFloatAlways(n, "body");
        r.upperArm = ReadFloatAlways(n, "upper_arm");
        r.lowerArm = ReadFloatAlways(n, "lower_arm");
        r.upperLeg = ReadFloatAlways(n, "upper_leg");
        r.lowerLeg = ReadFloatAlways(n, "lower_leg");
        r.damageAgainstMoverMultiplier = ReadFloatAlways(n, "Damage_Against_Mover_Multiplier");
        r.damageAgainstMoverMinimumHp = ReadFloatAlways(n, "Damage_Against_Mover_Minimum_HP");
    }
    if (const Node* n = FindChild(gc, "Melee_attack")) {
        if (const Node* speeds = FindChild(n, "Melee_attack_speeds")) {
            for (const Node* e = FindChild(speeds, "Melee_attack_speed_element"); e; e = NextSibling(speeds, e, "Melee_attack_speed_element")) {
                GcMeleeAttackSpeedElement el;
                el.numAttackers = ReadInt32Always(e, "Num_attackers");
                el.attackDelayMin = ReadInt32Always(e, "Attack_delay_min");
                el.attackDelayMax = ReadInt32Always(e, "Attack_delay_max");
                el.swordAttackDelayMin = ReadInt32Always(e, "Sword_Attack_Delay_Min");
                el.swordAttackDelayMax = ReadInt32Always(e, "Sword_Attack_Delay_Max");
                c.meleeAttack.attackSpeeds.push_back(el);
            }
        }
        c.meleeAttack.gunMeleeAttackDelay = ReadUInt32Always(n, "Gun_melee_attack_delay");
        c.meleeAttack.finisherCameraChance = ReadFloatAlways(n, "Finisher_Camera_Chance");
        c.meleeAttack.wieldablePropPlayerDamageMult = ReadFloatAlways(n, "Wieldable_Prop_Player_Damage_Mult");
        c.meleeAttack.powerAttackAngleDegrees = ReadInt32Always(n, "Power_Attack_Angle");
    }
    if (const Node* n = FindChild(gc, "Firearm_attack")) {
        if (const Node* mins = FindChild(n, "Min_spread_multipliers")) {
            for (const Node* e = FindChild(mins, "Min_spread_multiplier_element"); e; e = NextSibling(mins, e, "Min_spread_multiplier_element")) {
                GcMinSpreadMultiplierElement el;
                el.attackerAdvantage = ReadInt32Always(e, "Attacker_advantage");
                el.minSpreadMultiplier = ReadFloatAlways(e, "Min_spread_multiplier");
                c.firearmAttack.minSpreadMultipliers.push_back(el);
            }
        }
        c.firearmAttack.dualWieldMultiplier = ReadFloatAlways(n, "dual_wield_multiplier");
        c.firearmAttack.dualWieldMultiplierOnline = ReadFloatAlways(n, "dual_wield_multiplier_online");
    }
    if (const Node* n = FindChild(gc, "ControlPatterns")) {
        c.controlPatterns.orthogonalArc = ReadUInt32Always(n, "OrthogonalArc");
        c.controlPatterns.centeredThreshold = ReadFloatAlways(n, "CenteredThreshold");
    }
    if (const Node* n = FindChild(gc, "TauntReactions")) {
        c.tauntReactions.responseFlee = ReadFloatAlways(n, "ResponseFlee");
        c.tauntReactions.responseTaunt = ReadFloatAlways(n, "ResponseTaunt");
        c.tauntReactions.responseAttack = ReadFloatAlways(n, "ResponseAttack");
    }
    if (const Node* n = FindChild(gc, "Bust_Offsets")) {
        auto readVec3 = [](const Node* p, const char* name) {
            GcVec3 v;
            Vec3Result r = ReadVec3Child(p, name);
            v.x = r.x;
            v.y = r.y;
            v.z = r.z;
            return v;
        };
        c.bustOffsets.bustFaceDownLeft = readVec3(n, "Bust_Face_Down_Left");
        c.bustOffsets.bustFaceDownRight = readVec3(n, "Bust_Face_Down_Right");
        c.bustOffsets.bustFaceUpLeft = readVec3(n, "Bust_Face_Up_Left");
        c.bustOffsets.bustFaceUpRight = readVec3(n, "Bust_Face_Up_Right");
        c.bustOffsets.bustStanding = readVec3(n, "Bust_Standing");
    }
    if (const Node* n = FindChild(gc, "Helicopter")) {
        if (const Node* d = FindChild(n, "Draft")) {
            c.helicopter.draft.height = ReadFloatAlways(d, "height");
            c.helicopter.draft.radius = ReadFloatAlways(d, "radius");
            c.helicopter.draft.forceX = ReadFloatAlways(d, "force_x");
            c.helicopter.draft.dislodgeDamageX = ReadFloatAlways(d, "dislodge_damage_x");
            c.helicopter.draft.pedFleeRadiusStart = ReadFloatAlways(d, "ped_flee_radius_start");
            c.helicopter.draft.pedFleeRadiusStop = ReadFloatAlways(d, "ped_flee_radius_stop");
        }
    }
    if (const Node* n = FindChild(gc, "Cribs")) {
        if (const Node* ms = FindChild(n, "Money_Storage")) {
            for (const Node* e = FindChild(ms, "Money_Storage_Element"); e; e = NextSibling(ms, e, "Money_Storage_Element")) {
                GcMoneyStorageElement el;
                el.numberOfCribs = ReadInt32Always(e, "Number_of_Cribs");
                el.maxStash = ReadInt32Always(e, "Max_Stash");
                c.cribs.moneyStorage.push_back(el);
            }
        }
    }
    if (const Node* n = FindChild(gc, "Combat_AI")) {
        // spec-tables-progression.md 10.2, CORRECTED 2026-10-01 (disassembly of 0x005F5390, job
        // 20261001T123123-team-a-ytgi): `Pepperspray`, `PepperSprayMinUsageDelay`, `StunGunMinUsageDelay`
        // and the three `Back_Away_*` leaves are all children of `Gun`, not siblings of it under
        // `Combat_AI` - "FOR TEAM B: ... are read from the Gun element; Gunfire_Evade and Bust from
        // Combat_AI". Previously coded (wrongly) as direct children of Combat_AI; fixed in place.
        if (const Node* g = FindChild(n, "Gun")) {
            c.combatAi.gun.repositionMin = ReadUInt32Always(g, "Reposition_Min");
            c.combatAi.gun.repositionMax = ReadUInt32Always(g, "Reposition_Max");
            c.combatAi.gun.cantFireRepositionMin = ReadUInt32Always(g, "Cant_Fire_Reposition_Min");
            c.combatAi.gun.cantFireRepositionMax = ReadUInt32Always(g, "Cant_Fire_Reposition_Max");
            if (const Node* p = FindChild(g, "Pepperspray")) {
                c.combatAi.pepperspray.sprayMin = ReadUInt32Always(p, "Spray_Min");
            }
            c.combatAi.pepperSprayMinUsageDelay = ReadUInt32Always(g, "PepperSprayMinUsageDelay");
            c.combatAi.stunGunMinUsageDelay = ReadUInt32Always(g, "StunGunMinUsageDelay");
            c.combatAi.backAwayMinDist = ReadFloatAlways(g, "Back_Away_Min_Dist");
            c.combatAi.backAwayMaxDist = ReadFloatAlways(g, "Back_Away_Max_Dist");
            c.combatAi.backAwayAbsMinDist = ReadFloatAlways(g, "Back_Away_Abs_Min_Dist");
        }
        if (const Node* ge = FindChild(n, "Gunfire_Evade")) {
            c.combatAi.gunfireEvade.cowerFleeChance = ReadFloatAlways(ge, "Cower_Flee_Chance");
            c.combatAi.gunfireEvade.cowerFleeMaxRank = ReadInt32Always(ge, "Cower_Flee_Max_Rank");
        }
        if (const Node* b = FindChild(n, "Bust")) {
            c.combatAi.bust.bustHpPcnt = ReadFloatAlways(b, "Bust_HP_Pcnt");
        }
    }
    if (const Node* n = FindChild(gc, "Vehicle_Evade_AI")) {
        c.vehicleEvadeAi.evadeChance = ReadFloatAlways(n, "Evade_Chance");
        c.vehicleEvadeAi.diveChance = ReadFloatAlways(n, "Dive_Chance");
        c.vehicleEvadeAi.threatChance = ReadFloatAlways(n, "Threat_Chance");
        c.vehicleEvadeAi.threatChanceAfterDive = ReadFloatAlways(n, "Threat_Chance_After_Dive");
    }
    if (const Node* n = FindChild(gc, "Idle_AI")) {
        if (const Node* cs = FindChild(n, "Crime_scene")) {
            c.idleAi.crimeScene.getCallersDelay = ReadInt32Always(cs, "Get_callers_delay");
            c.idleAi.crimeScene.dontCallDelay = ReadInt32Always(cs, "Dont_call_delay");
            c.idleAi.crimeScene.policeResponseDelay = ReadInt32Always(cs, "Police_response_delay");
            c.idleAi.crimeScene.observeInterval = ReadInt32Always(cs, "Observe_interval");
            c.idleAi.crimeScene.addCopInterval = ReadInt32Always(cs, "Add_cop_interval");
            c.idleAi.crimeScene.radius = ReadFloatAlways(cs, "Radius");
        }
        if (const Node* dr = FindChild(n, "Drunk")) {
            c.idleAi.drunk.drunkKnockdownChance = ReadInt32Always(dr, "drunk_knockdown_chance");
            c.idleAi.drunk.drunkActionDelayMin = ReadInt32Always(dr, "drunk_action_delay_min");
            c.idleAi.drunk.drunkActionDelayMax = ReadInt32Always(dr, "drunk_action_delay_max");
            if (const Node* dat = FindChild(dr, "Drunk_action_type")) {
                c.idleAi.drunk.drunkActionType.drunkTauntPct = ReadInt32Always(dat, "drunk_taunt_pct");
                c.idleAi.drunk.drunkActionType.drunkStumblePct = ReadInt32Always(dat, "drunk_stumble_pct");
                c.idleAi.drunk.drunkActionType.drunkVomitPct = ReadInt32Always(dat, "drunk_vomit_pct");
                c.idleAi.drunk.drunkActionType.drunkStandPct = ReadInt32Always(dat, "drunk_stand_pct");
                c.idleAi.drunk.drunkActionType.drinkMorePct = ReadInt32Always(dat, "drink_more_pct");
            }
        }
        if (const Node* sw = FindChild(n, "Sidewalk")) {
            c.idleAi.sidewalk.playerBlockingComplainMs = ReadInt32Always(sw, "Player_Blocking_Complain_Ms");
            c.idleAi.sidewalk.playerBlockingReactMs = ReadInt32Always(sw, "Player_Blocking_React_Ms");
        }
    }
    if (const Node* n = FindChild(gc, "PepperSpray")) {
        c.pepperSpray.sprayMeterReact = ReadFloatAlways(n, "SprayMeterReact");
        c.pepperSpray.sprayMeterMax = ReadFloatAlways(n, "SprayMeterMax");
        c.pepperSpray.playerSprayIncRate = ReadFloatAlways(n, "PlayerSprayIncRate");
        c.pepperSpray.playerSprayDecRate = ReadFloatAlways(n, "PlayerSprayDecRate");
        c.pepperSpray.sprayIncRate = ReadFloatAlways(n, "SprayIncRate");
        c.pepperSpray.sprayDecRate = ReadFloatAlways(n, "SprayDecRate");
    }
    if (const Node* n = FindChild(gc, "Fight_Club")) {
        if (const Node* fr = FindChild(n, "Finisher_Rates")) {
            for (const Node* e = FindChild(fr, "Finisher_Rate"); e; e = NextSibling(fr, e, "Finisher_Rate")) {
                GcFinisherRateElement el;
                el.numFailures = ReadInt32Always(e, "Num_Failures");
                el.npc = ReadFloatAlways(e, "NPC");
                el.player = ReadFloatAlways(e, "Player");
                c.fightClub.finisherRates.push_back(el);
            }
        }
        c.fightClub.healthWeight = ReadFloatAlways(n, "Health_Weight");
        c.fightClub.maxImbalance = ReadInt32Always(n, "Max_Imbalance");
        c.fightClub.minImbalance = ReadInt32Always(n, "Min_Imbalance");
    }
    if (const Node* n = FindChild(gc, "Coop_Meta_Game")) {
        c.coopMetaGame.rewardCash = ReadFloatAlways(n, "Reward_Cash");
        c.coopMetaGame.pointsPerPlayerDeath = ReadInt32Always(n, "Points_Per_Player_Death");
        c.coopMetaGame.pointsPerGangVehicle = ReadInt32Always(n, "_Gang_Vehicle");
        c.coopMetaGame.pointsPerHomieRevive = ReadInt32Always(n, "_Homie_Revive");
        c.coopMetaGame.pointsPerMissionObjective = ReadInt32Always(n, "_Mission_Objective");
        c.coopMetaGame.pointsPerHeadshot = ReadInt32Always(n, "_Headshot");
        c.coopMetaGame.pointsPerNutshot = ReadInt32Always(n, "_Nutshot");
        c.coopMetaGame.pointsPerMeleeKill = ReadInt32Always(n, "_Melee_Kill");
        c.coopMetaGame.pointsPerExplosionKill = ReadInt32Always(n, "_Explosion_Kill");
        c.coopMetaGame.pointsPerQuickKill = ReadInt32Always(n, "_Quick_Kill");
    }
    if (const Node* n = FindChild(gc, "Wieldable_Prop_Throw")) {
        c.wieldablePropThrow.basketballMultiplierLow = ReadFloatAlways(n, "Basketball_Multiplier_Low");
        c.wieldablePropThrow.basketballMultiplierHigh = ReadFloatAlways(n, "Basketball_Multiplier_High");
        c.wieldablePropThrow.h1hMult = ReadFloatAlways(n, "H1H_Mult");
        c.wieldablePropThrow.h2hMult = ReadFloatAlways(n, "H2H_Mult");
        c.wieldablePropThrow.h2hHighMult = ReadFloatAlways(n, "H2H_High_Mult");
        c.wieldablePropThrow.h2hLowMult = ReadFloatAlways(n, "H2H_Low_Mult");
    }
    if (const Node* n = FindChild(gc, "Watercraft_Params")) {
        c.watercraftParams.reducedSpeedPct = ReadFloatAlways(n, "Reduced_Speed_Pct");
        c.watercraftParams.reducedSpeedPctMin = ReadFloatAlways(n, "Reduced_Speed_Pct_Min");
        c.watercraftParams.reducedSpeedPctMax = ReadFloatAlways(n, "Reduced_Speed_Pct_Max");
    }
    if (const Node* n = FindChild(gc, "Siren_Whoop_Params")) {
        c.sirenWhoopParams.whoopMinCount = ReadInt32Always(n, "Whoop_Min_Count");
        c.sirenWhoopParams.whoopMaxCount = ReadInt32Always(n, "Whoop_Max_Count");
        c.sirenWhoopParams.honkWhoopChance = ReadFloatAlways(n, "Honk_Whoop_Chance");
        c.sirenWhoopParams.ramWhoopChance = ReadFloatAlways(n, "Ram_Whoop_Chance");
        c.sirenWhoopParams.hitVehicleWhoopChance = ReadFloatAlways(n, "Hit_Vehicle_Whoop_Chance");
        c.sirenWhoopParams.hitPedWhoopChance = ReadFloatAlways(n, "Hit_Ped_Whoop_Chance");
    }
    if (const Node* n = FindChild(gc, "Fall_Damage")) {
        c.fallDamage.minDistance = ReadFloatAlways(n, "Min_Distance");
        c.fallDamage.percentDamagePerMeter = ReadFloatAlways(n, "Percent_Damage_per_Meter");
        c.fallDamage.mpPercentDamagePerMeter = ReadFloatAlways(n, "MP_Percent_Damage_per_Meter");
    }

    return c;
}


} // namespace sr3tables_progression

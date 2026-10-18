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

namespace detail {

std::optional<std::string> OptText(const Node* n, std::string_view name) {
    const std::string* t = ChildText(n, name);
    if (!t) return std::nullopt;
    return *t;
}

// spec 6.1 (notoriety.xtbl Check_Detection) and spec 2.2/1.3 (stats.xtbl Allow_Update_By_Server,
// CORRECTED 2026-10-01): both fields' ONLY accepted spelling is the literal "true" (case-insensitive) -
// unlike sr3xtbl::ParseBool, "yes" does NOT count for either. Neither uses the generic four-literal
// ("true"/"yes"/"false"/"no") bool reader the rest of this table group's bool fields use.
bool TextEqualsTrue(const Node* n, std::string_view name) {
    const std::string* t = ChildText(n, name);
    return t != nullptr && NameEquals(*t, "true");
}

// spec-tables-environment.md S15.5 (default_global.xtbl, 0x00BB6400's bool reader 0x00DC51B0):
// "yes"/"true"/"1", case-insensitive; returns nullopt (caller applies ITS OWN default) only when
// the node/text is absent - a third bool-literal set in this table group, distinct from both
// sr3xtbl::ParseBool ("true"/"yes") and TextEqualsTrue ("true" only) above.
std::optional<bool> OptYesTrueOneBool(const Node* n, std::string_view name) {
    const std::string* t = ChildText(n, name);
    if (!t) return std::nullopt;
    return NameEquals(*t, "true") || NameEquals(*t, "yes") || NameEquals(*t, "1");
}

} // namespace detail

namespace {

// spec 4.4: the 61 <Type> child names, in the tested order (position ==
// typeIndex). Transcribed verbatim from the spec's own numbered table.
constexpr const char* kUnlockableTypeNames[61] = {
    "Vehicle", "Homie", "Crib_Weapon", "Store_Weapon", "Discount", "Crib_Customization_Discount",
    "Crib_Level", "Clothes", "Sprint_Bonus", "Damage_Resist", "Notoriety", "Health", "Crib", "Music",
    "Repair_Discount", "Mission_Stronghold", "Custom", "Gang_Customization", "Gang_Vehicle_Customization",
    "Melee_Damage_Bonus", "Unlimited_Crib_Ammo", "Ammo_Multiplier", "No_Reloading", "Firearm_Accuracy",
    "Gang_Taunts", "Taunts", "Compliments", "Outfit", "Free_Driveup_Homie", "Pickpocket", "Dual_Wield",
    "Weekly_Payments", "Pay_Cash_for_Respect", "Lump_Sum_of_Money", "Vehicle_Customization",
    "Character_Customization", "Gang_Customization_Unlock", "Respect_Bonus_Modifier", "Cash_Bonus_Modifier",
    "NPC_Cash_Drop_Modifier", "DBNO_Extra_Time", "Revive_Hold_Time_Modifier", "Homie_Health_Bonus",
    "Special_Homie_Health_Bonus", "Player_Health_Bonus", "Muscles", "Weapon_Customization_Discount",
    "Reminder", "Auto_Complete_City_Takeover_District", "Auto_Complete_City_Takeover_All",
    "Explosive_No_Ragdoll", "Gang_Weapons", "Vampire", "Customization_Items", "Bloody_Mess",
    "Reload_Speed_Bonus", "Immune_To_Flat_Tires", "Collectable_Finder", "Crib_Cash_Limit_Scalar",
    "Crib_Cash_Stronghold_Scalar", "Crib_Ammo",
};

} // namespace

// ===========================================================================
// stats.xtbl - spec-tables-progression.md S2
// ===========================================================================

Stat ParseStat(const Node* row) {
    Stat s;
    if (const std::string* n = ChildText(row, "Name")) s.name = *n;
    s.displayName = OptText(row, "DisplayName");
    if (const Node* vt = FindChild(row, "Value_Type")) {
        if (FindChild(vt, "integer")) {
            s.valueKind = StatValueKind::Integer;
        } else if (FindChild(vt, "float")) {
            s.valueKind = StatValueKind::Float;
        } else if (FindChild(vt, "distance")) {
            s.valueKind = StatValueKind::Distance;
        } else if (FindChild(vt, "time")) {
            s.valueKind = StatValueKind::Time;
        } else if (FindChild(vt, "money")) {
            s.valueKind = StatValueKind::Money;
        } else if (FindChild(vt, "boolean")) {
            s.valueKind = StatValueKind::Boolean;
        } else if (const Node* pct = FindChild(vt, "percent")) {
            s.valueKind = StatValueKind::Percent;
            s.percentageOfStat = OptText(pct, "Percentage_Of_Stat");
        } else if (FindChild(vt, "complex")) {
            s.valueKind = StatValueKind::Complex;
        } else {
            s.valueKind = StatValueKind::None;
        }
    }
    // spec 2.2/1.3, CORRECTED 2026-10-01: compared to the single literal "true" (case-insensitive),
    // NOT the generic four-literal bool reader - "yes" does not count (same mechanism as Check_Detection).
    s.allowUpdateByServer = TextEqualsTrue(row, "Allow_Update_By_Server");
    s.livePropertyId = GetInt32(row, "LivePropertyID");
    s.liveLeaderboardId = GetInt32(row, "LiveLeaderboardID");
    return s;
}

std::vector<Stat> ParseStatsTable(const Document& doc) {
    std::vector<Stat> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Stat"); row; row = NextSibling(table, row, "Stat"))
        out.push_back(ParseStat(row));
    return out;
}

// ===========================================================================
// achievements.xtbl - spec-tables-progression.md S3
// ===========================================================================

Achievement ParseAchievement(const Node* row) {
    Achievement a;
    if (const std::string* n = ChildText(row, "Name")) a.name = *n;
    a.displayName = OptText(row, "DisplayName");
    a.image = OptText(row, "Image");
    if (const Node* reqs = FindChild(row, "Requirements")) {
        for (const Node* r = FindChild(reqs, "Requirement"); r; r = NextSibling(reqs, r, "Requirement")) {
            AchievementRequirement req;
            req.stat = OptText(r, "Stat");
            if (const std::string* c = ChildText(r, "Condition")) {
                if (NameEquals(*c, "at least")) req.condition = RequirementCondition::AtLeast;
                else if (NameEquals(*c, "at most")) req.condition = RequirementCondition::AtMost;
            }
            req.valueAsInt = GetInt32(r, "Value");
            req.valueAsFloat = GetFloat(r, "Value");
            a.requirements.push_back(req);
        }
    }
    a.hudUpdateFrequency = ReadInt32Always(row, "HudUpdateFrequency");
    a.hudUpdateNumSuppress = ReadBoolAlways(row, "HudUpdateNumSuppress");
    a.avatarAwardPresent = FindChild(row, "Avatar_award") != nullptr;
    a.framework = OptText(row, "Framework");
    return a;
}

std::vector<Achievement> ParseAchievementsTable(const Document& doc) {
    std::vector<Achievement> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Achievement"); row; row = NextSibling(table, row, "Achievement"))
        out.push_back(ParseAchievement(row));
    return out;
}

// ===========================================================================
// unlockables.xtbl / patch_unlockables.xtbl / *_unlockables.xtbl - spec S4
// ===========================================================================

Unlockable ParseUnlockable(const Node* row) {
    Unlockable u;
    if (const std::string* n = ChildText(row, "Name")) u.name = *n;

    if (const Node* typeNode = FindChild(row, "Type")) {
        for (int i = 0; i < 61; ++i) {
            const Node* child = FindChild(typeNode, kUnlockableTypeNames[i]);
            if (!child) continue;
            u.typeIndex = i;
            u.typeName = kUnlockableTypeNames[i];
            switch (i) {
                case 0: // Vehicle: <Vehicle><Vehicles><Vehicle><Type/><Variant/></Vehicle>...</Vehicles></Vehicle>
                    if (const Node* vehicles = FindChild(child, "Vehicles")) {
                        for (const Node* v = FindChild(vehicles, "Vehicle"); v; v = NextSibling(vehicles, v, "Vehicle")) {
                            UnlockableVehicleEntry e;
                            if (const std::string* t = ChildText(v, "Type")) e.vehicleName = *t;
                            e.variant = OptText(v, "Variant");
                            u.vehicleEntries.push_back(e);
                        }
                    }
                    break;
                case 1: u.homieName = OptText(child, "Name"); break;
                case 2: u.cribWeaponName = OptText(child, "Name"); break;
                case 3: u.storeWeaponName = OptText(child, "Name"); break;
                case 4: { // Discount
                    UnlockableDiscountPayload d;
                    if (auto s = OptText(child, "name")) d.shopNames.push_back(*s);
                    for (int k = 2; k <= 8; ++k) {
                        std::string nm = "name_" + std::to_string(k);
                        if (auto s = OptText(child, nm)) d.shopNames.push_back(*s);
                    }
                    d.amount = GetFloat(child, "Amount");
                    u.discount = d;
                    break;
                }
                case 5: u.cribCustomizationDiscountAmount = GetFloat(child, "Amount"); break;
                case 6: { // Crib_Level
                    UnlockableCribLevelPayload p;
                    p.cribName = OptText(child, "Crib_Name");
                    p.level = GetInt32(child, "Level");
                    u.cribLevel = p;
                    break;
                }
                case 7: // Clothes: <Items><Item><ItemName/><ItemVariant/><Color1/2/3/></Item>...</Items>
                    if (const Node* items = FindChild(child, "Items")) {
                        for (const Node* it = FindChild(items, "Item"); it; it = NextSibling(items, it, "Item")) {
                            UnlockableClothesItem ci;
                            ci.itemName = OptText(it, "ItemName");
                            ci.itemVariant = OptText(it, "ItemVariant");
                            ci.color1 = OptText(it, "Color1");
                            ci.color2 = OptText(it, "Color2");
                            ci.color3 = OptText(it, "Color3");
                            u.clothesItems.push_back(ci);
                        }
                    }
                    break;
                case 8: { // Sprint_Bonus
                    UnlockableSprintBonusPayload p;
                    p.amount = GetInt32(child, "Amount");
                    p.level = GetInt32(child, "Level");
                    u.sprintBonus = p;
                    break;
                }
                case 9: { // Damage_Resist
                    UnlockableDamageResistPayload p;
                    p.source = OptText(child, "Source");
                    p.resistPercent = GetInt32(child, "Resist_Percent");
                    u.damageResist = p;
                    break;
                }
                case 10: { // Notoriety
                    UnlockableNotorietyPayload p;
                    p.score = OptText(child, "Score");
                    p.amount = GetInt32(child, "Amount");
                    u.notoriety = p;
                    break;
                }
                case 11: u.healthModifier = GetFloat(child, "Modifier"); break;
                case 12: u.cribName = OptText(child, "Name"); break;
                case 13: break; // Music: no elements read
                case 14: u.repairDiscountAmount = GetFloat(child, "Amount"); break;
                case 15: break; // Mission_Stronghold
                case 16: break; // Custom
                case 17: u.gangCustomizationGangType = OptText(child, "Gang_Type"); break;
                case 18: u.gangVehicleCustomizationVehicleGroup = OptText(child, "Vehicle_Group"); break;
                case 19: u.meleeDamageBonusMultiplier = GetFloat(child, "Damage_Multiplier"); break;
                case 20: u.unlimitedCribAmmoWeaponClass = OptText(child, "weapon_class"); break;
                case 21: { // Ammo_Multiplier
                    UnlockableAmmoMultiplierPayload p;
                    p.invSlot = OptText(child, "inv_slot");
                    p.ammoMul = GetFloat(child, "ammo_mul");
                    u.ammoMultiplier = p;
                    break;
                }
                case 22: u.noReloadingInvSlot = OptText(child, "inv_slot"); break;
                case 23: u.firearmAccuracyMultiplier = GetFloat(child, "accuracy_multiplier"); break;
                case 24: u.gangTauntsTeam = OptText(child, "Team"); break;
                case 25: // Taunts: <Actions><Animation_Action><Action/></Animation_Action>...</Actions>
                    if (const Node* acts = FindChild(child, "Actions")) {
                        for (const Node* a = FindChild(acts, "Animation_Action"); a; a = NextSibling(acts, a, "Animation_Action")) {
                            if (const std::string* t = ChildText(a, "Action")) u.tauntsActions.push_back(*t);
                        }
                    }
                    break;
                case 26: // Compliments: same shape as Taunts
                    if (const Node* acts = FindChild(child, "Actions")) {
                        for (const Node* a = FindChild(acts, "Animation_Action"); a; a = NextSibling(acts, a, "Animation_Action")) {
                            if (const std::string* t = ChildText(a, "Action")) u.complimentsActions.push_back(*t);
                        }
                    }
                    break;
                case 27: u.outfitName = OptText(child, "Outfit"); break;
                case 28: break; // Free_Driveup_Homie
                case 29: break; // Pickpocket
                case 30: u.dualWieldWeaponType = OptText(child, "Weapon_Type"); break;
                case 31: u.weeklyPaymentsAmount = GetInt32(child, "Amount"); break;
                case 32: { // Pay_Cash_for_Respect
                    UnlockablePayCashForRespectPayload p;
                    p.cashCost = GetInt32(child, "Cash_Cost");
                    p.respectReceived = GetInt32(child, "Respect_Received");
                    u.payCashForRespect = p;
                    break;
                }
                case 33: u.lumpSumOfMoneyAmount = GetInt32(child, "Amount"); break;
                case 34: break; // Vehicle_Customization
                case 35: break; // Character_Customization
                case 36: break; // Gang_Customization_Unlock
                case 37: u.respectBonusModifier = GetFloat(child, "Modifier"); break;
                case 38: u.cashBonusModifier = GetFloat(child, "Modifier"); break;
                case 39: u.npcCashDropModifier = GetFloat(child, "Modifier"); break;
                case 40: u.dbnoExtraTimeMs = GetInt32(child, "ms"); break;
                case 41: u.reviveHoldTimeModifier = GetFloat(child, "Modifier"); break;
                case 42: u.homieHealthBonusModifier = GetFloat(child, "Modifier"); break;
                case 43: u.specialHomieHealthBonusModifier = GetFloat(child, "Modifier"); break;
                case 44: { // Player_Health_Bonus
                    UnlockablePlayerHealthBonusPayload p;
                    p.modifier = GetFloat(child, "Modifier");
                    p.level = GetInt32(child, "Level");
                    u.playerHealthBonus = p;
                    break;
                }
                case 45: { // Muscles
                    UnlockableMusclesPayload p;
                    p.boost = GetFloat(child, "Boost");
                    p.meleeMultiplier = GetFloat(child, "Melee_Multiplier");
                    p.throwX = GetFloat(child, "Throw_x");
                    p.throwY = GetFloat(child, "Throw_y");
                    p.throwZ = GetFloat(child, "Throw_z");
                    u.muscles = p;
                    break;
                }
                case 46: u.weaponCustomizationDiscountAmount = GetFloat(child, "Amount"); break;
                case 47: break; // Reminder
                case 48: break; // Auto_Complete_City_Takeover_District
                case 49: break; // Auto_Complete_City_Takeover_All
                case 50: break; // Explosive_No_Ragdoll
                case 51: u.gangWeaponsSlot = OptText(child, "Weapon_Slot"); break;
                case 52: break; // Vampire
                case 53: u.customizationItemsTeamName = OptText(child, "Team_Name"); break;
                case 54: break; // Bloody_Mess
                case 55: u.reloadSpeedBonusScalar = GetFloat(child, "scalar"); break;
                case 56: break; // Immune_To_Flat_Tires
                case 57: break; // Collectable_Finder
                case 58: u.cribCashLimitScalar = GetFloat(child, "scalar"); break;
                case 59: { // Crib_Cash_Stronghold_Scalar
                    UnlockableCribCashStrongholdScalarPayload p;
                    p.scalar = GetFloat(child, "scalar");
                    p.district = OptText(child, "district");
                    u.cribCashStrongholdScalar = p;
                    break;
                }
                case 60: u.cribAmmoPercent = GetFloat(child, "percent"); break;
                default: break;
            }
            break; // first present child wins (spec 4.4)
        }
    }

    u.displayName = OptText(row, "DisplayName");
    u.description = OptText(row, "Description");
    if (const Node* imgSrc = FindChild(row, "Image_Source")) u.imageFilename = OptText(imgSrc, "Filename");
    {
        static constexpr std::string_view kCategoryNames[10] = {
            "Player Abilities", "Health", "Damage", "Weapons", "Vehicles",
            "Homies", "Discounts", "Customization", "Strongholds", "Activities",
        };
        int idx = EnumIndex(FindChild(row, "Category"), kCategoryNames, 10);
        u.category = idx >= 0 ? static_cast<UnlockableCategory>(idx) : UnlockableCategory::PlayerAbilities;
    }
    u.detailedDescriptionText = OptText(row, "Detailed_Description_Text");
    u.eventText = OptText(row, "Event_Text");
    u.price = ReadUInt32Always(row, "Price");
    u.priority = OptText(row, "Priority");
    {
        static constexpr std::string_view kAutoUnlockNames[4] = {
            "Silently", "Loudly", "Loudly (Helipad)", "Silently (Last)",
        };
        int idx = EnumIndex(FindChild(row, "Auto_Unlock"), kAutoUnlockNames, 4);
        u.autoUnlock = idx >= 0 ? static_cast<UnlockableAutoUnlock>(idx + 1) : UnlockableAutoUnlock::None;
    }
    {
        static constexpr std::string_view kIsDlcNames[3] = {"False", "True", "Sometimes"};
        int idx = EnumIndex(FindChild(row, "Is_DLC"), kIsDlcNames, 3);
        u.isDlc = idx >= 0 ? static_cast<UnlockableIsDlc>(idx) : UnlockableIsDlc::False;
    }
    u.framework = OptText(row, "Framework");
    return u;
}

std::vector<Unlockable> ParseUnlockablesTable(const Document& doc) {
    std::vector<Unlockable> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Unlockable"); row; row = NextSibling(table, row, "Unlockable"))
        out.push_back(ParseUnlockable(row));
    return out;
}

// ===========================================================================
// respect_levels.xtbl - spec-tables-progression.md S5
// ===========================================================================

RespectLevel ParseRespectLevel(const Node* row) {
    RespectLevel r;
    r.rank = OptText(row, "Rank");
    r.respect = ReadInt32Always(row, "Respect");
    if (const Node* uns = FindChild(row, "Unlockables")) {
        for (const Node* u = FindChild(uns, "Unlockable"); u; u = NextSibling(uns, u, "Unlockable"))
            if (u->text()) r.unlockables.push_back(*u->text());
    }
    return r;
}

std::vector<RespectLevel> ParseRespectLevelsTable(const Document& doc) {
    std::vector<RespectLevel> out;
    const Node* table = doc.table();
    const Node* rl = FindChild(table, "respect_levels");
    const Node* levels = rl ? FindChild(rl, "levels") : nullptr;
    if (!levels) return out;
    for (const Node* row = FindChild(levels, "respect_level"); row; row = NextSibling(levels, row, "respect_level"))
        out.push_back(ParseRespectLevel(row));
    return out;
}

// ===========================================================================
// notoriety.xtbl - spec-tables-progression.md S6.1
// ===========================================================================

const char* const kNotorietyActivityNames[30] = {
    "armored truck assault", "burglary", "carjacking", "car assault", "civilian assault",
    "civilian kill", "civilian shooting", "civilian squirting", "drug use", "firearm discharge",
    "gang alert", "gang assault", "gang kill", "gang shooting", "generic gang",
    "generic police", "govt car destroy", "govt car theft", "govt heli theft", "helicopter destroy",
    "mover dislodge", "public nudity", "police alert", "police assault", "police kill",
    "police shooting", "robbery", "atm extortion", "vandalism", "enter owned store",
};

NotorietyEntry ParseNotorietyEntry(const Node* row) {
    NotorietyEntry e;
    if (const std::string* nameText = ChildText(row, "Name")) {
        for (size_t i = 0; i < kNotorietyActivityCount; ++i) {
            if (NameEquals(*nameText, kNotorietyActivityNames[i])) {
                e.activityIndex = static_cast<int>(i);
                break;
            }
        }
    }
    const Node* data = FindChild(row, "Data");
    const Node* activity = data ? FindChild(data, "Activity") : nullptr;
    if (activity) {
        e.delay = ReadInt32Always(activity, "Delay");
        if (const Node* gang = FindChild(activity, "Gang")) {
            e.gang.points = ReadInt32Always(gang, "Points");
            e.gang.minLevel = ReadInt32Always(gang, "Min_Level");
            e.gang.maxLevel = ReadInt32Always(gang, "Max_Level");
            e.gang.checkDetection = TextEqualsTrue(gang, "Check_Detection");
        }
        if (const Node* police = FindChild(activity, "Police")) {
            e.police.points = ReadInt32Always(police, "Points");
            e.police.minLevel = ReadInt32Always(police, "Min_Level");
            e.police.maxLevel = ReadInt32Always(police, "Max_Level");
            e.police.checkDetection = TextEqualsTrue(police, "Check_Detection");
        }
    }
    return e;
}

std::vector<NotorietyEntry> ParseNotorietyTable(const Document& doc) {
    std::vector<NotorietyEntry> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Entry"); row; row = NextSibling(table, row, "Entry"))
        out.push_back(ParseNotorietyEntry(row));
    return out;
}

// ===========================================================================
// notoriety_levels.xtbl - spec-tables-progression.md S6.2
// ===========================================================================

NotorietyLevelsSet ParseNotorietyLevelsSet(const Node* setNode) {
    NotorietyLevelsSet s;
    const Node* data = FindChild(setNode, "NotorietyLevelsData");
    if (!data) return s;
    for (const Node* el = FindChild(data, "NotorietyLevelElement"); el; el = NextSibling(data, el, "NotorietyLevelElement")) {
        NotorietyLevelElement e;
        e.notorietyLevel = ReadInt32Always(el, "NotorietyLevel");
        e.notorietyLevelLimit = ReadInt32Always(el, "NotorietyLevelLimit");
        e.notorietyDecayRate = ReadInt32Always(el, "NotorietyDecayRate");
        e.notorietyFirstDecay = ReadInt32Always(el, "NotorietyFirstDecay");
        e.roadblockSpawnTimeMin = ReadInt32Always(el, "RoadblockSpawnTimeMin");
        e.roadblockSpawnTimeMax = ReadInt32Always(el, "RoadblockSpawnTimeMax");
        e.bruteSpawnTimeMin = ReadInt32Always(el, "BruteSpawnTimeMin");
        e.bruteSpawnTimeMax = ReadInt32Always(el, "BruteSpawnTimeMax");
        s.levels.push_back(e);
    }
    return s;
}

NotorietyLevelsTable ParseNotorietyLevelsTable(const Document& doc) {
    NotorietyLevelsTable t;
    const Node* table = doc.table();
    const Node* first = FindChild(table, "NotorietyLevels");
    if (first) {
        t.setA = ParseNotorietyLevelsSet(first);
        const Node* second = NextSibling(table, first, "NotorietyLevels");
        if (second) t.setB = ParseNotorietyLevelsSet(second);
    }
    return t;
}

// ===========================================================================
// notoriety_spawn.xtbl - spec-tables-progression.md S6.3 (element tree corrected by S14.6)
// ===========================================================================

const char* const kNotorietySpawnGroupNames[24] = {
    "police", "police_motorcycle", "police_heli", "police_blackhawk", "police_attack_heli",
    "police_waverunner", "police_speedboat", "police_riot", "police_tank", "police_ng",
    "luchadores", "deckers", "morningstar", "morningstar_heli", "stag",
    "stag_riot", "stag_heli", "stag_attack_heli", "stag_tank", "whored_one",
    "whored_two", "survival_bikers", "survival_mascots", "survival_bums",
};

// spec-tables-progression.md S6.3's flat element tree is STRUCK ("SUPERSEDED by
// S14.6 ... do not implement from the block below"). S14.6's structural
// correction is labelled "[CONFIRMED - empirical, from-scratch stack trace of
// the raw file around several rows.]": level_info, group_info and group_details
// are each ONE wrapper child of the row whose own children carry the same tag
// name and are the records:
//   <level_info><level_info>...</level_info><level_info>...</level_info></level_info>
// (likewise for the other two). Leaf names/types are unchanged from S6.3 (its
// corrected tree: "leaf names, types and conversions unchanged"). Only the
// FIRST wrapper of each name is read (the corrected tree says "ONE wrapper");
// record-shaped elements sitting directly under the row (the struck flat
// shape) are NOT records any more. S6.3's own review status is NEEDS-EXE for
// the 24-vs-25 loop bound only, which this per-row reader does not depend on.
NotorietySpawnRow ParseNotorietySpawnRow(const Node* row) {
    NotorietySpawnRow r;
    if (const std::string* n = ChildText(row, "Name")) r.name = *n;
    const Node* liWrap = FindChild(row, "level_info");
    for (const Node* li = liWrap ? FindChild(liWrap, "level_info") : nullptr; li;
         li = NextSibling(liWrap, li, "level_info")) {
        SpawnLevelInfo l;
        l.level = ReadInt32Always(li, "level");
        l.minSpawnTime = ReadInt32Always(li, "min_spawn_time");
        l.maxSpawnTime = ReadInt32Always(li, "max_spawn_time");
        l.vehMinSpawnTime = ReadInt32Always(li, "veh_min_spawn_time");
        l.vehMaxSpawnTime = ReadInt32Always(li, "veh_max_spawn_time");
        l.maxVehicleOccupants = ReadInt32Always(li, "max_vehicle_occupants");
        l.maxVehicles = ReadInt32Always(li, "max_vehicles");
        l.npcCap = ReadInt32Always(li, "npc_cap");
        l.specialistCap = ReadInt32Always(li, "specialist_cap");
        l.bruteCap = ReadInt32Always(li, "brute_cap");
        r.levelInfos.push_back(l);
    }
    const Node* giWrap = FindChild(row, "group_info");
    for (const Node* gi = giWrap ? FindChild(giWrap, "group_info") : nullptr; gi;
         gi = NextSibling(giWrap, gi, "group_info")) {
        SpawnGroupInfo g;
        g.level = ReadInt32Always(gi, "level");
        g.chance = ReadFloatAlways(gi, "chance");
        g.tagName = OptText(gi, "tag_name");
        g.vehicleName = OptText(gi, "vehicle_name");
        g.variantName = OptText(gi, "variant_name");
        r.groupInfos.push_back(g);
    }
    const Node* gdWrap = FindChild(row, "group_details");
    for (const Node* gd = gdWrap ? FindChild(gdWrap, "group_details") : nullptr; gd;
         gd = NextSibling(gdWrap, gd, "group_details")) {
        SpawnGroupDetail d;
        d.tagName = OptText(gd, "tag_name");
        d.seatName = OptText(gd, "seat_name");
        d.npcName = OptText(gd, "npc_name");
        d.meleeBruteSeat = GetBool(gd, "melee_brute_seat").value_or(false);
        d.weaponsBruteSeat = GetBool(gd, "weapons_brute_seat").value_or(false);
        d.rollerbladersSeat = GetBool(gd, "rollerbladers_seat").value_or(false);
        d.outsideSeat = GetBool(gd, "outside_seat").value_or(false);
        r.groupDetails.push_back(d);
    }
    r.overrideGroup = OptText(row, "override_group");
    if (const Node* flags = FindChild(row, "spawn_flags")) {
        r.spawnOnLand = HasFlag(flags, "Spawn on Land");
        r.spawnOnWater = HasFlag(flags, "Spawn on Water");
        r.spawnVsBikesOnly = HasFlag(flags, "Spawn vs Bikes Only");
        r.spawnIndoorsOnly = HasFlag(flags, "Spawn Indoors Only");
        r.attackHelicopter = HasFlag(flags, "Attack Helicopter");
        r.forceNoRam = HasFlag(flags, "Force No Ram");
        r.activeWithStag = HasFlag(flags, "Active With Stag");
    }
    return r;
}

std::vector<NotorietySpawnRow> ParseNotorietySpawnTable(const Document& doc) {
    std::vector<NotorietySpawnRow> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Table"); row; row = NextSibling(table, row, "Table"))
        out.push_back(ParseNotorietySpawnRow(row));
    return out;
}

// ===========================================================================
// difficulty_levels.xtbl - spec-tables-progression.md S7
// ===========================================================================

DynamicDifficultySet ParseDynamicDifficultySet(const Node* setNode) {
    DynamicDifficultySet s;
    if (const Node* levels = FindChild(setNode, "Difficulty_Levels")) {
        for (const Node* dl = FindChild(levels, "Difficulty_Level"); dl; dl = NextSibling(levels, dl, "Difficulty_Level")) {
            DifficultyLevel l;
            l.damageReceivedMult = ReadFloatAlways(dl, "Damage_Received_Mult");
            l.healthRegenWaitMult = ReadFloatAlways(dl, "Health_Regen_Wait_Mult");
            l.healthRegenSpeedMult = ReadFloatAlways(dl, "Health_Regen_Speed_Mult");
            l.damageDealtMult = ReadFloatAlways(dl, "Damage_Dealt_Mult");
            l.notorietyDecayMult = ReadFloatAlways(dl, "Notoriety_Decay_Mult");
            l.homieReviveTimerMult = ReadFloatAlways(dl, "Homie_Revive_Timer_Mult");
            l.vehicleDamageReceivedMult = ReadFloatAlways(dl, "Vehicle_Damage_Received_Mult");
            l.bruteHealthScalar = ReadFloatAlways(dl, "Brute_Health_Scalar");
            l.friendlyHealthScalar = ReadFloatAlways(dl, "Friendly_Health_Scalar");
            l.playerRamDelaySeconds = ReadFloatAlways(dl, "Player_Ram_Delay");
            s.levels.push_back(l);
        }
    }
    if (const Node* ranks = FindChild(setNode, "Rank_Occurrence_Percentages")) {
        for (const Node* re = FindChild(ranks, "Rank_Occurrence_Element"); re; re = NextSibling(ranks, re, "Rank_Occurrence_Element")) {
            RankOccurrenceElement r;
            r.missionName = OptText(re, "Mission_Name");
            r.rank1 = ReadFloatAlways(re, "Rank1");
            r.rank2 = ReadFloatAlways(re, "Rank2");
            r.rank3 = ReadFloatAlways(re, "Rank3");
            r.rank4 = ReadFloatAlways(re, "Rank4");
            s.rankOccurrences.push_back(r);
        }
    }
    return s;
}

DifficultyLevelsTable ParseDifficultyLevelsTable(const Document& doc) {
    DifficultyLevelsTable t;
    const Node* table = doc.table();
    const Node* first = FindChild(table, "Dynamic_Difficulty");
    if (first) {
        t.setA = ParseDynamicDifficultySet(first);
        const Node* second = NextSibling(table, first, "Dynamic_Difficulty");
        if (second) t.setB = ParseDynamicDifficultySet(second);
    }
    return t;
}


} // namespace sr3tables_progression

// sr3tables_progression - implementation. Built ONLY on include/sr3xtbl/xtbl.h's
// accessors; every field read here is cited to its spec-tables-progression.md
// section in the matching header (tables_core.h / tables_world.h / tables_rules.h).
// No game executable, disassembly or decompiled code was consulted (cleanroom
// boundary - see the headers' banner comments).

#include "sr3tables_progression/tables.h"

#include <array>
#include <string>
#include <string_view>

namespace sr3tables_progression {

using sr3xtbl::ChildText;
using sr3xtbl::EnumIndex;
using sr3xtbl::FindChild;
using sr3xtbl::GetBool;
using sr3xtbl::GetFloat;
using sr3xtbl::GetInt32;
using sr3xtbl::GetUInt32;
using sr3xtbl::HasFlag;
using sr3xtbl::NameEquals;
using sr3xtbl::NextSibling;
using sr3xtbl::ReadBoolAlways;
using sr3xtbl::ReadFloatAlways;
using sr3xtbl::ReadInt32Always;
using sr3xtbl::ReadUInt32Always;
using sr3xtbl::ReadUInt8Always;
using sr3xtbl::ReadVec3Child;
using sr3xtbl::Vec3Result;

namespace {

std::optional<std::string> OptText(const Node* n, std::string_view name) {
    const std::string* t = ChildText(n, name);
    if (!t) return std::nullopt;
    return *t;
}

// spec 6.1: Check_Detection's ONLY accepted spelling is the literal "true"
// (unlike sr3xtbl::ParseBool, "yes" does NOT count here).
bool TextEqualsTrue(const Node* n, std::string_view name) {
    const std::string* t = ChildText(n, name);
    return t != nullptr && NameEquals(*t, "true");
}

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
    s.allowUpdateByServer = ReadBoolAlways(row, "Allow_Update_By_Server");
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
        if (const Node* g = FindChild(n, "Gun")) {
            c.combatAi.gun.repositionMin = ReadUInt32Always(g, "Reposition_Min");
            c.combatAi.gun.repositionMax = ReadUInt32Always(g, "Reposition_Max");
            c.combatAi.gun.cantFireRepositionMin = ReadUInt32Always(g, "Cant_Fire_Reposition_Min");
            c.combatAi.gun.cantFireRepositionMax = ReadUInt32Always(g, "Cant_Fire_Reposition_Max");
        }
        if (const Node* p = FindChild(n, "Pepperspray")) {
            c.combatAi.pepperspray.sprayMin = ReadUInt32Always(p, "Spray_Min");
        }
        c.combatAi.pepperSprayMinUsageDelay = ReadUInt32Always(n, "PepperSprayMinUsageDelay");
        c.combatAi.stunGunMinUsageDelay = ReadUInt32Always(n, "StunGunMinUsageDelay");
        c.combatAi.backAwayMinDist = ReadFloatAlways(n, "Back_Away_Min_Dist");
        c.combatAi.backAwayMaxDist = ReadFloatAlways(n, "Back_Away_Max_Dist");
        c.combatAi.backAwayAbsMinDist = ReadFloatAlways(n, "Back_Away_Abs_Min_Dist");
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
// default_global.xtbl - spec-tables-progression.md S10.9
// ===========================================================================

DefaultGlobal ParseDefaultGlobal(const Document& doc) {
    DefaultGlobal g;
    const Node* root = doc.root();
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

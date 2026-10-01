// The tutorial name table (spec-lua-api-behaviour.md Sec26.28, batch
// 2026-10-01): the 210 strings the static, file-backed pointer table at
// 0x012f5930 points at, in index order, as the spec lists them. These are
// functional identifiers that scripts pass to tutorial_start /
// tutorial_advance and that the engine resolves by name (owner's content
// ruling 2026-10-01, quoted in Sec26.28); they are spec text, transcribed by
// a script from the spec's table, not read from any game file.
//
// CONFIRMED - disassembly (static data) for 209 names; index 176 is OPEN
// (the dump did not resolve its string: shorter than 4 characters or
// non-ASCII), so it is nullptr here and EngineState::tutorialLookup treats
// it as unknown. Index 36 and 129 are both "escort_minigame" in the spec;
// the resolver's linear scan returns the first.
#include "sr3luahost/engine_state.h"

namespace sr3luahost {

namespace {
const char* const kTutorialNames[EngineState::kTutorialEntryCount] = {
    "save", // 0
    "autosave", // 1
    "notoriety_gang", // 2
    "notoriety_police", // 3
    "notoriety_forgive", // 4
    "crib_receive", // 5
    "crib_customize", // 6
    "crib_garage", // 7
    "crib_cash", // 8
    "crib_closet", // 9
    "crib_weapons", // 10
    "crib_newspaper", // 11
    "combat", // 12
    "busted", // 13
    "smoked", // 14
    "homie_revive", // 15
    "waypoint", // 16
    "explore", // 17
    "stronghold_start", // 18
    "stronghold_respect", // 19
    "act_intro_crowd_control", // 20
    "act_intro_drug_trafficking", // 21
    "act_intro_escort", // 22
    "act_intro_escort_tiger", // 23
    "act_intro_fight_club", // 24
    "act_intro_fuzz", // 25
    "act_intro_heli_assault", // 26
    "act_intro_human_torch", // 27
    "act_intro_ins_fraud", // 28
    "act_intro_mayhem", // 29
    "act_intro_piracy", // 30
    "act_intro_septic_truck", // 31
    "act_intro_snatch", // 32
    "act_intro_snatch_kinzie", // 33
    "act_crowd_control_throwing", // 34
    "act_crowd_control_grab", // 35
    "escort_minigame", // 36
    "fight_club_neck_breaker", // 37
    "fight_club_opponent_down", // 38
    "activity_complete", // 39
    "div_intro_ambulance", // 40
    "div_intro_fire_truck", // 41
    "div_available_flashing", // 42
    "div_intro_hoing", // 43
    "div_intro_hostage", // 44
    "div_intro_mugging", // 45
    "div_intro_racing", // 46
    "div_available_streaking", // 47
    "div_available_streaking_pc", // 48
    "div_intro_streaking", // 49
    "div_intro_tagging", // 50
    "div_intro_taxi", // 51
    "div_intro_tow_truck", // 52
    "div_intro_base_jumping", // 53
    "diversion_complete", // 54
    "collection_collectible", // 55
    "collection_stunt_jump", // 56
    "store_intro_clothes", // 57
    "store_intro_mechanic", // 58
    "store_intro_weapons", // 59
    "store_intro_melee_weapons", // 60
    "store_intro_liquor", // 61
    "store_intro_fnf", // 62
    "store_intro_music", // 63
    "store_intro_surgeon", // 64
    "store_intro_food", // 65
    "store_intro_tattoo", // 66
    "store_intro_jewelry", // 67
    "store_intro_mechanic_large", // 68
    "store_intro_vehicle_dealer", // 69
    "human_shield", // 70
    "quick_kill", // 71
    "throw_kill", // 72
    "improvised_weapons", // 73
    "minimap_icons", // 74
    "combat_ranged", // 75
    "combat_weapons", // 76
    "combat_grenades", // 77
    "combat_grenades_pc", // 78
    "combat_swords", // 79
    "combat_fineaim", // 80
    "jump_climb", // 81
    "crouch", // 82
    "driving_stunts", // 83
    "shooting_stunts", // 84
    "ambulance_shock_paddles", // 85
    "ambulance_cpr", // 86
    "cruise_control_completed", // 87
    "helicopter_control", // 88
    "helicopter_control_pc", // 89
    "airplane_control", // 90
    "airplane_control_alt", // 91
    "airplane_control_alt_pc", // 92
    "vtol_control", // 93
    "mission_checkpoint", // 94
    "taunting", // 95
    "respect_meter", // 96
    "health_sprint_meter", // 97
    "pause_map", // 98
    "minimap", // 99
    "recruiting", // 100
    "diversion_hud", // 101
    "hud_text", // 102
    "unlockables", // 103
    "store_ownership", // 104
    "gang_customize", // 105
    "shortcut_splines", // 106
    "satchel_charges", // 107
    "ar50", // 108
    "annihilator", // 109
    "np_car_controls", // 110
    "np_motorcycle_controls", // 111
    "np_motorcycle_controls_pc", // 112
    "np_boat_controls", // 113
    "np_heli_weapon_controls", // 114
    "np_tank_controls", // 115
    "np_tank_controls_pc", // 116
    "div_intro_coop_death_tag", // 117
    "div_intro_coop_cat_and_mouse", // 118
    "territory_", // 119
    "tss02_complete_", // 120
    "sprint", // 121
    "vehicle entry", // 122
    "act_intro_tank_mayhem", // 123
    "act_tank_mayhem_hvt", // 124
    "act_tank_mayhem_repair", // 125
    "act_intro_guardian_angel", // 126
    "act_intro_running_man", // 127
    "act_intro_cyber_blazing", // 128
    "escort_minigame", // 129
    "final_act_level_complete", // 130
    "diversion_respect_cap", // 131
    "div_intro_taunting", // 132
    "div_intro_holdup", // 133
    "div_intro_hitman", // 134
    "div_hitman_started", // 135
    "div_hitman_gps", // 136
    "div_intro_chop_shop", // 137
    "div_chop_shop_vehicle_damage", // 138
    "div_chop_shop_gps", // 139
    "div_intro_survival", // 140
    "div_survival_started", // 141
    "div_intro_barnstorming", // 142
    "hitman_new", // 143
    "chopshop", // 144
    "act_fraud_first_hit", // 145
    "act_fraud_first_hit_pc", // 146
    "act_fraud_adrenaline", // 147
    "mis_02_skydive_flip", // 148
    "mis_02_skydive_flip_pc", // 149
    "game_complete", // 150
    "notoriety_decreases", // 151
    "weap_airstrike", // 152
    "weap_alt_fire", // 153
    "weap_charge_weapon", // 154
    "weap_drone", // 155
    "weap_rc_vehicle", // 156
    "weap_rc_vehicle_upgraded", // 157
    "weap_temp_weapon", // 158
    "weap_zoom", // 159
    "weap_dlc_genki_gun", // 160
    "weap_dlc_shotgun", // 161
    "weap_dlc_sniper", // 162
    "combat_melee_bash", // 163
    "upgrade_store", // 164
    "weapon_upgrades", // 165
    "recieve_crib", // 166
    "flashpoint", // 167
    "player_choice", // 168
    "rank_up", // 169
    "car_shooting", // 170
    "vehicle_turret", // 171
    "secret_locations", // 172
    "sprint_recharge", // 173
    "radio_control", // 174
    "human_shield_general", // 175
    nullptr, // 176: OPEN (Sec26.28: string at 0x01124348 not resolved by the dump)
    "dlc_mancannon", // 177
    "nag_change_clothes", // 178
    "nag_crib_stash", // 179
    "city_takeover", // 180
    "nag_upgrade_store", // 181
    "challenges", // 182
    "nitrous", // 183
    "low_ammo", // 184
    "TUT_SIXAXIS_BOAT", // 185
    "TUT_SIXAXIS_PLANE", // 186
    "TUT_SIXAXIS_HELICOPTER", // 187
    "horde_mode_pickup", // 188
    "dlc1_act_genki_escort_into", // 189
    "dlc1_act_panda_blazing", // 190
    "dlc1_act_ball_mayhem", // 191
    "dlc2_near_crash", // 192
    "dlc2_alien_aircraft", // 193
    "dlc1_act_ball_mayhem_shockwave", // 194
    "airplane_control_pc", // 195
    "horde_mode_new_wave", // 196
    "horde_mode_challenge_wave", // 197
    "horde_mode_weapon_upgrade", // 198
    "horde_mode_kill_all_enemies", // 199
    "horde_mode_unlimited_ammo", // 200
    "horde_mode_invulnerability", // 201
    "mis_21_ride_killbane", // 202
    "mis_21_ride_killbane_pc", // 203
    "mis_undercover", // 204
    "mis_16_avatar_attacks", // 205
    "mis_22_oleg_follow", // 206
    "dlc3_m03_super_powers", // 207
    "dlc2_win", // 208
    "dlc3_win", // 209
};
} // namespace

const char* EngineState::tutorialName(int index) {
    if (index < 0 || index >= kTutorialEntryCount) return nullptr;
    return kTutorialNames[index];
}

} // namespace sr3luahost

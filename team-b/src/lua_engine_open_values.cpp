// Engine state for the values answered by Team A's blocker write-ups in spec-lua-api-behaviour.md
// (spec syncs 727a33f, bfe0542, 98375ea): the human-shield gate (Sec13.14, dlc3_m01/m12), the synced-action
// instance (Sec18.5, m07), RC control (Sec18.10, m20) and the HUD-screen exit hook (Sec24.24, m16). Each method
// cites the "Port rule" paragraph it implements. Test-set OpenValues (the legacy test hooks) still take priority so
// the older unit tests keep exercising their own path.
#include "sr3luahost/engine_state.h"

namespace sr3luahost {

namespace {
bool sameName(const std::string& a, const std::string& b) { return AsciiLowerFold(a) == AsciiLowerFold(b); }
}  // namespace

void EngineState::beginHumanShield(const std::string& holderName, const std::string& hostageName) {
    // 0x009acb20(holder, hostage): "writes each party's id pair into the other's +0x1890/+0x1894, sets the
    // hostage's +0x18dc bit 0, clears the holder's, and sets both states to 2".
    CharacterState& holder = getOrCreateCharacter(holderName);
    CharacterState& hostage = getOrCreateCharacter(hostageName);
    holder.humanShieldPartnerName = hostageName;
    hostage.humanShieldPartnerName = holderName;
    hostage.humanShieldIsHostage = true;
    holder.humanShieldIsHostage = false;
    holder.humanShieldStateByte18d4 = 2;
    hostage.humanShieldStateByte18d4 = 2;
    holder.humanShieldHostageName = hostageName;  // the "taker's own held-hostage name" of Sec45.1 is the same link
}

bool EngineState::characterHasSpecificHumanShield(const std::string& char1, const std::string& char2) {
    CharacterState& c1 = getOrCreateCharacter(char1);
    if (c1.usingHumanShieldQualifies.known()) {  // legacy test hook
        return c1.usingHumanShieldQualifies.get() && c1.humanShieldHostageName == char2;
    }
    // Gate 0x009aa260(char1, 0), single-player path: state != 0, the partner resolves and is a live human, the link
    // is mutual, and char 1 is the holder (not the hostage); exclusion 0x009aa660: a partner with state in 3..6
    // does not count; final test: the partner is char 2.
    const int32_t state = c1.humanShieldStateByte18d4;
    if (state == 0 || state >= 3) return false;
    if (c1.humanShieldIsHostage) return false;
    if (c1.humanShieldPartnerName.empty()) return false;
    auto it = characters_.find(c1.humanShieldPartnerName);
    if (it == characters_.end()) return false;
    const CharacterState& partner = it->second;
    if (characterReadsDead(partner)) return false;              // 0x009aa130: must be alive
    if (!sameName(partner.humanShieldPartnerName, char1)) return false;  // mutual link
    return sameName(c1.humanShieldPartnerName, char2);
}

void EngineState::beginSyncedActionInstance(const std::string& a, const std::string& b, double durationSeconds) {
    const double now = syncedActionNowSeconds();
    for (const std::string* n : {&a, &b}) {
        CharacterState& c = getOrCreateCharacter(*n);
        c.syncedAnimStartSeconds = now;
        c.syncedAnimDurationSeconds = durationSeconds;
    }
}

void EngineState::endSyncedActionInstance(const std::string& name) {
    getOrCreateCharacter(name).syncedAnimDurationSeconds = 0.0;  // handle -1
}

bool EngineState::syncedActionFinishedFor(const std::string& name, double threshold) {
    CharacterState& c = getOrCreateCharacter(name);
    if (c.syncedAnimControllerActive.known()) {  // legacy test hook
        if (!c.syncedAnimControllerActive.get()) return true;
        return c.syncedAnimElapsedTime.get() >= threshold;
    }
    if (c.syncedAnimDurationSeconds <= 0.0) return true;  // no active instance
    const double elapsed = syncedActionNowSeconds() - c.syncedAnimStartSeconds;
    const double fraction = elapsed >= c.syncedAnimDurationSeconds ? 1.0 : (elapsed < 0.0 ? 0.0 : elapsed / c.syncedAnimDurationSeconds);
    return fraction >= threshold;
}

bool EngineState::beginRcControl(const std::string& vehicleName, const std::string& characterName) {
    CharacterState& c = getOrCreateCharacter(characterName);
    // 0x00a93070 bails if the character already has an active vehicle.
    if (!c.rcActiveVehicleName.empty() && hasVehicle(c.rcActiveVehicleName)) return false;
    c.rcActiveVehicleName = vehicleName;
    getOrCreateVehicle(vehicleName).rcControllerName = characterName;
    return true;
}

void EngineState::endRcControl(const std::string& vehicleName) {
    // 0x00a934b0: the null pair into the vehicle's controller; the character's active-vehicle link goes with it.
    getOrCreateVehicle(vehicleName).rcControllerName.clear();
    for (auto& kv : characters_)
        if (sameName(kv.second.rcActiveVehicleName, vehicleName)) kv.second.rcActiveVehicleName.clear();
}

bool EngineState::playerIsRcDriver(const std::string& characterName, const std::string& vehicleNameOrEmpty) {
    if (isRcDriverPredicate_.known(characterName)) return isRcDriverPredicate_.get(characterName);  // legacy test hook
    std::string vehicle = vehicleNameOrEmpty;
    if (vehicle.empty()) {
        auto it = characters_.find(characterName);
        if (it == characters_.end()) return false;
        vehicle = it->second.rcActiveVehicleName;  // without arg 2: the character's active-vehicle pair
    }
    if (vehicle.empty()) return false;
    for (const auto& kv : vehicles_)
        if (sameName(kv.first, vehicle)) return !kv.second.rcControllerName.empty() && sameName(kv.second.rcControllerName, characterName);
    return false;
}

bool EngineState::popHudScreen(const std::string& screenName, const std::string& playerName) {
    std::string cb = namedHookCallback("on_hud_screen_exit:" + screenName);
    if (cb.empty()) cb = namedHookCallback("on_hud_screen_exit:" + AsciiLowerFold(screenName));
    return fireEngineCallback(cb, {CallbackArg::str(screenName), CallbackArg::str(playerName)}, "on_hud_screen_exit");
}

}  // namespace sr3luahost

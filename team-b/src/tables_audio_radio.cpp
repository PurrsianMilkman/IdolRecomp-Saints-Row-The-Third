// Parse functions for sr3tables_audio_radio (see
// include/sr3tables_audio_radio/tables.h for the struct definitions and
// per-field spec citations). Built ONLY on sr3xtbl's accessors
// (include/sr3xtbl/xtbl.h); nothing here touches the game executable,
// disassembly or decompiled code. Source: spec-tables-audio-radio.md.

#include "sr3tables_audio_radio/tables.h"

namespace sr3tables_audio_radio {

namespace {

using sr3xtbl::Node;

std::optional<std::string> getText(const Node* node, std::string_view name = {}) {
    const std::string* t = sr3xtbl::ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

}  // namespace

// ===========================================================================
// Bespoke parsers
// ===========================================================================
uint32_t ParseWwiseIdDigitPrefix(std::string_view text) {
    if (!text.empty() && text[0] == '-') return 0;  // leading '-' aborts to 0 immediately (spec 1.3 item 1)
    uint32_t value = 0;
    size_t i = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10u + static_cast<uint32_t>(text[i] - '0');
        ++i;
    }
    const char stopChar = (i < text.size()) ? text[i] : '\0';
    if (stopChar == '\0' || stopChar == '.') return value;
    return 0;  // any other trailing character makes the whole parse return 0
}

// ===========================================================================
// 2. audio_banks.xtbl
// ===========================================================================
AudioBank ParseAudioBank(const Node* row) {
    AudioBank b;
    b.name = sr3xtbl::CopyText(row, "Name", 0x40);
    b.wwiseId = sr3xtbl::detail::alwaysWith<uint32_t>(row, "wwise_id", ParseWwiseIdDigitPrefix);
    b.streaming = getText(row, "streaming");
    b.loadAtBoot = getText(row, "load_at_boot");
    b.ramBankPc = getText(row, "ram_bank_pc");
    b.cacheablePc = getText(row, "cacheable_pc");
    b.ramSizePc = sr3xtbl::GetUInt32(row, "ram_size_pc");
    b.voice = getText(row, "voice");
    return b;
}

std::vector<AudioBank> ParseAudioBanksTable(const Document& doc) {
    std::vector<AudioBank> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "NewEntity")) out.push_back(ParseAudioBank(row));
    return out;
}

// ===========================================================================
// 3. audio_constants.xtbl
// ===========================================================================
std::optional<AudioConstants> ParseAudioConstants(const Document& doc) {
    const Node* ac = sr3xtbl::FindChild(doc.table(), "AudioConstants");
    if (!ac) return std::nullopt;
    AudioConstants c;

    const Node* playTimers = sr3xtbl::FindChild(ac, "PlayTimers");
    c.playTimerBrassCollision = sr3xtbl::ReadFloatAlways(playTimers, "BrassCollision");
    c.playTimerGlassShatter = sr3xtbl::ReadFloatAlways(playTimers, "GlassShatter");
    c.playTimerBulletImpactHuman = sr3xtbl::ReadFloatAlways(playTimers, "BulletImpactHuman");
    c.playTimerBulletImpactWall = sr3xtbl::ReadFloatAlways(playTimers, "BulletImpactWall");
    c.playTimerObjectDebris = sr3xtbl::ReadFloatAlways(playTimers, "ObjectDebris");
    c.playTimerVehicleImpactCollision = sr3xtbl::ReadFloatAlways(playTimers, "VehicleImpactCollision");
    c.playTimerVehicleImpactDistance = sr3xtbl::ReadFloatAlways(playTimers, "VehicleImpactDistance");
    c.playTimerVehicleScrapeCollision = sr3xtbl::ReadFloatAlways(playTimers, "VehicleScrapeCollision");
    c.playTimerVehicleScrapeDistance = sr3xtbl::ReadFloatAlways(playTimers, "VehicleScrapeDistance");
    c.playTimerRagdollBoneImpactCollision = sr3xtbl::ReadFloatAlways(playTimers, "RagdollBoneImpactCollision");
    c.playTimerSmallDeformation = sr3xtbl::ReadFloatAlways(playTimers, "SmallDeformation");
    c.playTimerLargeDeformation = sr3xtbl::ReadFloatAlways(playTimers, "LargeDeformation");

    const Node* onFoot = sr3xtbl::FindChild(ac, "OnFootSettings");
    c.onFootFootstepRange = sr3xtbl::ReadFloatAlways(onFoot, "FootstepRange");

    const Node* driving = sr3xtbl::FindChild(ac, "DrivingSettings");
    c.drivingAmbientSpawnAcquireRadio = sr3xtbl::ReadFloatAlways(driving, "AmbientSpawnAcquireRadio");
    const Node* alarm = sr3xtbl::FindChild(driving, "Alarm");
    c.drivingAlarmPercentage = sr3xtbl::ReadFloatAlways(alarm, "Percentage");
    c.drivingAlarmTimeMin = sr3xtbl::ReadUInt16Always(alarm, "TimeMin");
    c.drivingAlarmTimeMax = sr3xtbl::ReadUInt16Always(alarm, "TimeMax");
    const Node* passbyWhoosh = sr3xtbl::FindChild(driving, "Passby_whoosh");
    c.drivingPassbyWhooshMinDistanceOnFoot = sr3xtbl::ReadFloatAlways(passbyWhoosh, "min_distance_on_foot");
    c.drivingPassbyWhooshMinDistanceDriving = sr3xtbl::ReadFloatAlways(passbyWhoosh, "min_distance_driving");
    c.drivingPassbyWhooshMinSpeed = sr3xtbl::ReadFloatAlways(passbyWhoosh, "min_speed");

    const Node* wind = sr3xtbl::FindChild(ac, "Wind");
    const Node* highAltitude = sr3xtbl::FindChild(wind, "high_altitude");
    c.windHighAltitudeMinSpeed = sr3xtbl::ReadFloatAlways(highAltitude, "min_speed");
    c.windHighAltitudeMaxSpeed = sr3xtbl::ReadFloatAlways(highAltitude, "max_speed");
    c.windHighAltitudeChangeRate = sr3xtbl::ReadFloatAlways(highAltitude, "wind_change_rate");
    c.windHighAltitudeMinAltitude = sr3xtbl::ReadFloatAlways(highAltitude, "min_altitude");
    c.windHighAltitudeMaxAltitude = sr3xtbl::ReadFloatAlways(highAltitude, "max_altitude");
    const Node* playerFalling = sr3xtbl::FindChild(wind, "player_falling");
    c.windPlayerFallingMinSpeed = sr3xtbl::ReadFloatAlways(playerFalling, "min_speed");
    c.windPlayerFallingMaxSpeed = sr3xtbl::ReadFloatAlways(playerFalling, "max_speed");
    c.windPlayerFallingParachuteSpeed = sr3xtbl::ReadFloatAlways(playerFalling, "parachute_speed");

    const Node* playerHealth = sr3xtbl::FindChild(ac, "Player_Health");
    c.medHealth = sr3xtbl::ReadInt32Always(playerHealth, "med_health");

    return c;
}

// ===========================================================================
// 4. audio_settings.xtbl
// ===========================================================================
std::optional<AudioSettings> ParseAudioSettings(const Document& doc) {
    const Node* globalSettings = sr3xtbl::FindChild(doc.table(), "global_settings");
    if (!globalSettings) return std::nullopt;
    AudioSettings s;
    const Node* general = sr3xtbl::FindChild(globalSettings, "general_settings");
    s.speedOfSound = sr3xtbl::GetFloat(general, "Speed_of_sound");
    s.healthAdjustRate = sr3xtbl::GetFloat(general, "Health_adjust_rate");
    const Node* doppler = sr3xtbl::FindChild(globalSettings, "Doppler_settings");
    s.dopplerMultiplier = sr3xtbl::GetFloat(doppler, "Doppler_multiplier");
    return s;
}

// ===========================================================================
// 5. audio_line_tags.xtbl
// ===========================================================================
AudioLineTag ParseAudioLineTag(const Node* row) {
    AudioLineTag t;
    t.name = getText(row, "Name");
    t.wwiseId = sr3xtbl::GetUInt32(row, "wwise_id");
    return t;
}

std::vector<AudioLineTag> ParseAudioLineTagsTable(const Document& doc) {
    std::vector<AudioLineTag> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Audio_line")) out.push_back(ParseAudioLineTag(row));
    return out;
}

// ===========================================================================
// 6. audio_personas.xtbl
// ===========================================================================
namespace {
// spec 6: fixed priority order, first substring match wins; case-SENSITIVE (see tables.h's judgment-call note).
int findSuffixIndex(const std::string& name) {
    if (name.find('_') == std::string::npos) return -1;  // "searched only after first confirming an underscore"
    for (size_t i = 0; i < kAudioPersonaDemographicSuffixes.size(); ++i) {
        if (name.find(kAudioPersonaDemographicSuffixes[i]) != std::string::npos) return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

int AudioPersona::DeriveGenderFromName() const {
    if (!name) return 0;
    const int idx = findSuffixIndex(*name);
    if (idx < 0) return 0;
    return (idx % 2 == 0) ? 1 : 2;  // even index = male (_WM/_BM/_HM/_AM), odd = female
}

int AudioPersona::DeriveEthnicityFromName() const {
    if (!name) return 0;
    const int idx = findSuffixIndex(*name);
    if (idx < 0) return 0;
    return (idx / 2) + 1;  // 0,1->White(1) 2,3->Black(2) 4,5->Hispanic(3) 6,7->Asian(4)
}

int AudioPersona::DeriveAgeFromName() const {
    if (!name) return 0;
    for (size_t i = 0; i < kAudioPersonaAgeTokens.size(); ++i) {
        if (name->find(kAudioPersonaAgeTokens[i]) != std::string::npos) return static_cast<int>(i) + 1;
    }
    return 0;
}

AudioPersona ParseAudioPersona(const Node* row) {
    AudioPersona p;
    p.name = sr3xtbl::CopyText(row, "Name", 0x20);
    p.wwiseId = sr3xtbl::GetUInt32(row, "wwise_id");
    return p;
}

std::vector<AudioPersona> ParseAudioPersonasTable(const Document& doc) {
    std::vector<AudioPersona> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Audio_Persona")) out.push_back(ParseAudioPersona(row));
    return out;
}

// ===========================================================================
// 7. persona_radio_prefs.xtbl
// ===========================================================================
PersonaRadioPref ParsePersonaRadioPref(const Node* row) {
    PersonaRadioPref p;
    p.name = getText(row, "Name");
    p.radioStation = getText(row, "Radio_Station");
    return p;
}

std::vector<PersonaRadioPref> ParsePersonaRadioPrefsTable(const Document& doc) {
    std::vector<PersonaRadioPref> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Audio_Persona")) out.push_back(ParsePersonaRadioPref(row));
    return out;
}

// ===========================================================================
// 8. foley_collision.xtbl
// ===========================================================================
FoleyCollision ParseFoleyCollision(const Node* row) {
    FoleyCollision f;
    f.name = getText(row, "Name");
    const Node* set = sr3xtbl::FindChild(row, "CollisionFoleySet");
    f.minimumSpeedRaw = sr3xtbl::ReadFloatAlways(set, "MinimumSpeed");
    f.maximumSpeedRaw = sr3xtbl::ReadFloatAlways(set, "MaximumSpeed");
    f.frequency = sr3xtbl::GetUInt32(set, "Frequency");
    f.wwiseSwitch = getText(set, "Wwise_switch");
    return f;
}

std::vector<FoleyCollision> ParseFoleyCollisionTable(const Document& doc) {
    std::vector<FoleyCollision> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "FoleyCollision")) out.push_back(ParseFoleyCollision(row));
    return out;
}

// ===========================================================================
// 9. foley_touch.xtbl
// ===========================================================================
FoleyTouch ParseFoleyTouch(const Node* row) {
    FoleyTouch f;
    f.name = getText(row, "Name");
    const Node* set = sr3xtbl::FindChild(row, "TouchFoleySet");
    f.frequency = sr3xtbl::GetUInt32(set, "Frequency");
    f.wwiseSwitch = getText(set, "Wwise_switch");
    return f;
}

std::vector<FoleyTouch> ParseFoleyTouchTable(const Document& doc) {
    std::vector<FoleyTouch> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "FoleyTouch")) out.push_back(ParseFoleyTouch(row));
    return out;
}

// ===========================================================================
// 10. foley_engine.xtbl
// ===========================================================================
FoleyEngine ParseFoleyEngine(const Node* row) {
    FoleyEngine f;
    f.name = getText(row, "Name");
    f.vehicleModel = getText(row, "Vehicle_Model");
    f.npcOnly = sr3xtbl::GetBool(row, "NPC_Only");
    f.dlcFrameworkId = sr3xtbl::GetInt8(row, "dlc_framework_id");
    return f;
}

std::vector<FoleyEngine> ParseFoleyEngineTable(const Document& doc) {
    std::vector<FoleyEngine> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Engine")) out.push_back(ParseFoleyEngine(row));
    return out;
}

// ===========================================================================
// 11. radio_stations.xtbl
// ===========================================================================
RadioStationInfo ParseRadioStationInfo(const Node* infoRow) {
    RadioStationInfo s;
    s.filename = getText(sr3xtbl::FindChild(infoRow, "xtbl_name"), "Filename");
    s.genre = sr3xtbl::CopyText(infoRow, "Genre", 0x40);
    const Node* flags = sr3xtbl::FindChild(infoRow, "Station_flags");
    s.selectable = sr3xtbl::HasFlag(flags, "Selectable");
    s.policeStation = sr3xtbl::HasFlag(flags, "Police_Station");
    s.fbiStation = sr3xtbl::HasFlag(flags, "FBI_Station");
    s.newsStation = sr3xtbl::HasFlag(flags, "News_Station");
    s.wwiseId = getText(infoRow, "wwise_id");
    return s;
}

std::optional<RadioSettings> ParseRadioSettings(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "NewEntity");
    if (!row) return std::nullopt;
    RadioSettings r;
    r.simultaneousNpcRadios = sr3xtbl::GetInt32(row, "Simultaneous_NPC_Radios");
    const Node* list = sr3xtbl::FindChild(row, "Radio_Station_List");
    for (const Node* info : sr3xtbl::Children(list, "Info")) r.stations.push_back(ParseRadioStationInfo(info));
    return r;
}

// ===========================================================================
// 12. playlist_artist_track.xtbl
// ===========================================================================
PlaylistTrack ParsePlaylistTrack(const Node* row) {
    PlaylistTrack t;
    t.wwiseId = sr3xtbl::GetUInt32(row, "WWise_ID");
    t.artistName = getText(row, "Artist_Name");
    t.trackName = getText(row, "Track_Name");
    return t;
}

std::vector<PlaylistTrack> ParsePlaylistArtistTrackTable(const Document& doc) {
    std::vector<PlaylistTrack> out;
    const Node* listing = sr3xtbl::FindChild(doc.table(), "Track_Listing");
    const Node* tracks = sr3xtbl::FindChild(listing, "Tracks");
    for (const Node* row : sr3xtbl::Children(tracks, "Track")) out.push_back(ParsePlaylistTrack(row));
    return out;
}

// ===========================================================================
// 13. radio_activities.xtbl
// ===========================================================================
RadioActivity ParseRadioActivity(const Node* row) {
    RadioActivity a;
    a.level = sr3xtbl::GetUInt32(row, "Level");
    a.percentageRaw = sr3xtbl::GetUInt32(row, "Percentage");
    return a;
}

std::vector<RadioActivity> ParseRadioActivitiesTable(const Document& doc) {
    std::vector<RadioActivity> out;
    const Node* radioActivities = sr3xtbl::FindChild(doc.table(), "RadioActivities");
    const Node* chances = sr3xtbl::FindChild(radioActivities, "ChancesToPlay");
    for (const Node* row : sr3xtbl::Children(chances, "ChanceToPlay")) out.push_back(ParseRadioActivity(row));
    return out;
}

// ===========================================================================
// 14. radio_events.xtbl
// ===========================================================================
RadioEvent ParseRadioEvent(const Node* row) {
    RadioEvent e;
    e.name = getText(row, "Name");
    e.eventType = getText(row, "EventType");
    e.postTime = sr3xtbl::GetInt32(row, "Post_Time");
    e.maxTimesPlayed = sr3xtbl::GetInt32(row, "MaxTimesPlayed");
    return e;
}

std::vector<RadioEvent> ParseRadioEventsTable(const Document& doc) {
    std::vector<RadioEvent> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Event")) out.push_back(ParseRadioEvent(row));
    return out;
}

// ===========================================================================
// 15. commercial_events.xtbl
// ===========================================================================
CommercialEvent ParseCommercialEvent(const Node* row) {
    CommercialEvent e;
    e.name = getText(row, "Name");
    e.eventValue = sr3xtbl::ReadInt32Always(row, "EventValue");
    return e;
}

std::vector<CommercialEvent> ParseCommercialEventsTable(const Document& doc) {
    std::vector<CommercialEvent> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Event")) out.push_back(ParseCommercialEvent(row));
    return out;
}

// ===========================================================================
// 16. commercials.xtbl
// ===========================================================================
Commercial ParseCommercial(const Node* row) {
    Commercial c;
    c.name = getText(row, "Name");
    c.initialState = getText(row, "InitialState");
    c.enableEvent = getText(row, "EnableEvent");
    c.disableEvent = getText(row, "DisableEvent");
    c.length = sr3xtbl::ReadInt32Always(row, "Length");
    return c;
}

std::vector<Commercial> ParseCommercialsTable(const Document& doc) {
    std::vector<Commercial> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Commercial")) out.push_back(ParseCommercial(row));
    return out;
}

int32_t ResolveCommercialEventValue(const std::string& eventName, const std::vector<CommercialEvent>& commercialEvents) {
    const uint32_t hash = sr3xtbl::NameHash(eventName);
    for (const CommercialEvent& ce : commercialEvents) {
        if (!ce.name || !ce.eventValue.present) continue;
        if (ce.NameHash() == hash) return ce.eventValue.value;
    }
    return -1;
}

// ===========================================================================
// 17. voc_sb_line_sit.xtbl
// ===========================================================================
VocSbLineSit ParseVocSbLineSit(const Node* row) {
    VocSbLineSit e;
    e.personaId = sr3xtbl::GetUInt32(row, "Persona_id");
    e.soundbank = sr3xtbl::CopyText(row, "Soundbank", 0x41);
    e.numLineSituations = sr3xtbl::GetUInt32(row, "Num_line_situations");
    return e;
}

std::vector<VocSbLineSit> ParseVocSbLineSitTable(const Document& doc) {
    std::vector<VocSbLineSit> out;
    const Node* entries = sr3xtbl::FindChild(doc.table(), "Entries");
    for (const Node* row : sr3xtbl::Children(entries, "Entry")) out.push_back(ParseVocSbLineSit(row));
    return out;
}

}  // namespace sr3tables_audio_radio

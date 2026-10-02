// Synthetic tests for sr3tables_audio_radio, built FROM THE TEXT of
// spec-tables-audio-radio.md only (hand-rolled XML fixtures as string
// literals) - never derived from this project's own reader source. A pass
// here proves the reader implements the spec's text, not that the spec is
// right; the real-data statement is
// tools/validation/validate_tables_audio_radio_population.cpp.
//
// One test function per implemented table (16, matching spec 1.1's full
// list). Each covers at least: a normal row; an absent optional element
// giving nullopt/present==false (or the spec's own OrDefault()); and, where
// the spec calls one out, a boundary/quirk value (audio_banks.xtbl's
// wwise_id bespoke digit-prefix grammar and its case-sensitive True/False/
// Init compares; foley_touch.xtbl's case-SENSITIVE Name hash vs every
// sibling table's case-INsensitive one; audio_personas.xtbl's demographic
// suffix/age-token derivation; radio_stations.xtbl's Station_flags; the
// commercials.xtbl <-> commercial_events.xtbl cross-table resolution).
//
// MUTATION-TESTING NOTE: before finalising, every test below was confirmed
// to actually fail when the corresponding element name or offset in
// src/tables_audio_radio.cpp was deliberately misspelled/altered, then
// reverted - see the task report for specifics. This file's own text is not
// itself the mutation log; it is the fixture set that log was produced
// against.

#include <iostream>
#include <string>
#include <vector>

#include "sr3tables_audio_radio/tables.h"

namespace {

using namespace sr3tables_audio_radio;
using namespace sr3xtbl;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ \
                      << "\n";                                                 \
            ++g_failures;                                                     \
        }                                                                      \
    } while (0)

Document P(std::string_view s) { return ParseDocument(s); }

bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

// ===========================================================================
// Bespoke wwise_id digit-prefix grammar (spec 1.3 item 1)
// ===========================================================================
void testWwiseIdDigitPrefixGrammar() {
    CHECK(ParseWwiseIdDigitPrefix("12345") == 12345u);
    CHECK(ParseWwiseIdDigitPrefix("007") == 7u);
    // Stops at the first non-digit; kept ONLY if the stop char is '\0' or '.'.
    CHECK(ParseWwiseIdDigitPrefix("12345.678") == 12345u);
    CHECK(ParseWwiseIdDigitPrefix("12345abc") == 0u);  // stop char 'a' -> whole parse returns 0
    CHECK(ParseWwiseIdDigitPrefix("0x10") == 0u);       // stop char 'x' -> 0 (NOT the engine's 0x-hex special case)
    CHECK(ParseWwiseIdDigitPrefix("-123") == 0u);        // leading '-' aborts to 0 IMMEDIATELY
    CHECK(ParseWwiseIdDigitPrefix("") == 0u);             // no digits at all; implicit '\0' stop -> kept -> 0
    CHECK(ParseWwiseIdDigitPrefix("abc") == 0u);          // stop char 'a' at position 0 -> 0
}

// ===========================================================================
// 2. audio_banks.xtbl
// ===========================================================================
void testAudioBank() {
    Document d = P(R"(<root><Table>
      <NewEntity>
        <Name>Init</Name>
        <wwise_id>123456</wwise_id>
        <streaming>False</streaming>
        <load_at_boot>True</load_at_boot>
        <ram_bank_pc>True</ram_bank_pc>
        <cacheable_pc>True</cacheable_pc>
        <ram_size_pc>4096</ram_size_pc>
        <voice>true</voice>
      </NewEntity>
      <NewEntity>
        <Name>ambient_bank</Name>
        <wwise_id>999.5</wwise_id>
        <streaming>false</streaming>
        <load_at_boot>true</load_at_boot>
      </NewEntity>
    </Table></root>)");
    std::vector<AudioBank> rows = ParseAudioBanksTable(d);
    CHECK(rows.size() == 2);

    const AudioBank& b0 = rows[0];
    CHECK(b0.name.has_value() && *b0.name == "Init");
    CHECK(b0.IsInit());
    CHECK(b0.wwiseId.present && b0.wwiseId.value == 123456u);
    CHECK(b0.StreamingOrDefault() == false);  // exactly "False" -> false
    CHECK(b0.LoadAtBootOrDefault() == true);   // exactly "True" -> true
    CHECK(b0.RamBankPcOrDefault() == true);
    CHECK(b0.CacheablePcOrDefault() == true);
    CHECK(b0.RamSizePcOrDefault() == 4096u);
    CHECK(b0.VoiceOrDefault() == true);  // voice is case-INSENSITIVE: lowercase "true" still counts
    CHECK(b0.IsBootLoad(true) == true);   // Name==Init alone qualifies regardless of file
    CHECK(b0.IsBootLoad(false) == true);

    const AudioBank& b1 = rows[1];
    CHECK(!b1.IsInit());
    // wwise_id "999.5": bespoke grammar stops at '.', kept -> 999.
    CHECK(b1.wwiseId.present && b1.wwiseId.value == 999u);
    // spec 1.3 item 3: the True/False compares are case-SENSITIVE - lower-case
    // "false"/"true" do NOT match, so both stay at their DEFAULT (true, false).
    CHECK(b1.StreamingOrDefault() == true);    // "false" != "False" -> stays default true
    CHECK(b1.LoadAtBootOrDefault() == false);  // "true" != "True" -> stays default false
    CHECK(b1.RamSizePcOrDefault() == 0u);       // absent -> default 0
    CHECK(!b1.voice.has_value());
    CHECK(b1.VoiceOrDefault() == false);
    // load_at_boot="true" (lower-case) never matched "True", so even for a base-game file this stays false;
    // for a DLC file it would be ignored regardless.
    CHECK(b1.IsBootLoad(true) == false);
    CHECK(b1.IsBootLoad(false) == false);

    // A row missing Name/wwise_id entirely: the hard-fail condition (spec 2.2) is NOT enforced by this reader
    // (see the header banner) - the row is still returned, with the missing field(s) simply absent.
    Document d2 = P(R"(<root><Table><NewEntity><streaming>True</streaming></NewEntity></Table></root>)");
    std::vector<AudioBank> rows2 = ParseAudioBanksTable(d2);
    CHECK(rows2.size() == 1);
    CHECK(!rows2[0].name.has_value());
    CHECK(!rows2[0].wwiseId.present && rows2[0].wwiseId.value == 0u);
}

// ===========================================================================
// 3. audio_constants.xtbl
// ===========================================================================
void testAudioConstants() {
    CHECK(!ParseAudioConstants(P("<root><Table></Table></root>")).has_value());  // absent -> nullopt (judgment call)

    Document d = P(R"(<root><Table><AudioConstants>
      <PlayTimers>
        <BrassCollision>200</BrassCollision><GlassShatter>1</GlassShatter><BulletImpactHuman>1</BulletImpactHuman>
        <BulletImpactWall>1</BulletImpactWall><ObjectDebris>1</ObjectDebris><VehicleImpactCollision>1</VehicleImpactCollision>
        <VehicleImpactDistance>12</VehicleImpactDistance><VehicleScrapeCollision>1</VehicleScrapeCollision>
        <VehicleScrapeDistance>4</VehicleScrapeDistance><RagdollBoneImpactCollision>1</RagdollBoneImpactCollision>
        <SmallDeformation>1</SmallDeformation><LargeDeformation>1000</LargeDeformation>
      </PlayTimers>
      <OnFootSettings><FootstepRange>3</FootstepRange></OnFootSettings>
      <DrivingSettings>
        <AmbientSpawnAcquireRadio>50</AmbientSpawnAcquireRadio>
        <Alarm><Percentage>0.5</Percentage><TimeMin>10</TimeMin><TimeMax>20</TimeMax></Alarm>
        <Passby_whoosh><min_distance_on_foot>1</min_distance_on_foot><min_distance_driving>2</min_distance_driving><min_speed>3</min_speed></Passby_whoosh>
      </DrivingSettings>
      <Wind>
        <high_altitude><min_speed>1</min_speed><max_speed>2</max_speed><wind_change_rate>3</wind_change_rate><min_altitude>4</min_altitude><max_altitude>5</max_altitude></high_altitude>
        <player_falling><min_speed>6</min_speed><max_speed>7</max_speed><parachute_speed>8</parachute_speed></player_falling>
      </Wind>
      <Player_Health><med_health>50</med_health></Player_Health>
    </AudioConstants></Table></root>)");
    std::optional<AudioConstants> ac = ParseAudioConstants(d);
    CHECK(ac.has_value());
    // spec 3, CORRECTED 2026-10-01: PlayTimers are u32 (NOT f32) - exact integer equality, no "near" needed.
    CHECK(ac->playTimerBrassCollision.present && ac->playTimerBrassCollision.value == 200u);
    CHECK(ac->playTimerLargeDeformation.value == 1000u);
    // spec 3 (count corrected 11 -> 12 by desk review 2026-09-30): ALL 12 PlayTimers values are read, each
    // distinguishable. Distinct values 101..112 in spec order prove no slot is dropped or shifted.
    {
        Document pt = P(R"(<root><Table><AudioConstants><PlayTimers>
          <BrassCollision>101</BrassCollision><GlassShatter>102</GlassShatter><BulletImpactHuman>103</BulletImpactHuman>
          <BulletImpactWall>104</BulletImpactWall><ObjectDebris>105</ObjectDebris><VehicleImpactCollision>106</VehicleImpactCollision>
          <VehicleImpactDistance>107</VehicleImpactDistance><VehicleScrapeCollision>108</VehicleScrapeCollision>
          <VehicleScrapeDistance>109</VehicleScrapeDistance><RagdollBoneImpactCollision>110</RagdollBoneImpactCollision>
          <SmallDeformation>111</SmallDeformation><LargeDeformation>112</LargeDeformation>
        </PlayTimers></AudioConstants></Table></root>)");
        std::optional<AudioConstants> p = ParseAudioConstants(pt);
        CHECK(p.has_value());
        const Always<uint32_t>* slots[12] = {
            &p->playTimerBrassCollision,           &p->playTimerGlassShatter,
            &p->playTimerBulletImpactHuman,        &p->playTimerBulletImpactWall,
            &p->playTimerObjectDebris,             &p->playTimerVehicleImpactCollision,
            &p->playTimerVehicleImpactDistance,    &p->playTimerVehicleScrapeCollision,
            &p->playTimerVehicleScrapeDistance,    &p->playTimerRagdollBoneImpactCollision,
            &p->playTimerSmallDeformation,         &p->playTimerLargeDeformation,
        };
        int presentCount = 0;
        for (int i = 0; i < 12; ++i) {
            if (slots[i]->present) ++presentCount;
            CHECK(slots[i]->value == static_cast<uint32_t>(101 + i));
        }
        CHECK(presentCount == 12);
    }
    CHECK(ac->playTimerVehicleImpactDistance.value == 12u);
    // spec 3, CORRECTED 2026-10-01: squared BY AN INTEGER MULTIPLY, not float - exact equality.
    CHECK(ac->PlayTimerVehicleImpactDistanceSquared() == 144u);
    CHECK(ac->PlayTimerVehicleScrapeDistanceSquared() == 16u);
    CHECK(ac->onFootFootstepRange.value == 3.0f);
    CHECK(near(ac->OnFootFootstepRangeSquared(), 9.0f, 1e-3f));
    CHECK(ac->drivingAlarmTimeMin.present && ac->drivingAlarmTimeMin.value == 10);
    CHECK(ac->drivingAlarmTimeMax.value == 20);
    CHECK(ac->windHighAltitudeMinSpeed.value == 1.0f && ac->windHighAltitudeMaxAltitude.value == 5.0f);
    CHECK(ac->windPlayerFallingParachuteSpeed.value == 8.0f);
    CHECK(ac->medHealth.present && ac->medHealth.value == 50);
    // spec 3's exact literal: (float)(int)50 * 0.009999999776482582 ~= 0.5
    CHECK(near(ac->MedHealthFraction(), 0.5f, 1e-4f));

    // A field never mentioned anywhere in a sparse fixture: Always-hazard, present==false, 0 stand-in
    // (NOT the engine's real indeterminate residue - see the file banner / xtbl.h).
    Document sparse = P("<root><Table><AudioConstants><PlayTimers><BrassCollision>1</BrassCollision></PlayTimers></AudioConstants></Table></root>");
    std::optional<AudioConstants> ac2 = ParseAudioConstants(sparse);
    CHECK(ac2.has_value());
    CHECK(ac2->playTimerBrassCollision.present);
    CHECK(!ac2->playTimerLargeDeformation.present && ac2->playTimerLargeDeformation.value == 0u);
    CHECK(!ac2->medHealth.present && ac2->medHealth.value == 0);
}

// ===========================================================================
// 4. audio_settings.xtbl
// ===========================================================================
void testAudioSettings() {
    CHECK(!ParseAudioSettings(P("<root><Table></Table></root>")).has_value());  // no <global_settings> -> nullopt

    // spec 4.1: the real shipped row supplies Health_adjust_rate and Doppler_multiplier but genuinely OMITS
    // Speed_of_sound - reproduced here exactly.
    Document d = P(R"(<root><Table><global_settings>
      <general_settings><Health_adjust_rate>0.5</Health_adjust_rate></general_settings>
      <Doppler_settings><Doppler_multiplier>11.0</Doppler_multiplier></Doppler_settings>
    </global_settings></Table></root>)");
    std::optional<AudioSettings> s = ParseAudioSettings(d);
    CHECK(s.has_value());
    CHECK(s->generalSettingsPresent);  // <general_settings> IS present in this fixture
    CHECK(!s->speedOfSound.has_value());
    CHECK(s->SpeedOfSoundOrDefault() == 343.5f);  // coded default applies (general_settings present, spec 4)
    CHECK(s->healthAdjustRate.has_value() && near(*s->healthAdjustRate, 0.5f, 1e-6f));
    CHECK(s->dopplerMultiplier.has_value() && near(*s->dopplerMultiplier, 11.0f, 1e-5f));
    CHECK(s->DopplerMultiplierOrDefault() == *s->dopplerMultiplier);

    // Doppler_multiplier absent -> its OrDefault applies. spec 4, CORRECTED 2026-10-01: the coded default is
    // 10.0, not 11.0 (the shipped row happens to supply an explicit 11.0 above, so retail data never exercises
    // this default).
    Document d2 = P(R"(<root><Table><global_settings><general_settings/></global_settings></Table></root>)");
    std::optional<AudioSettings> s2 = ParseAudioSettings(d2);
    CHECK(s2.has_value());
    CHECK(s2->generalSettingsPresent);  // <general_settings/> IS present, just empty
    CHECK(!s2->dopplerMultiplier.has_value());
    CHECK(s2->DopplerMultiplierOrDefault() == 10.0f);
    CHECK(!s2->healthAdjustRate.has_value());
    CHECK(s2->HealthAdjustRateOrDefault() == 0.0f);  // CONFIRMED 2026-10-01: zero-filled .data, no other writer
    CHECK(s2->SpeedOfSoundOrDefault() == 343.5f);  // general_settings present -> 343.5 default still applies

    // spec 4, CORRECTED 2026-10-01: <general_settings> ABSENT ENTIRELY (unlike d2's empty-but-present case)
    // means the 343.5 Speed_of_sound default is never primed at all - it stays at the engine's zero-filled 0.0,
    // not 343.5. <global_settings> itself is still present, so ParseAudioSettings still returns a value.
    Document d3 = P(R"(<root><Table><global_settings>
      <Doppler_settings><Doppler_multiplier>12.0</Doppler_multiplier></Doppler_settings>
    </global_settings></Table></root>)");
    std::optional<AudioSettings> s3 = ParseAudioSettings(d3);
    CHECK(s3.has_value());
    CHECK(!s3->generalSettingsPresent);
    CHECK(!s3->speedOfSound.has_value());
    CHECK(s3->SpeedOfSoundOrDefault() == 0.0f);  // NOT 343.5 - general_settings was never present to prime it
    CHECK(!s3->healthAdjustRate.has_value());
    CHECK(s3->HealthAdjustRateOrDefault() == 0.0f);
    CHECK(s3->dopplerMultiplier.has_value() && near(*s3->dopplerMultiplier, 12.0f, 1e-5f));
}

// ===========================================================================
// 5. audio_line_tags.xtbl
// ===========================================================================
void testAudioLineTag() {
    Document d = P(R"(<root><Table>
      <Audio_line><Name>line_01</Name><wwise_id>42</wwise_id></Audio_line>
      <Audio_line><Name>line_02</Name></Audio_line>
    </Table></root>)");
    std::vector<AudioLineTag> rows = ParseAudioLineTagsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "line_01");
    // spec 5, CORRECTED 2026-10-01: wwise_id is the "always" reader, not "if-present".
    CHECK(rows[0].wwiseId.present && rows[0].wwiseId.value == 42u);
    CHECK(rows[1].name.has_value() && *rows[1].name == "line_02");
    CHECK(!rows[1].wwiseId.present);
}

// ===========================================================================
// 6. audio_personas.xtbl
// ===========================================================================
void testAudioPersona() {
    auto persona = [](std::string_view name) {
        AudioPersona p;
        p.name = std::string(name);
        // spec 6, CORRECTED 2026-10-01: the demographic derivation searches `fullName` (the untruncated Name),
        // not `name` (the 0x20-byte bounded record copy) - set both here so this synthetic helper still exercises
        // the Derive*FromName() methods correctly.
        p.fullName = std::string(name);
        return p;
    };
    // spec 6: fixed suffix list, gender 1=male/2=female, ethnicity 1=White/2=Black/3=Hispanic/4=Asian.
    CHECK(persona("Ped_WM_01").DeriveGenderFromName() == 1 && persona("Ped_WM_01").DeriveEthnicityFromName() == 1);
    CHECK(persona("Ped_WF_01").DeriveGenderFromName() == 2 && persona("Ped_WF_01").DeriveEthnicityFromName() == 1);
    CHECK(persona("Ped_BM_01").DeriveGenderFromName() == 1 && persona("Ped_BM_01").DeriveEthnicityFromName() == 2);
    CHECK(persona("Ped_BF_01").DeriveGenderFromName() == 2 && persona("Ped_BF_01").DeriveEthnicityFromName() == 2);
    CHECK(persona("Ped_HM_01").DeriveGenderFromName() == 1 && persona("Ped_HM_01").DeriveEthnicityFromName() == 3);
    CHECK(persona("Ped_HF_01").DeriveGenderFromName() == 2 && persona("Ped_HF_01").DeriveEthnicityFromName() == 3);
    CHECK(persona("Ped_AM_01").DeriveGenderFromName() == 1 && persona("Ped_AM_01").DeriveEthnicityFromName() == 4);
    CHECK(persona("Ped_AF_01").DeriveGenderFromName() == 2 && persona("Ped_AF_01").DeriveEthnicityFromName() == 4);
    // No underscore at all -> both 0 (spec 6: "searched only after first confirming the name contains an
    // underscore at all").
    CHECK(persona("NoUnderscoreName").DeriveGenderFromName() == 0);
    CHECK(persona("NoUnderscoreName").DeriveEthnicityFromName() == 0);
    // Underscore present but no matching suffix token -> 0.
    CHECK(persona("Angel_Boss").DeriveGenderFromName() == 0);
    CHECK(persona("Angel_Boss").DeriveEthnicityFromName() == 0);
    // Age tokens: 1=Young, 2=Middle, 3=Elderly, 0=no match.
    CHECK(persona("Ped_Young_WM_01").DeriveAgeFromName() == 1);
    CHECK(persona("Ped_Middle_BF_01").DeriveAgeFromName() == 2);
    CHECK(persona("Ped_Elderly_HM_01").DeriveAgeFromName() == 3);
    CHECK(persona("Angel").DeriveAgeFromName() == 0);
    AudioPersona noName;  // Name absent entirely
    CHECK(noName.DeriveGenderFromName() == 0 && noName.DeriveEthnicityFromName() == 0 && noName.DeriveAgeFromName() == 0);

    Document d = P(R"(<root><Table>
      <Audio_Persona><Name>Ped_Young_WM_01</Name><wwise_id>777</wwise_id></Audio_Persona>
    </Table></root>)");
    std::vector<AudioPersona> rows = ParseAudioPersonasTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Ped_Young_WM_01");
    CHECK(rows[0].fullName.has_value() && *rows[0].fullName == "Ped_Young_WM_01");
    // spec 6, CORRECTED 2026-10-01: wwise_id is the "always" reader, not "if-present".
    CHECK(rows[0].wwiseId.present && rows[0].wwiseId.value == 777u);
    CHECK(rows[0].DeriveGenderFromName() == 1 && rows[0].DeriveEthnicityFromName() == 1 && rows[0].DeriveAgeFromName() == 1);

    // Regression test for the §6 2026-10-01 correction: the demographic suffix search must use the FULL Name
    // text (FUN_00EA48B0 given the full Name, CONFIRMED - disassembly), not the 0x20-byte-bounded record copy -
    // a Name longer than 31 bytes whose suffix falls beyond that bound must still resolve correctly.
    {
        const std::string longName = std::string(35, 'x') + "_WM";  // 38 chars; "_WM" starts at index 35, past
                                                                     // the 0x1F-byte truncation boundary of `name`
        const std::string xml =
            "<root><Table><Audio_Persona><Name>" + longName + "</Name></Audio_Persona></Table></root>";
        Document dl = P(xml);
        std::vector<AudioPersona> lrows = ParseAudioPersonasTable(dl);
        CHECK(lrows.size() == 1);
        CHECK(lrows[0].name.has_value() && lrows[0].name->size() == 0x1F);     // bounded record copy, truncated
        CHECK(lrows[0].fullName.has_value() && *lrows[0].fullName == longName);  // untruncated, used for derivation
        CHECK(lrows[0].DeriveGenderFromName() == 1);     // male (_WM) - only findable via the FULL name
        CHECK(lrows[0].DeriveEthnicityFromName() == 1);  // White
    }
}

// ===========================================================================
// 7. persona_radio_prefs.xtbl
// ===========================================================================
void testPersonaRadioPref() {
    Document d = P(R"(<root><Table>
      <Audio_Persona><Name>Ped_WM_01</Name><Radio_Station>KRHYME</Radio_Station></Audio_Persona>
    </Table></root>)");
    std::vector<PersonaRadioPref> rows = ParsePersonaRadioPrefsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Ped_WM_01");
    CHECK(rows[0].radioStation.has_value() && *rows[0].radioStation == "KRHYME");
}

// ===========================================================================
// 8. foley_collision.xtbl
// ===========================================================================
void testFoleyCollision() {
    Document d = P(R"(<root><Table>
      <FoleyCollision><Name>metal_light</Name>
        <CollisionFoleySet><MinimumSpeed>10</MinimumSpeed><MaximumSpeed>50</MaximumSpeed><Frequency>3</Frequency><Wwise_switch>metal_switch</Wwise_switch></CollisionFoleySet>
        <CollisionFoleySet><MinimumSpeed>999</MinimumSpeed></CollisionFoleySet>
      </FoleyCollision>
    </Table></root>)");
    std::vector<FoleyCollision> rows = ParseFoleyCollisionTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "metal_light");
    CHECK(rows[0].minimumSpeedRaw.present && rows[0].minimumSpeedRaw.value == 10.0f);
    CHECK(near(rows[0].MinimumSpeedMps(), 4.4704f, 1e-3f));   // 10 mph * 0.44704
    CHECK(near(rows[0].MaximumSpeedMps(), 22.352f, 1e-3f));   // 50 mph * 0.44704
    // spec 8, CORRECTED 2026-10-01: Frequency is the "always" reader, not "if-present".
    CHECK(rows[0].frequency.present && rows[0].frequency.value == 3u);
    CHECK(rows[0].wwiseSwitch.has_value() && *rows[0].wwiseSwitch == "metal_switch");
    // spec 8: only the FIRST CollisionFoleySet is ever read - the second (MinimumSpeed=999) must be ignored.
    CHECK(rows[0].minimumSpeedRaw.value != 999.0f);
}

// ===========================================================================
// 9. foley_touch.xtbl
// ===========================================================================
void testFoleyTouch() {
    Document d = P(R"(<root><Table>
      <FoleyTouch><Name>Door</Name>
        <TouchFoleySet><Frequency>7</Frequency><Wwise_switch>door_switch</Wwise_switch></TouchFoleySet>
      </FoleyTouch>
    </Table></root>)");
    std::vector<FoleyTouch> rows = ParseFoleyTouchTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "Door");
    // spec 9, CORRECTED 2026-10-01: Frequency is the "always" reader, not "if-present".
    CHECK(rows[0].frequency.present && rows[0].frequency.value == 7u);
    CHECK(rows[0].wwiseSwitch.has_value() && *rows[0].wwiseSwitch == "door_switch");
    // spec 1.3 item 2 / 9: the ONE table whose Name hash is CASE-SENSITIVE (FUN_00D9E7E0), unlike every sibling.
    CHECK(rows[0].NameHash() == sr3xtbl::NameHashCaseSensitive("Door"));
    CHECK(rows[0].NameHash() != sr3xtbl::NameHash("Door"));  // the lower-casing engine hash disagrees on mixed case
}

// ===========================================================================
// 10. foley_engine.xtbl
// ===========================================================================
void testFoleyEngine() {
    Document d = P(R"(<root><Table>
      <Engine><Name>Mustang</Name><Vehicle_Model>voltage</Vehicle_Model><NPC_Only>true</NPC_Only><dlc_framework_id>2</dlc_framework_id></Engine>
      <Engine><Name>bootlegger</Name></Engine>
    </Table></root>)");
    std::vector<FoleyEngine> rows = ParseFoleyEngineTable(d);
    CHECK(rows.size() == 2);
    const FoleyEngine& e0 = rows[0];
    CHECK(e0.name.has_value() && *e0.name == "Mustang");
    // spec 10: engine CRC-32, LOWER-CASED - "Mustang" and "mustang" must hash the same.
    CHECK(e0.NameHash() == sr3xtbl::NameHash("mustang"));
    CHECK(e0.VehicleSwitchName() == "Veh_Mustang");  // spec 10: "Veh_" + Name, exact case of Name preserved
    CHECK(e0.vehicleModel.has_value() && *e0.vehicleModel == "voltage");
    CHECK(e0.NpcOnlyOrDefault() == true);
    CHECK(e0.dlcFrameworkId.has_value() && *e0.dlcFrameworkId == 2);
    CHECK(e0.DlcFrameworkIdOrDefault() == 2);

    const FoleyEngine& e1 = rows[1];
    CHECK(!e1.vehicleModel.has_value());
    CHECK(e1.NpcOnlyOrDefault() == false);
    CHECK(!e1.dlcFrameworkId.has_value());
    CHECK(e1.DlcFrameworkIdOrDefault() == static_cast<int8_t>(0xFF));  // -1, the "not DLC" sentinel
}

// ===========================================================================
// 11. radio_stations.xtbl
// ===========================================================================
void testRadioStations() {
    CHECK(!ParseRadioSettings(P("<root><Table></Table></root>")).has_value());  // no settings <NewEntity> -> nullopt

    Document d = P(R"(<root><Table><NewEntity>
      <Simultaneous_NPC_Radios>2</Simultaneous_NPC_Radios>
      <Radio_Station_List>
        <Info><xtbl_name><Filename>krhyme.xtbl</Filename></xtbl_name><Genre>RADIO_STATION_GENRE_KRHYME</Genre>
          <Station_flags><Flag>Selectable</Flag></Station_flags><wwise_id>krhyme_wid</wwise_id></Info>
        <Info><xtbl_name><Filename>police.xtbl</Filename></xtbl_name><Genre>RADIO_STATION_GENRE_POLICE</Genre>
          <Station_flags><Flag>Police_Station</Flag><Flag>Bogus_Flag</Flag></Station_flags></Info>
        <Info><xtbl_name><Filename>news.xtbl</Filename></xtbl_name><Genre>RADIO_STATION_GENRE_NEWS</Genre>
          <Station_flags><Flag>News_Station</Flag><Flag>FBI_Station</Flag></Station_flags></Info>
      </Radio_Station_List>
    </NewEntity></Table></root>)");
    std::optional<RadioSettings> rs = ParseRadioSettings(d);
    CHECK(rs.has_value());
    CHECK(rs->simultaneousNpcRadios.has_value() && *rs->simultaneousNpcRadios == 2);
    CHECK(rs->stations.size() == 3);

    const RadioStationInfo& s0 = rs->stations[0];
    CHECK(s0.filename.has_value() && *s0.filename == "krhyme.xtbl");
    CHECK(s0.genre.has_value() && *s0.genre == "RADIO_STATION_GENRE_KRHYME");
    CHECK(s0.selectable && !s0.policeStation && !s0.fbiStation && !s0.newsStation);
    CHECK(s0.wwiseId.has_value() && *s0.wwiseId == "krhyme_wid");

    const RadioStationInfo& s1 = rs->stations[1];
    CHECK(!s1.selectable && s1.policeStation && !s1.fbiStation && !s1.newsStation);  // "Bogus_Flag" silently ignored
    CHECK(!s1.wwiseId.has_value());

    const RadioStationInfo& s2 = rs->stations[2];
    CHECK(s2.newsStation && s2.fbiStation && !s2.selectable && !s2.policeStation);

    // Missing Simultaneous_NPC_Radios -> spec default 1.
    Document d2 = P(R"(<root><Table><NewEntity><Radio_Station_List/></NewEntity></Table></root>)");
    std::optional<RadioSettings> rs2 = ParseRadioSettings(d2);
    CHECK(rs2.has_value());
    CHECK(!rs2->simultaneousNpcRadios.has_value());
    CHECK(rs2->SimultaneousNpcRadiosOrDefault() == 1);
    CHECK(rs2->stations.empty());
}

// ===========================================================================
// 12. playlist_artist_track.xtbl
// ===========================================================================
void testPlaylistTrack() {
    Document d = P(R"(<root><Table><Track_Listing><Tracks>
      <Track><WWise_ID>500</WWise_ID><Artist_Name>DJ Test</Artist_Name><Track_Name>Song A</Track_Name></Track>
      <Track><Artist_Name>Unknown Artist</Artist_Name></Track>
    </Tracks></Track_Listing></Table></root>)");
    std::vector<PlaylistTrack> rows = ParsePlaylistArtistTrackTable(d);
    CHECK(rows.size() == 2);
    // spec 12, CORRECTED 2026-10-01: WWise_ID is the "always" reader, not "if-present".
    CHECK(rows[0].wwiseId.present && rows[0].wwiseId.value == 500u);
    CHECK(rows[0].artistName.has_value() && *rows[0].artistName == "DJ Test");
    CHECK(rows[0].trackName.has_value() && *rows[0].trackName == "Song A");
    CHECK(!rows[1].wwiseId.present);
    CHECK(rows[1].artistName.has_value() && *rows[1].artistName == "Unknown Artist");
}

// ===========================================================================
// 13. radio_activities.xtbl
// ===========================================================================
void testRadioActivity() {
    Document d = P(R"(<root><Table><RadioActivities><ChancesToPlay>
      <ChanceToPlay><Level>1</Level><Percentage>25</Percentage></ChanceToPlay>
      <ChanceToPlay><Level>8</Level></ChanceToPlay>
    </ChancesToPlay></RadioActivities></Table></root>)");
    std::vector<RadioActivity> rows = ParseRadioActivitiesTable(d);
    CHECK(rows.size() == 2);
    // spec 13, CORRECTED 2026-10-01: Level and Percentage are both the "always" reader, not "if-present".
    CHECK(rows[0].level.present && rows[0].level.value == 1u);
    CHECK(rows[0].percentageRaw.present && rows[0].percentageRaw.value == 25u);
    CHECK(near(rows[0].PercentageAsUnsignedFloat(), 25.0f, 1e-6f));
    // spec 13, CORRECTED 2026-10-01: the denominator global is CONFIRMED as the constant 100.0 (was OPEN) - a
    // reimplementation must store the FRACTION, not the raw value.
    CHECK(near(rows[0].PercentageFraction(), 0.25f, 1e-6f));
    CHECK(rows[1].level.present && rows[1].level.value == 8u);
    CHECK(!rows[1].percentageRaw.present);
    CHECK(rows[1].PercentageAsUnsignedFloat() == 0.0f);  // value when !present
    CHECK(rows[1].PercentageFraction() == 0.0f);

    // spec 13.1's own validation example: real base-game raw values 1,2,3,5,10,15,20,25 must become fractions
    // 0.01..0.25 (summing to 0.81, per the spec's own note), placed by ROW ORDER (not by Level).
    {
        Document d2 = P(R"(<root><Table><RadioActivities><ChancesToPlay>
          <ChanceToPlay><Level>1</Level><Percentage>1</Percentage></ChanceToPlay>
          <ChanceToPlay><Level>2</Level><Percentage>2</Percentage></ChanceToPlay>
          <ChanceToPlay><Level>3</Level><Percentage>3</Percentage></ChanceToPlay>
          <ChanceToPlay><Level>4</Level><Percentage>5</Percentage></ChanceToPlay>
          <ChanceToPlay><Level>5</Level><Percentage>10</Percentage></ChanceToPlay>
          <ChanceToPlay><Level>6</Level><Percentage>15</Percentage></ChanceToPlay>
          <ChanceToPlay><Level>7</Level><Percentage>20</Percentage></ChanceToPlay>
          <ChanceToPlay><Level>8</Level><Percentage>25</Percentage></ChanceToPlay>
        </ChancesToPlay></RadioActivities></Table></root>)");
        std::vector<RadioActivity> rows2 = ParseRadioActivitiesTable(d2);
        CHECK(rows2.size() == 8);
        const float expected[8] = {0.01f, 0.02f, 0.03f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f};
        float sum = 0.0f;
        for (int i = 0; i < 8; ++i) {
            CHECK(near(rows2[i].PercentageFraction(), expected[i], 1e-5f));
            sum += rows2[i].PercentageFraction();
        }
        CHECK(near(sum, 0.81f, 1e-4f));
    }
}

// ===========================================================================
// 14. radio_events.xtbl
// ===========================================================================
void testRadioEvent() {
    Document d = P(R"(<root><Table>
      <Event><Name>news_flash</Name><EventType>News</EventType><Post_Time>3</Post_Time><MaxTimesPlayed>2</MaxTimesPlayed></Event>
      <Event><Name>commercial_break</Name><EventType>Commercial</EventType></Event>
    </Table></root>)");
    std::vector<RadioEvent> rows = ParseRadioEventsTable(d);
    CHECK(rows.size() == 2);
    const RadioEvent& e0 = rows[0];
    CHECK(e0.name.has_value() && *e0.name == "news_flash");
    CHECK(e0.NameHash() == sr3xtbl::NameHash("news_flash"));  // lower-cased engine hash
    CHECK(e0.eventType.has_value() && *e0.eventType == "News");
    CHECK(e0.postTime.has_value() && *e0.postTime == 3);
    CHECK(e0.PostTimeOrDefault() == 3);
    CHECK(e0.maxTimesPlayed.has_value() && *e0.maxTimesPlayed == 2);

    const RadioEvent& e1 = rows[1];
    CHECK(e1.eventType.has_value() && *e1.eventType == "Commercial");
    CHECK(!e1.postTime.has_value());
    CHECK(e1.PostTimeOrDefault() == 1);  // spec default
    CHECK(!e1.maxTimesPlayed.has_value());
    CHECK(e1.MaxTimesPlayedOrDefault() == 1);  // spec default

    // kRadioEventTypeNames carries all 5 documented enum literals in order.
    CHECK(kRadioEventTypeNames.size() == 5);
    CHECK(kRadioEventTypeNames[0] == "Commercial" && kRadioEventTypeNames[4] == "Police and FBI");
}

// ===========================================================================
// 15. commercial_events.xtbl
// ===========================================================================
void testCommercialEvent() {
    Document d = P(R"(<root><Table>
      <Event><Name>event_a</Name><EventValue>1</EventValue></Event>
      <Event><Name>event_b</Name><EventValue>27</EventValue></Event>
    </Table></root>)");
    std::vector<CommercialEvent> rows = ParseCommercialEventsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "event_a");
    CHECK(rows[0].NameHash() == sr3xtbl::NameHash("event_a"));
    CHECK(rows[0].eventValue.present && rows[0].eventValue.value == 1);
    CHECK(rows[1].eventValue.value == 27);
}

// ===========================================================================
// 16. commercials.xtbl
// ===========================================================================
void testCommercial() {
    Document d = P(R"(<root><Table>
      <Commercial><Name>billboard_ad</Name><InitialState>Disabled</InitialState><EnableEvent>event_a</EnableEvent><DisableEvent>event_b</DisableEvent><Length>30</Length></Commercial>
      <Commercial><Name>radio_ad</Name><Length>15</Length></Commercial>
      <Commercial><Name>lower_disabled</Name><InitialState>disabled</InitialState><Length>10</Length></Commercial>
    </Table></root>)");
    std::vector<Commercial> rows = ParseCommercialsTable(d);
    CHECK(rows.size() == 3);

    const Commercial& c0 = rows[0];
    CHECK(c0.name.has_value() && *c0.name == "billboard_ad");
    CHECK(c0.initialState.has_value() && *c0.initialState == "Disabled");
    CHECK(c0.EnabledOrDefault() == false);  // exactly "Disabled" clears it
    CHECK(c0.length.present && c0.length.value == 30);

    const Commercial& c1 = rows[1];
    CHECK(!c1.initialState.has_value());
    CHECK(c1.EnabledOrDefault() == true);  // absent -> default enabled

    const Commercial& c2 = rows[2];
    // spec 16: InitialState compare is case-INSENSITIVE - "disabled" (lower-case) still clears it (unlike
    // audio_banks.xtbl's UNRELATED case-SENSITIVE True/False compares, spec 1.3 item 3).
    CHECK(c2.EnabledOrDefault() == false);

    // Cross-table resolution (spec 16, empirically an exhaustive 100% match on real data - 16.1).
    std::vector<CommercialEvent> events = ParseCommercialEventsTable(P(R"(<root><Table>
      <Event><Name>event_a</Name><EventValue>1</EventValue></Event>
      <Event><Name>event_b</Name><EventValue>27</EventValue></Event>
    </Table></root>)"));
    CHECK(ResolveCommercialEventValue(*c0.enableEvent, events) == 1);
    CHECK(ResolveCommercialEventValue(*c0.disableEvent, events) == 27);
    CHECK(ResolveCommercialEventValue("EVENT_A", events) == 1);       // engine hash lower-cases both sides
    CHECK(ResolveCommercialEventValue("no_such_event", events) == -1);  // not-found sentinel
}

// ===========================================================================
// 17. voc_sb_line_sit.xtbl
// ===========================================================================
void testVocSbLineSit() {
    Document d = P(R"(<root><Table><Entries>
      <Entry><Persona_id>5</Persona_id><Soundbank>veh_generic</Soundbank><Num_line_situations>12</Num_line_situations></Entry>
      <Entry><Persona_id>6</Persona_id></Entry>
    </Entries></Table></root>)");
    std::vector<VocSbLineSit> rows = ParseVocSbLineSitTable(d);
    CHECK(rows.size() == 2);
    // spec 17, CORRECTED 2026-10-01: Persona_id and Num_line_situations are both the "always" reader, not
    // "if-present".
    CHECK(rows[0].personaId.present && rows[0].personaId.value == 5u);
    CHECK(rows[0].soundbank.has_value() && *rows[0].soundbank == "veh_generic");
    CHECK(rows[0].numLineSituations.present && rows[0].numLineSituations.value == 12u);
    CHECK(rows[0].ExpectedLmPcFilename() == "veh_generic.lm_pc");
    CHECK(rows[1].personaId.present && rows[1].personaId.value == 6u);
    CHECK(!rows[1].soundbank.has_value());
    CHECK(!rows[1].numLineSituations.present);
    CHECK(rows[1].ExpectedLmPcFilename().empty());
}

}  // namespace

int main() {
    try {
        testWwiseIdDigitPrefixGrammar();
        testAudioBank();
        testAudioConstants();
        testAudioSettings();
        testAudioLineTag();
        testAudioPersona();
        testPersonaRadioPref();
        testFoleyCollision();
        testFoleyTouch();
        testFoleyEngine();
        testRadioStations();
        testPlaylistTrack();
        testRadioActivity();
        testRadioEvent();
        testCommercialEvent();
        testCommercial();
        testVocSbLineSit();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed\n";
        return 1;
    }
    std::cout << g_checks << " tests passed.\n";
    return 0;
}

#pragma once

// sr3tables_audio_radio - typed readers for the audio / radio / foley
// gameplay `.xtbl` data table group (audio_banks.xtbl, audio_constants.xtbl,
// audio_line_tags.xtbl, audio_personas.xtbl, audio_settings.xtbl,
// foley_collision.xtbl, foley_engine.xtbl, foley_touch.xtbl,
// radio_activities.xtbl, radio_events.xtbl, radio_stations.xtbl,
// persona_radio_prefs.xtbl, playlist_artist_track.xtbl, voc_sb_line_sit.xtbl,
// commercial_events.xtbl, commercials.xtbl).
//
// Source: spec-tables-audio-radio.md (SPEC TEAM, agent AO), the ONLY source
// consulted for this file's schema (cleanroom boundary - see the task's HARD
// RULES). Built entirely on top of include/sr3xtbl/xtbl.h's document model
// and accessors (Node, FindChild, Children, ChildText, CopyText, Always<T>,
// GetX/ReadXAlways, NameHash/NameHashCaseSensitive, HasFlag, EnumIndex);
// nothing here was derived from the game executable, disassembly or
// decompiled code - every offset/address cited in a comment is copied
// verbatim from the spec's own prose, purely to let a reader cross-reference
// the spec section, never re-derived. This is UNRELATED to the existing
// include/sr3audio/ library (a structure-only Wwise/audio-bank BINARY
// container reader, built from spec-audio-format.md): that one reads
// `.bnk_pc`/`_media.bnk_pc` bytes, this one reads plain XML `.xtbl` gameplay
// tables. There is no overlap at the type level and nothing here touches or
// depends on sr3audio.
//
// CONVENTION (matches sr3xtbl.h / sr3tables_environment exactly):
//   * sr3xtbl::Always<T> (value + present) = the spec's "always write"
//     elements (the shared family's s32/u32/f32/bool/s8 "always" accessors,
//     spec 1.2's own catalogue: FUN_00DABC70/00DABDF0/00DACCB0/00DAC480/
//     00DAC300). When !present the ENGINE's real behaviour is an
//     indeterminate stack-residue value (the "hazard" the shared-family docs
//     describe), NOT sr3xtbl::Always<T>'s 0 stand-in.
//   * std::optional<T> = the spec's "write only if present" elements (the
//     family's *If_Present*/"if present" siblings: FUN_00DABD20/00DABE80/
//     00DACD40/00DAC510/00DAC3B0), and any element the spec states is
//     untouched or has no traced default-priming when absent.
//   * A handful of elements have a spec-STATED CONCRETE default distinct
//     from both of the above (e.g. audio_settings.xtbl's Speed_of_sound ->
//     343.5, Doppler_multiplier -> 11.0; radio_events.xtbl's Post_Time -> 1,
//     MaxTimesPlayed -> 1; foley_engine.xtbl's dlc_framework_id -> 0xFF).
//     These are modelled as std::optional<T> (the struct holds exactly what
//     was read) plus a small `...OrDefault()` helper applying the spec's own
//     documented constant.
//
// THIRD-PARTY HASH, NOT REPRODUCED. Several tables identify a row (or a
// cross-reference target) by hashing text through the AUDIO MIDDLEWARE's own
// string-to-id function (spec 1.2/1.4's `FUN_00462960`, called "Wwise/AK" by
// the spec) - a third-party (Audiokinetic Wwise) algorithm, never disclosed
// by the spec (only that it exists and is distinct from the engine's own
// CRC-32). This project's foundation implements ONLY the engine's own
// documented CRC-32 (sr3xtbl::NameHash / NameHashCaseSensitive); the Wwise/AK
// hash is NOT reproduced anywhere here, consistent with the "don't guess an
// unspecified algorithm" rule. Every field the spec says is Wwise/AK-hashed
// is surfaced here as its RAW TEXT only (audio_banks.xtbl's own `wwise_id`
// is the one exception - see 2.3, it is a bespoke digit-prefix NUMBER parse,
// not a hash at all). By contrast, the ENGINE's own CRC-32
// (FUN_00D9E8B0/FUN_00D9E740, lower-cased, and FUN_00D9E7E0, case-sensitive)
// IS fully specified and IS reproduced, via sr3xtbl::NameHash /
// NameHashCaseSensitive, exposed as `NameHash()` helper methods below where
// the spec says a row's Name goes through this hash (foley_engine.xtbl,
// foley_touch.xtbl, radio_events.xtbl, commercial_events.xtbl,
// commercials.xtbl's EnableEvent/DisableEvent).
//
// WHAT IS DELIBERATELY LEFT OUT (per "don't guess"):
//   * Fields the spec marks OPEN at the level of what the element's runtime
//     CONSUMER does are still surfaced (raw text/number) when the element
//     itself is CONFIRMED to exist (e.g. audio_personas.xtbl's record
//     +0x26/+0x2C bytes are NOT surfaced because they are not read from any
//     XML element at all - pure record defaults, nothing to parse; but
//     audio_line_tags.xtbl's Name IS surfaced even though the spec says the
//     engine reads it into a scratch buffer and never persists it - it is
//     real, present XML data, the same call this project's own
//     sr3tables_environment made for map_districts.xtbl's textLocationY).
//   * Purely derived/computed, non-XML values are not modelled as fields:
//     a row's resolved Wwise/AK hash id, a cross-table pointer resolved at
//     load time, radio_events.xtbl's +0x2C slot index (2026-10-01, CONFIRMED
//     - disassembly: an index into the shared stride-20 radio-content table
//     at 0x013BBC18, resolved by FUN_0055E470's linear scan - still not
//     modelled here, it is cross-table/derived, not XML-sourced, spec 14),
//     radio_stations.xtbl station 0's hard-coded "My Radio 85.5"/"Mix Tape"
//     (not read from any row at all - spec 11.1). Where the spec gives an
//     exact, CONFIRMED formula for a load-time transform of a field this
//     reader DOES capture (e.g. mph->m/s x0.44704, a squared distance,
//     med_health's percent-to-fraction scale, radio_activities.xtbl's own
//     Percentage/100 fraction (spec 13, denominator global DAT_012A2DD8
//     CONFIRMED 2026-10-01 as the constant double 100.0), the "Veh_"+Name
//     switch-name construction, the "<Soundbank>.lm_pc" filename
//     construction), a small helper method applies exactly that spec-stated
//     formula - never a guessed one.
//   * No table in this group has NO loader (spec 1.1: all 16 filenames have
//     exactly one code cross-reference and were fully traced) - unlike
//     sr3tables_environment's materials.xtbl, nothing is skipped here for
//     lack of a loader.
//
// A NOTED SPEC INCONSISTENCY (see AudioConstants below): spec 3's PlayTimers
// sentence labels its accessor family "if present" while citing the ALWAYS-
// WRITE addresses (FUN_00DABDF0/FUN_00DACCB0) from spec 1.2's own catalogue;
// two lines later, the same section's med_health sentence correctly calls
// FUN_00DABC70 "the shared 'always' integer reader". This reader trusts the
// address citations (the spec's own stated "evidence anchor" convention,
// spec's cleanroom-compliance note) and the correctly-labelled sentence over
// the apparently mislabelled adjective, and models the whole AudioConstants
// group as Always<T>. See the struct's own banner for detail.
//
// Row-level parsers are named ParseXxx(const sr3xtbl::Node* row) -> Xxx.
// Whole-table convenience parsers are ParseXxxTable(const sr3xtbl::Document&)
// -> std::vector<Xxx> for repeated-row tables, and ParseXxx(const
// sr3xtbl::Document&) -> std::optional<Xxx> for the single-block tables
// (audio_constants.xtbl, audio_settings.xtbl, radio_stations.xtbl's own
// settings row). All definitions are in src/tables_audio_radio.cpp.
//
// A STRUCTURAL JUDGMENT CALL, made consistently throughout: the spec states
// several TABLE-LEVEL loader guards (a fixed-capacity array with no bound
// check, an effective cap below the real row count, a per-row "abandon every
// remaining row in the file" hard-fail, a dedup-by-id skip). Matching
// sr3tables_environment's own established precedent (its lens_flares.xtbl /
// shells.xtbl / camera_shake.xtbl 8/128-row guards, "reported by the
// validation harness rather than modelled here"), NONE of these are enforced
// by the ParseXxxTable() functions below: every row actually present in the
// XML is parsed and returned, and the loader-level consequence (which rows
// would actually reach the runtime array) is a validation-harness concern -
// see tools/validation/validate_tables_audio_radio_population.cpp, which
// reproduces the spec's own headline figures (e.g. audio_line_tags.xtbl's
// 4,507/4,626 unreachable-row mismatch, spec 5.1).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_audio_radio {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// Bespoke parsers not part of sr3xtbl's shared accessor family
// ===========================================================================

// spec 1.3 item 1 / 2.3: audio_banks.xtbl's `wwise_id` bespoke digit-prefix
// parser (FUN_00464730) - NOT the shared sr3xtbl u32/s32 family, and NOT the
// same field as the Wwise/AK-HASHED `wwise_id`s used by other tables in this
// group (radio_stations.xtbl - spec 1.3 item 1's own cross-reference).
// Grammar: an optional leading '-' aborts to 0 IMMEDIATELY (rejects
// negatives outright, never returns a negative value); otherwise accumulates
// ASCII '0'-'9', stopping at the first non-digit; the accumulated value is
// kept only if the character that stopped the scan is '\0' (end of text) or
// '.' - any other trailing character (e.g. a letter) makes the WHOLE parse
// return 0. CONFIRMED empirically: all 496 real wwise_id values in the
// shipped audio_banks.xtbl match this grammar with zero exceptions (spec
// 2.4). Accumulation is ordinary unsigned multiply-and-add (wraps modulo
// 2^32 like every other engine integer grammar in this family; no
// independent overflow rule was documented for this bespoke parser).
uint32_t ParseWwiseIdDigitPrefix(std::string_view text);

// ===========================================================================
// 2. audio_banks.xtbl - the Wwise bank registry (spec 2)
// ===========================================================================
struct AudioBank {
    std::optional<std::string> name;  // Name (bounded char[0x40] copy) -> record +0x00. REQUIRED: the real loader
                                       // abandons every REMAINING <NewEntity> row in the file if either Name or
                                       // wwise_id is absent on any one row (spec 2.2) - never observed in the real
                                       // 496-row file (spec 2.4) and NOT reproduced by ParseAudioBanksTable() (see
                                       // the file banner's "structural judgment call"). The loader's own "Unknown"
                                       // fallback text for a literally-absent Name is dead code (spec 2.3) and is
                                       // not modelled either.
    bool IsInit() const { return name.has_value() && *name == "Init"; }  // case-SENSITIVE compare (spec 1.3 item 3
                                                                          // / 2.3); exactly 1 row is named "Init"
                                                                          // in the real file (spec 2.4)

    Always<uint32_t> wwiseId;  // wwise_id via ParseWwiseIdDigitPrefix() above (spec 1.3 item 1), NOT sr3xtbl's
                                // shared u32 reader -> record +0x40. Also REQUIRED, see `name` above.

    std::optional<std::string> streaming;  // streaming - case-SENSITIVE compare against exactly "False" (spec 1.3
                                            // item 3); default true/streaming. A mod authoring lower-case "false"
                                            // would silently fail to clear this (spec 1.3 item 3) - zero occurrences
                                            // of that in the real data (spec 2.4).
    bool StreamingOrDefault() const { return streaming.value_or(std::string()) != "False"; }  // -> +0x44

    std::optional<std::string> loadAtBoot;  // load_at_boot - case-SENSITIVE compare against exactly "True"; only
                                             // ever CONSULTED for base-game (non-DLC) rows - a DLC-framework bank
                                             // cannot set this itself in the real loader (spec 2.3). This raw text
                                             // is surfaced regardless of which file it came from; IsBootLoad() below
                                             // takes the base-game/DLC distinction as an explicit caller-supplied
                                             // fact rather than guessing it.
    bool LoadAtBootOrDefault() const { return loadAtBoot.has_value() && *loadAtBoot == "True"; }

    std::optional<std::string> ramBankPc;  // ram_bank_pc - case-SENSITIVE compare against exactly "True" -> +0x5b bit 3
    bool RamBankPcOrDefault() const { return ramBankPc.has_value() && *ramBankPc == "True"; }

    std::optional<std::string> cacheablePc;  // cacheable_pc - case-SENSITIVE compare against exactly "True" -> +0x5b bit 2
    bool CacheablePcOrDefault() const { return cacheablePc.has_value() && *cacheablePc == "True"; }

    std::optional<uint32_t> ramSizePc;  // ram_size_pc (u32, if-present, default 0) - NOT stored in the record
                                         // itself; only a TRIGGER: if non-zero AND the row is boot-load, the real
                                         // engine creates a dedicated Wwise memory pool sized ramSizePc+0x2000 bytes
                                         // (AK::MemoryMgr::CreatePool/SetPoolName, spec 2.3) - a load-time action,
                                         // not modelled here beyond surfacing the raw value.
    uint32_t RamSizePcOrDefault() const { return ramSizePc.value_or(0u); }

    std::optional<std::string> voice;  // voice - case-INSENSITIVE compare against "True" (the ONE boolean-like
                                        // check in this loader that is case-insensitive, spec 2.3) -> record
                                        // `+0x5B` bit 4 (`0x10`) (2026-10-01, disassembly - CORRECTED: NOT a
                                        // transient local as previously thought; the flag is also passed to the
                                        // allocator, which then runs an untraced helper (0x00464820) on the
                                        // record before hashing its name).
    bool VoiceOrDefault() const { return voice.has_value() && sr3xtbl::NameEquals(*voice, "True"); }

    // spec 2.3/11.2/17: the combined "boot-load" flag (+0x54 bit 0x8000) other tables in this group (notably
    // voc_sb_line_sit.xtbl, spec 17) gate on. `isBaseGameFile` must be supplied by the caller (true for plain
    // audio_banks.xtbl, false for a dlcN_audio_banks.xtbl load) since a single row's own data cannot tell which
    // file it came from and load_at_boot is only ever consulted for base-game rows (spec 2.3).
    bool IsBootLoad(bool isBaseGameFile) const { return IsInit() || (isBaseGameFile && LoadAtBootOrDefault()); }

    // record `+0x5A` (byte) = the DLC index the row's own FILE was loaded from (0 for plain audio_banks.xtbl, N
    // for dlcN_audio_banks.xtbl) - 2026-10-01, disassembly. Like `isBaseGameFile` above, this is a caller-known
    // fact about WHICH FILE was opened, not a per-row XML element; not modelled as a struct field here for the
    // same reason `isBaseGameFile` is a parameter rather than a field, not because it is uncertain (spec 2.3).
};
AudioBank ParseAudioBank(const Node* row);
// Row `<NewEntity>` directly under `<Table>` (spec 2.1). DLC-aware: `dlc%d_audio_banks.xtbl` for dlc_index>0,
// else plain `audio_banks.xtbl` (spec 2.1) - a file-naming concern for the caller, not this parser. Record
// allocation in the real engine is from a fixed free list of UNRESOLVED capacity (spec 2.3, OPEN); the shipped
// 496-row file does not exhaust it and this parser has no cap of its own.
std::vector<AudioBank> ParseAudioBanksTable(const Document& doc);

// ===========================================================================
// 3. audio_constants.xtbl - global audio tuning constants (spec 3)
// ===========================================================================
// Single-row globals table (`<AudioConstants>` directly under `<Table>`), read once into a flat set of named
// globals - no runtime array, no per-row record (spec 3). SEE THE FILE BANNER for the noted spec inconsistency
// this struct resolves by trusting the address citations: every field here is modelled Always<T>, not
// optional<T>, including the PlayTimers/OnFootSettings/DrivingSettings/Wind groups the spec's own prose calls
// "if present" while citing the ALWAYS-write accessor addresses.
struct AudioConstants {
    // PlayTimers (12 u32, NOT f32 - spec-tables-audio-radio.md §3: count corrected from 11 to 12 by desk review
    // 2026-09-30, the 12 listed names; type corrected 2026-10-01, disassembly - CORRECTED: every one is read by
    // the shared "always" u32 reader FUN_00DABDF0 into consecutive dword globals 0x013BB440-0x013BB46C, i.e.
    // these are INTEGERS, not floats - a reimplementation that stores them as floats (or squares the two
    // distances below in float) diverges from the engine's integer values, Team B relevance per the 2026-10-01
    // re-derivation).
    Always<uint32_t> playTimerBrassCollision;
    Always<uint32_t> playTimerGlassShatter;
    Always<uint32_t> playTimerBulletImpactHuman;
    Always<uint32_t> playTimerBulletImpactWall;
    Always<uint32_t> playTimerObjectDebris;
    Always<uint32_t> playTimerVehicleImpactCollision;
    Always<uint32_t> playTimerVehicleImpactDistance;  // RAW; engine squares it in place after load BY AN INTEGER
                                                       // MULTIPLY (distance^2 as an integer, wraps modulo 2^32 -
                                                       // presumably compared against a squared position-delta to
                                                       // avoid a sqrt, spec 3, corrected 2026-10-01)
    uint32_t PlayTimerVehicleImpactDistanceSquared() const {
        return playTimerVehicleImpactDistance.value * playTimerVehicleImpactDistance.value;
    }
    Always<uint32_t> playTimerVehicleScrapeCollision;
    Always<uint32_t> playTimerVehicleScrapeDistance;  // RAW; squared likewise, by an integer multiply
    uint32_t PlayTimerVehicleScrapeDistanceSquared() const {
        return playTimerVehicleScrapeDistance.value * playTimerVehicleScrapeDistance.value;
    }
    Always<uint32_t> playTimerRagdollBoneImpactCollision;
    Always<uint32_t> playTimerSmallDeformation;
    Always<uint32_t> playTimerLargeDeformation;

    Always<float> onFootFootstepRange;  // OnFootSettings/FootstepRange - RAW; squared in place
    float OnFootFootstepRangeSquared() const { return onFootFootstepRange.value * onFootFootstepRange.value; }

    Always<float> drivingAmbientSpawnAcquireRadio;  // DrivingSettings/AmbientSpawnAcquireRadio
    Always<float> drivingAlarmPercentage;           // DrivingSettings/Alarm/Percentage
    Always<uint16_t> drivingAlarmTimeMin;           // DrivingSettings/Alarm/TimeMin (read as u32, truncated to u16)
    Always<uint16_t> drivingAlarmTimeMax;           // DrivingSettings/Alarm/TimeMax
    Always<float> drivingPassbyWhooshMinDistanceOnFoot;   // DrivingSettings/Passby_whoosh/min_distance_on_foot
    Always<float> drivingPassbyWhooshMinDistanceDriving;  // .../min_distance_driving
    Always<float> drivingPassbyWhooshMinSpeed;            // .../min_speed

    Always<float> windHighAltitudeMinSpeed, windHighAltitudeMaxSpeed, windHighAltitudeChangeRate;
    Always<float> windHighAltitudeMinAltitude, windHighAltitudeMaxAltitude;  // Wind/high_altitude/*
    Always<float> windPlayerFallingMinSpeed, windPlayerFallingMaxSpeed,
        windPlayerFallingParachuteSpeed;  // Wind/player_falling/*

    Always<int32_t> medHealth;  // Player_Health/med_health - s32, the shared "always" INTEGER reader (FUN_00DABC70,
                                 // CORRECTLY labelled by the spec's own prose here - not a float, spec 3)
    // stored = (float)(int)med_health * 0.009999999776482582 - spec 3's own exact literal (a genuine global
    // constant, a percent-to-fraction scale, ~0.01).
    float MedHealthFraction() const { return static_cast<float>(medHealth.value) * 0.009999999776482582f; }
};
// nullopt if <AudioConstants> itself is absent - a JUDGMENT CALL (not explicitly stated by spec 3), following
// this project's own motion_blur.xtbl precedent for single-row globals tables (sr3tables_environment).
std::optional<AudioConstants> ParseAudioConstants(const Document& doc);

// ===========================================================================
// 4. audio_settings.xtbl - global Wwise-adjacent tuning (spec 4)
// ===========================================================================
struct AudioSettings {
    // Whether the `<general_settings>` child itself exists at all (spec 4, 2026-10-01, disassembly). This GATES
    // the 343.5 Speed_of_sound default below: `FUN_00467F40` only primes that default when `general_settings`
    // is present, immediately before reading `Speed_of_sound`; if `general_settings` is absent entirely, nothing
    // primes the global and it stays at the engine's zero-filled `.data` value (0.0) - CORRECTED 2026-10-01, this
    // reader previously applied the 343.5 default unconditionally.
    bool generalSettingsPresent = false;

    std::optional<float> speedOfSound;  // general_settings/Speed_of_sound (if-present); the real shipped row
                                         // GENUINELY omits this element but DOES carry `general_settings` itself
                                         // (spec 4.1), so the coded default of 343.5 m/s applies on retail data.
    float SpeedOfSoundOrDefault() const { return speedOfSound.value_or(generalSettingsPresent ? 343.5f : 0.0f); }

    std::optional<float> healthAdjustRate;  // general_settings/Health_adjust_rate (if-present). UNLIKE every other
                                             // "if present" field in this group, no code primes a default for this
                                             // one - but 2026-10-01, disassembly CONFIRMED the fallback anyway:
                                             // `0x0317297C` sits in the zero-filled `.data` block with no writer
                                             // other than this very reader, so an absent element leaves it at
                                             // 0.0 - no longer OPEN, a real confirmed fact, not an invented
                                             // default. (The file's own embedded TableDescription authoring-tool
                                             // metadata separately documents 0.75 for this field - irrelevant to
                                             // the runtime default, spec 4.1, not modelled - attributes are never
                                             // visited by row readers, xtbl.h 1.)
    float HealthAdjustRateOrDefault() const { return healthAdjustRate.value_or(0.0f); }

    std::optional<float> dopplerMultiplier;  // Doppler_settings/Doppler_multiplier (if-present); default-primed
                                              // UNCONDITIONALLY by `FUN_00467F40` before the `Doppler_settings`
                                              // lookup (regardless of whether `general_settings` exists) -> the
                                              // float at `0x01118D20` = **10.0** (2026-10-01, disassembly -
                                              // CORRECTED from 11.0; the shipped row supplies an explicit 11.0,
                                              // so retail data never actually falls back to this default, §4.1).
    float DopplerMultiplierOrDefault() const { return dopplerMultiplier.value_or(10.0f); }
};
// nullopt if the gating <global_settings> element itself is absent (spec 4: "only if a <global_settings> child
// exists at all"). Path assumed general_settings/Doppler_settings nested INSIDE global_settings, matching this
// spec's consistent "X -> Y" child-element notation used everywhere else in the document - a JUDGMENT CALL,
// spec 4's own prose does not spell the nesting out as explicitly as most other tables in this group do.
std::optional<AudioSettings> ParseAudioSettings(const Document& doc);

// ===========================================================================
// 5. audio_line_tags.xtbl - a striking, confirmed capacity mismatch (spec 5)
// ===========================================================================
struct AudioLineTag {
    std::optional<std::string> name;  // Name - read into a LOADER SCRATCH BUFFER, never persisted by the runtime
                                       // array itself (spec 5); kept here only because it is real, present XML
                                       // data - the same call sr3tables_environment's map_districts.xtbl
                                       // textLocationY precedent made for a field the engine reads but discards.
    Always<uint32_t> wwiseId;  // wwise_id (u32, **"always"**, FUN_00DABDF0 - written straight into the slot;
                                       // 2026-10-01, disassembly - CORRECTED from "if-present": an absent element
                                       // still consumes a slot, with whatever the "always" reader leaves for a
                                       // missing child, see xtbl.h's Always<T> convention). Only the FIRST 119 rows
                                       // (index 0..118) of the real file ever reach the runtime array
                                       // DAT_01504484 (a 120-slot allocation, count tested AFTER the store and
                                       // breaking once it reaches 119, spec 5) - a table-level loader cap, NOT
                                       // enforced by ParseAudioLineTagsTable() below (see the file banner); the
                                       // real shipped file has 4,626 rows against this 119 cap, a CONFIRMED
                                       // 4,507/4,626 (97%) unreachable-row mismatch (spec 5.1) reproduced by
                                       // tools/validation/validate_tables_audio_radio_population.cpp. 2026-10-01:
                                       // the cross-reference listing for the array and its count shows NO
                                       // consumer outside this loader - no other function reads them by name.
};
AudioLineTag ParseAudioLineTag(const Node* row);
// Row `<Audio_line>` directly under `<Table>` (CONFIRMED not nested, spec 5).
std::vector<AudioLineTag> ParseAudioLineTagsTable(const Document& doc);

// ===========================================================================
// 6. audio_personas.xtbl - persona demographic classification (spec 6)
// ===========================================================================
// spec 6: fixed priority order, first substring match wins, searched ONLY if Name contains an underscore at
// all. index -> (gender, ethnicity): 0 _WM(male,White) 1 _WF(female,White) 2 _BM(male,Black) 3 _BF(female,Black)
// 4 _HM(male,Hispanic) 5 _HF(female,Hispanic) 6 _AM(male,Asian) 7 _AF(female,Asian). CONFIRMED disassembly (full
// body of FUN_00709F40 read); the substring search itself (FUN_00EA48B0) was not independently decompiled, only
// its call pattern, so this project cannot confirm whether the match is case-sensitive - modelled here as an
// exact-case (case-SENSITIVE) substring search, since the spec explicitly calls it "a plain substring search"
// with no case-folding mentioned (unlike the element-NAME lookups the shared family documents as
// case-insensitive) - a JUDGMENT CALL, flagged honestly.
// LABEL: spec-tables-audio-radio.md §6 [CONFIRMED - disassembly, 2026-10-01, re-derived (job
// 20261001T123123-team-a-ytgi), was OPEN before this pass]: FUN_00EA48B0 is the C runtime's `strstr`, byte-exact
// and CASE-SENSITIVE, and it is given the row's FULL `Name` text - NOT the 0x20-byte-bounded copy stored in the
// persisted record. This reader therefore keeps a separate, untruncated copy of `Name` (`fullName` below) for
// the three Derive*FromName() methods to search, distinct from `name` (the bounded record copy) - see `fullName`.
inline constexpr std::array<std::string_view, 8> kAudioPersonaDemographicSuffixes = {
    "_WM", "_WF", "_BM", "_BF", "_HM", "_HF", "_AM", "_AF",
};
// spec 6: independently searched (same substring-search mechanism), first match wins.
inline constexpr std::array<std::string_view, 3> kAudioPersonaAgeTokens = {
    "Young", "Middle", "Elderly",
};
struct AudioPersona {
    std::optional<std::string> name;  // Name (bounded char[0x20] copy) -> record +0x00-0x1F. This is the
                                       // PERSISTED record copy (used for e.g. by-name lookup, spec 7's
                                       // FUN_00709EB0) - the demographic derivation below does NOT search this
                                       // truncated copy, see `fullName`.
    std::optional<std::string> fullName;  // Name, UNTRUNCATED (spec 6, CONFIRMED 2026-10-01, disassembly): the
                                           // three Derive*FromName() methods search this raw text, not the
                                           // 0x20-byte bounded copy above - a Name longer than 31 bytes whose
                                           // demographic suffix falls beyond that bound would be missed if the
                                           // bounded copy were searched instead. Real base-game names are short
                                           // enough that this has no observed effect on retail data (§6.1), but is
                                           // a real, disassembly-confirmed distinction.
    Always<uint32_t> wwiseId;  // wwise_id (u32, **"always"**, FUN_00DABDF0; 2026-10-01, disassembly - CORRECTED
                                       // from "if-present") -> +0x20. The real loader DEDUPLICATES by wwise_id (a
                                       // persona whose id already exists in the table is skipped, linear scan on
                                       // +0x20, spec 6) - NOT enforced by ParseAudioPersonasTable() below (see the
                                       // file banner); use the validation harness to check for real duplicates.
                                       // OPEN (2026-10-01): what the "always" reader leaves for a missing
                                       // wwise_id (callee 0x00DABDF0 not dumped) - if 0, all such rows after the
                                       // first would collapse onto one record under the real loader's dedup rule.
    // record +0x24 (default 0xFF) is NOT read from any XML element by THIS table's own loader - it is exactly
    // the byte persona_radio_prefs.xtbl later patches (see PersonaRadioPref below, spec 7). +0x26 (default
    // 0xFFFF) is OPEN (spec 6/20 item 7), a pure record default, not derived from any XML element at all - not
    // modelled. +0x2C is a u16, zeroed by the fill helper (not read from any element either; width CONFIRMED
    // 2026-10-01, disassembly) - also not modelled, same reason.

    // 1=male, 2=female, 0=no match / Name has no underscore at all (spec 6)
    int DeriveGenderFromName() const;
    // 1=White, 2=Black, 3=Hispanic, 4=Asian, 0=no match (same suffix search as gender - spec 6)
    int DeriveEthnicityFromName() const;
    // 1=Young, 2=Middle, 3=Elderly, 0=no match (spec 6)
    int DeriveAgeFromName() const;
};
AudioPersona ParseAudioPersona(const Node* row);
// Row `<Audio_Persona>` directly under `<Table>`; DLC-aware - a SIGN test on the index argument (non-negative
// selects dlc%d_audio_personas.xtbl, negative the plain file; 2026-10-01, disassembly - CORRECTED from "> 0",
// unlike audio_banks.xtbl's own DLC selection), a caller-known fact about which file was opened, not modelled as
// a per-row field, same reasoning as AudioBank's `+0x5A` note above. The real fill helper (FUN_00709F40) returns
// NULL once the running count reaches the 300-entry cap, but the LOADER IGNORES that and keeps walking every
// remaining row without filling further records (2026-10-01, disassembly - CORRECTED: previously described as
// the loader itself failing/returning null at the cap) - NOT enforced here either way, since no cap is applied
// by ParseAudioPersonasTable() below (see the file banner).
std::vector<AudioPersona> ParseAudioPersonasTable(const Document& doc);

// ===========================================================================
// 7. persona_radio_prefs.xtbl - patches audio_personas.xtbl's unused +0x24 (spec 7)
// ===========================================================================
struct PersonaRadioPref {
    std::optional<std::string> name;  // Name -> resolved to an EXISTING audio_personas.xtbl record via
                                       // FUN_00709EB0, CONFIRMED - disassembly 2026-10-01: a case-insensitive
                                       // `_stricmp` linear scan over the table §6 built; a Name matching no
                                       // persona skips the row entirely (spec 7).
    std::optional<std::string> radioStation;  // Radio_Station -> a station index via FUN_0055DD50, CONFIRMED -
                                               // disassembly 2026-10-01 (was OPEN before this pass): the text is
                                               // Wwise/AK-hashed (FUN_00462960) and compared against each
                                               // station's own `+0x140` id (itself the Wwise/AK hash of that
                                               // station's `wwise_id` element, §11.2) in index order starting at
                                               // the hard-coded station 0 (whose `+0x140` is always 0, §11.3) -
                                               // the earlier "stem match against Genre" reading was coincidental,
                                               // not the real mechanism. The stored byte is (matched index) + 1 -
                                               // 1 would mean station 0 (the mix tape), 2 means the first
                                               // `radio_stations.xtbl` `<Info>` row, etc. - or 0xFF when nothing
                                               // matches OR the input hash is 0 (empty/unset text); since the
                                               // input hash is checked non-zero before comparing, station 0's
                                               // always-0 `+0x140` can never itself be matched in practice. NOT
                                               // implemented here as an actual resolver: the match is by the
                                               // Wwise/AK hash, a third-party algorithm this project does not
                                               // reproduce (see the file banner) - raw text is kept on both sides
                                               // (this field and RadioStationInfo::wwiseId below) instead; §7.1
                                               // confirms every one of the 9 distinct real Radio_Station values is
                                               // literally equal (as TEXT) to a real station's wwise_id text, a
                                               // 100%-empirical stand-in for the hash match on retail data, but
                                               // not the same operation and not assumed to hold for modded data.
                                               // Written into the resolved persona record's own +0x24 (default
                                               // 0xFF = "no station preference").
};
PersonaRadioPref ParsePersonaRadioPref(const Node* row);
// Row `<Audio_Persona>` directly under `<Table>` - the SAME row-element name as audio_personas.xtbl's own table
// (spec 6), but a genuinely separate file/loader (spec 7).
std::vector<PersonaRadioPref> ParsePersonaRadioPrefsTable(const Document& doc);

// ===========================================================================
// 8. foley_collision.xtbl - collision foley switches (spec 8)
// ===========================================================================
struct FoleyCollision {
    std::optional<std::string> name;  // Name (direct child of the row) -> record +0x00 name id, Wwise/AK-hashed
                                       // (third-party, NOT reproduced - see the file banner); raw text kept
    // CollisionFoleySet - ONLY the FIRST such child is ever read by the real loader (a row with more than one is
    // not an error, but the rest are silently ignored, spec 8; zero rows in the real data carry more than one,
    // spec 8's own validation) - FindChild() below naturally only returns the first match, matching this.
    Always<float> minimumSpeedRaw;  // MinimumSpeed - RAW mph; engine converts x0.44704 -> m/s IN PLACE (the same
                                     // constant spec-vehicle-data.md establishes for vehicle speeds, spec 8/18.4)
    Always<float> maximumSpeedRaw;  // MaximumSpeed - RAW mph; same conversion
    float MinimumSpeedMps() const { return minimumSpeedRaw.value * 0.44704f; }
    float MaximumSpeedMps() const { return maximumSpeedRaw.value * 0.44704f; }
    Always<uint32_t> frequency;       // Frequency (u32, **"always"**, FUN_00DABDF0, written unconditionally;
                                       // 2026-10-01, disassembly - CORRECTED from "if-present") -> +0x0C
    std::optional<std::string> wwiseSwitch;  // Wwise_switch -> +0x10 id, Wwise/AK-hashed (third-party, not
                                              // reproduced); raw text kept
};
FoleyCollision ParseFoleyCollision(const Node* row);
// Row `<FoleyCollision>` directly under `<Table>` (spec 8).
std::vector<FoleyCollision> ParseFoleyCollisionTable(const Document& doc);

// ===========================================================================
// 9. foley_touch.xtbl - touch/handling foley switches (spec 9)
// ===========================================================================
struct FoleyTouch {
    std::optional<std::string> name;  // Name (direct child of the row) -> record +0x00 name id, via the engine's
                                       // CRC-32 SIBLING ENTRY POINT `FUN_00D9E7E0` - the ONE table in this whole
                                       // group whose row-name key is CASE-SENSITIVE (NOT lower-cased), unlike every
                                       // other engine-CRC-32 Name hash in this group (spec 1.3 item 2 / 9).
    // LABEL: spec-tables-audio-radio.md §9 / §1.3 item 2 [CONFIRMED - disassembly, 2026-10-01, re-derived (job
    // 20261001T123123-team-a-ytgi), was OPEN before this pass]: at the foley_touch call site (0x00561B23-
    // 0x00561B31) the seed IS 0 and the maximum byte count is 0xFFFFFFFF (no length limit), no final XOR -
    // exactly what this reader already assumed; no behaviour change, the earlier "OUR ASSUMPTION" caveat no
    // longer applies.
    uint32_t NameHash() const { return name ? sr3xtbl::NameHashCaseSensitive(*name) : 0; }

    // TouchFoleySet - only the FIRST such child is read (same caveat as foley_collision.xtbl, spec 9).
    Always<uint32_t> frequency;       // Frequency (u32, **"always"**, FUN_00DABDF0, written unconditionally into
                                       // +0x04; 2026-10-01, disassembly - CORRECTED from "if-present") -> +0x04
    std::optional<std::string> wwiseSwitch;  // Wwise_switch -> +0x08 id, Wwise/AK-hashed (third-party, not
                                              // reproduced); raw text kept
};
FoleyTouch ParseFoleyTouch(const Node* row);
// Row `<FoleyTouch>` directly under `<Table>` (spec 9).
std::vector<FoleyTouch> ParseFoleyTouchTable(const Document& doc);

// ===========================================================================
// 10. foley_engine.xtbl - closes an open destination in spec-vehicle-data.md (spec 10)
// ===========================================================================
struct FoleyEngine {
    std::optional<std::string> name;  // Name (direct child of the row) -> +0x00 engine CRC-32, seed 0, no length
                                       // limit, LOWER-CASED per the documented shared family (spec 10)
    uint32_t NameHash() const { return name ? sr3xtbl::NameHash(*name) : 0; }
    // The ACTUAL Wwise/AK switch/state name driving the engine sound is "Veh_" + Name, NOT Name itself (spec 10)
    // - the CONSTRUCTED STRING is reproduced exactly here (a simple, confirmed sprintf-build, not a hash); the
    // Wwise/AK hash of it (-> +0x04) is third-party and not reproduced.
    std::string VehicleSwitchName() const { return name ? ("Veh_" + *name) : std::string(); }

    std::optional<std::string> vehicleModel;  // Vehicle_Model (optional child) -> +0x08, Wwise/AK-hashed (0 if
                                               // absent per the hash function's own convention - third-party, not
                                               // reproduced); raw text kept. 100% present in the real base-game
                                               // rows despite being "optional" (spec 10's own validation).
    std::optional<bool> npcOnly;  // NPC_Only (optional child, bool if-present, default false) -> +0x0C. ZERO
                                   // occurrences in the entire real base game (spec 10's own validation) - fully
                                   // supported by the reader, simply unused by retail content.
    bool NpcOnlyOrDefault() const { return npcOnly.value_or(false); }
    std::optional<int8_t> dlcFrameworkId;  // dlc_framework_id (optional child, s8 if-present, default 0xFF = "not
                                            // DLC" sentinel, matching the convention this project's other tables
                                            // use for the same sentinel) -> +0x0D
    int8_t DlcFrameworkIdOrDefault() const { return dlcFrameworkId.value_or(static_cast<int8_t>(0xFF)); }

    // CROSS-REFERENCE (spec 10/18.4, [HIGH CONFIDENCE - inferred] and [OPEN - desk review 2026-09-30]; NOT
    // CONFIRMED): the spec's reading is that a vehicle record's own Foley/Engine field (u16 at +0x7AC,
    // spec-vehicle-data.md 7.3/7.4) may index this table's pointer array DAT_013BB8A0 by row position (indices
    // assigned in file/load order across the base game and any DLC frameworks). It was never traced from the
    // vehicle side, and the spec flags it as in tension with spec-vehicle-data.md's "u16 sound id" wording and
    // with this table's own +0x00 name hash (a hash/name lookup vs positional index; DLC load order would matter
    // if positional). Which mechanism applies is OPEN, to be settled against the executable. That value is not a
    // field of THIS row and is not modelled here; row order as parsed is merely the parse order, NOT a confirmed
    // index space.
};
FoleyEngine ParseFoleyEngine(const Node* row);
// Row `<Engine>` directly under `<Table>`; DLC-aware (spec 10). Records are individually heap-allocated,
// addressed through a pointer array whose capacity was not resolved by the spec (OPEN, spec 10/20 item 6) - no
// cap enforced here.
std::vector<FoleyEngine> ParseFoleyEngineTable(const Document& doc);

// ===========================================================================
// 11. radio_stations.xtbl - the station array spec-save-format.md already reads (spec 11)
// ===========================================================================
struct RadioStationInfo {
    std::optional<std::string> filename;  // xtbl_name/Filename (nested; bounded char[0x40] copy) -> +0xC0
    std::optional<std::string> genre;     // Genre (direct child; bounded char[0x40] copy) -> +0x100. A
                                           // LOCALISATION-STRING KEY (e.g. "RADIO_STATION_GENRE_KRHYME"), NOT free
                                           // display text (spec 11.2) - and see PersonaRadioPref above for its
                                           // cross-table correspondence.

    // Station_flags/Flag children, matched case-insensitively against exactly 4 literals (an if/else chain, NOT
    // an exhaustive-enum check - any other Flag text is silently ignored, spec 11.2); these 4 bits are wholly
    // DETERMINED by this element on each load (the destination bits are cleared first), not accumulated.
    // NOTE (spec 11.1, CONFIRMED - disassembly, 2026-10-01): `+0x689` bit 5 has a DIFFERENT meaning on the
    // hard-coded station 0 (not represented by this struct at all, see RadioSettings::stations below) - there it
    // means "mix-tape list non-empty", set/cleared dynamically by 0x0055E380, NOT by any `<Info>` row's
    // `Station_flags`. The meaning below (bit 5 set together with bit 6 by `Selectable`) applies ONLY to the
    // stations 1..N this struct actually models; the real enumerator (FUN_0055DDC0) that lists selectable
    // stations tests bit 6, not bit 5, for exactly this reason.
    bool selectable = false;      // "Selectable" -> +0x689 bits 5&6 together (0x60)
    bool policeStation = false;   // "Police_Station" -> +0x68A bit 1 (0x02)
    bool fbiStation = false;      // "FBI_Station" -> +0x68A bit 2 (0x04)
    bool newsStation = false;     // "News_Station" -> +0x68A bit 3 (0x08)

    std::optional<std::string> wwiseId;  // wwise_id (direct child) -> +0x140. Text hashed via the Wwise/AK
                                          // string-to-id function - UNLIKE audio_banks.xtbl's field of the SAME
                                          // NAME, this one is NOT the bespoke digit-prefix number parse (spec 1.3
                                          // item 1 / 11.2); third-party hash, not reproduced - raw text kept. This
                                          // is the exact field spec-save-format.md's own "+0x140" already names.

    // CONFIRMED structural fact, NO counterpart element: real <Info> rows carry no <Name> child at all - the
    // in-game display string for stations 1..N does not come from this file at all (spec 11.2/11.3).
};
RadioStationInfo ParseRadioStationInfo(const Node* infoRow);

struct RadioSettings {
    std::optional<int32_t> simultaneousNpcRadios;  // Simultaneous_NPC_Radios (int, via FUN_00EA47E1, not
                                                    // independently decompiled) -> global DAT_013BBAB0, default 1
    int32_t SimultaneousNpcRadiosOrDefault() const { return simultaneousNpcRadios.value_or(1); }

    // Radio_Station_List/Info - these are stations 1..N. Station 0 is entirely HARD-CODED IN THE ENGINE, not
    // read from any row at all (spec 11.1) - it is deliberately NOT represented here since it has no source XML
    // element whatsoever, regardless of the detail below. (2026-10-01, disassembly - CORRECTED: station 0
    // actually carries TWO different hard-coded name fields, not one - `+0x40` = "My Radio 85.5" (loader
    // hard-init) and `+0x00` = "Mix Tape" (written later, only when global byte 0x013C60E4 is set and `+0x00` is
    // still empty, by 0x0055DB40); only bit 3 of `+0x689` is set by the loader itself, not bit 5 as earlier
    // believed - bit 5 is set/cleared dynamically by the mix-tape list setter 0x0055E380, see RadioStationInfo's
    // own bit-5 note above.) The real allocated station-array capacity is stations.size() + 1 (the "+1" reserves
    // station index 0, spec 11.1) - not itself a field, easily recovered by the caller as stations.size() + 1.
    std::vector<RadioStationInfo> stations;
};
// nullopt if the single settings row (`<NewEntity>` directly under `<Table>` - the file's ONE settings block,
// spec 11.1) is absent.
std::optional<RadioSettings> ParseRadioSettings(const Document& doc);

// ===========================================================================
// 12. playlist_artist_track.xtbl - the track/artist catalog (spec 12)
// ===========================================================================
struct PlaylistTrack {
    Always<uint32_t> wwiseId;  // WWise_ID (u32, **"always"**, FUN_00DABDF0; note the spec's own exact
                                       // capitalisation; 2026-10-01, disassembly - CORRECTED from "if-present") ->
                                       // +0x00. CONFIRMED (spec 12.2, full chain traced): this is the JOIN KEY a
                                       // separate runtime function uses to resolve a played song (via the
                                       // stride-20 radio-content table at 0x013BBC18 - now substantially mapped,
                                       // §20 item 3 - the same table commercials.xtbl/radio_events.xtbl share,
                                       // see Commercial below) back to display text. The real shipped file has
                                       // zero duplicate WWise_ID values (spec 12.3), though the loader itself
                                       // does not enforce uniqueness.
    std::optional<std::string> artistName;  // Artist_Name -> +0x04 (interned/localised string handle, FUN_00DB12C0)
    std::optional<std::string> trackName;   // Track_Name -> +0x08 (same helper)
};
PlaylistTrack ParsePlaylistTrack(const Node* row);
// Row `<Track_Listing>/<Tracks>/<Track>` (three levels of nesting, spec 12); the real loader reads at most 145
// (0x91) rows, silently dropping any beyond that bound - NOT enforced here (the real shipped file has 138 rows,
// under the cap, spec 12.3).
//
// STATION NUMBERING (spec 12.2, CONFIRMED - disassembly, 2026-10-01): every per-station accessor in this group
// uses 1-BASED station numbers (station 0 = the hard-coded Mix Tape, radio_stations.xtbl's `<Info>` rows are
// stations 1..N, spec 11.1). The real per-station playlist-slot bound check (`FUN_0055DFB0`) is `slot <=
// +0x5A4`, INCLUSIVE - an off-by-one that admits one slot past the real list, a confirmed real-ENGINE quirk, not
// a reader defect. Neither this table's reader nor radio_stations.xtbl's (RadioStationInfo/RadioSettings above)
// performs any station-indexed lookup or bound check at all - this is purely a schema-level XML reader, so
// nothing here currently depends on or needs to reproduce the inclusive-bound quirk; noted for any FUTURE
// consumer (e.g. a save-format or runtime playlist-queue module) that models per-station slot access.
std::vector<PlaylistTrack> ParsePlaylistArtistTrackTable(const Document& doc);

// ===========================================================================
// 13. radio_activities.xtbl - an 8-slot, unchecked-bound chance table (spec 13)
// ===========================================================================
struct RadioActivity {
    Always<uint32_t> level;  // Level (u32, **"always"**, FUN_00DABDF0; 2026-10-01, disassembly - CORRECTED from
                              // "if-present"); real base-game values are 1..8 (one-indexed, NOT 0-based, spec
                              // 13.1). CORRECTED 2026-10-01 - DESPITE THE NAME, `Level` IS NOT USED AS THE SLOT
                              // INDEX by the real engine: `FUN_0060E790` fills slots strictly in ROW ORDER (the
                              // slot pointer simply advances by 8 bytes per row, with no bound check of its own);
                              // `Level` is stored into the record but this job's traced code never reads it back
                              // as an index. Team B relevance (spec 13): "place rows by order, not by `Level`" -
                              // matches this reader's own row-order `std::vector` (see
                              // ParseRadioActivitiesTable below). Also note: only the eight `Percentage` floats
                              // are pre-zeroed by the loader before the walk - the `Level` dwords are NOT
                              // pre-written (spec 13); an absent `Level`'s "always"-reader 0-stand-in should not
                              // be read as reflecting that real pre-zeroing.
    Always<uint32_t> percentageRaw;  // Percentage (u32, **"always"**, FUN_00DABDF0; 2026-10-01, disassembly -
                                             // CORRECTED from "if-present"). The real engine reinterprets this as
                                             // SIGNED and, if negative, adds 2^32 back (spec 13) - i.e. it
                                             // corrects the raw bit pattern back to its UNSIGNED reading; this is
                                             // functionally exactly the helper below (the add-back-2^32 branch is
                                             // dead in practice on retail data - all 8 real values are
                                             // non-negative, spec 13.1 - but real, disassembly-confirmed code).
    float PercentageAsUnsignedFloat() const { return static_cast<float>(percentageRaw.value); }
    // The per-load denominator global DAT_012A2DD8 is CONFIRMED (2026-10-01, disassembly, was OPEN before this
    // pass) as the constant double **100.0** (`.rdata`, 279 read-only uses across 195 functions, no writer - the
    // earlier "reads as 0" finding was just the zero LOW DWORD of the 100.0 double literal, not an unprimed
    // global). The real computation divides in DOUBLE precision and narrows the quotient to `f32` for storage -
    // percent -> fraction (spec 13's own "Team B relevance" line: "a reimplementation must store `Percentage /
    // 100` (fraction), not the raw value"). Spec 13.1's raw values 1,2,3,5,10,15,20,25 must become
    // 0.01,0.02,0.03,0.05,0.10,0.15,0.20,0.25 (summing to 0.81, per the spec's own note) through this accessor.
    float PercentageFraction() const {
        return static_cast<float>(static_cast<double>(percentageRaw.value) / 100.0);
    }
};
RadioActivity ParseRadioActivity(const Node* row);
// Row `<RadioActivities>/<ChancesToPlay>/<ChanceToPlay>`. The row-walking loop has NO BOUND CHECK of its own
// (spec 13) - a 9th row would silently overrun a fixed 8-slot region in the real engine; NOT enforced/truncated
// here (the real shipped file has exactly 8 rows, spec 13.1). Rows fill slots in ROW ORDER, not by `Level` (see
// RadioActivity::level above) - this function's own `std::vector` already preserves document row order and
// never uses `.level` as an index anywhere in this codebase, so it already matches the real engine's slot
// assignment with no further change needed.
std::vector<RadioActivity> ParseRadioActivitiesTable(const Document& doc);

// ===========================================================================
// 14. radio_events.xtbl - a second uninitialised-memory quirk, and the EventType enum (spec 14)
// ===========================================================================
// spec 14: a fixed 5-literal enum table for EventType. ALL 13 real base-game rows use "News" (index 1) - the
// other 4 values are fully supported by the reader but have zero occurrences in retail content (spec 14.1).
inline constexpr std::array<std::string_view, 5> kRadioEventTypeNames = {
    "Commercial", "News", "Police", "FBI", "Police and FBI",
};
struct RadioEvent {
    std::optional<std::string> name;  // Name -> +0x08 engine CRC-32, LOWER-CASED. CORRECTED 2026-10-01,
                                       // disassembly (was previously described as "REQUIRED - skips the rest of
                                       // the row if absent"): an absent Name skips NOTHING - the NULL pointer is
                                       // simply handed to the hash function (-> 0, §1.2) and every OTHER field of
                                       // the row is still read and written normally. This reader's own behaviour
                                       // already matched the corrected finding (ParseRadioEvent below never
                                       // conditions any other field on Name's presence) - only this comment was
                                       // stale.
    // LABEL: spec-tables-audio-radio.md §14 [CONFIRMED - disassembly, 2026-10-01, re-derived (job
    // 20261001T123123-team-a-ytgi), was OPEN before this pass]: "Name is hashed with seed 0 and no length limit"
    // - exactly what this reader already assumed; no behaviour change.
    uint32_t NameHash() const { return name ? sr3xtbl::NameHash(*name) : 0; }

    std::optional<std::string> eventType;  // EventType (direct child) -> +0x0C enum index via
                                            // kRadioEventTypeNames (case-insensitive, sr3xtbl::EnumIndex
                                            // convention: -1 if unmatched/absent). Raw text kept, matching this
                                            // project's own time_of_day_objects.xtbl (sr3tables_environment)
                                            // precedent for an enum-selector field.

    std::optional<int32_t> postTime;  // Post_Time (s32, if-present, default 1) -> +0x10. Matches
                                       // spec-save-format.md's own description of the runtime "posted radio
                                       // events" ring's timer (spec 14).
    int32_t PostTimeOrDefault() const { return postTime.value_or(1); }

    std::optional<int32_t> maxTimesPlayed;  // MaxTimesPlayed (s32, if-present, truncated to a byte, default 1) -> +0x2F
    int32_t MaxTimesPlayedOrDefault() const { return maxTimesPlayed.value_or(1); }

    // NOTE (spec 14, 2026-10-01, disassembly - CONFIRMED, was OPEN before this pass): a further u16 slot index
    // derived from Name (Wwise/AK-hashed, then resolved via FUN_0055E470, the same "hash -> small index"
    // resolver commercials.xtbl uses - spec 16) is written to +0x2C. The consuming structure IS now identified:
    // it is the index of the matching entry (by Wwise id) in the SHARED stride-20 radio-content table at
    // 0x013BBC18 (count: s16 at 0x013C045C) - the same table §12's playlist slots and §16's Commercial records
    // live in - or 0xFFFF when none matches. Still not modelled here: it is a derived cross-table value, not
    // read from any XML element, and the Wwise/AK hash step itself remains third-party (not reproduced).
    // NOTE (spec 14): the RUNTIME record this row feeds has 16 of its own 52 bytes written by this reader (spec
    // 14, corrected by desk review 2026-09-30 from ~17); the remaining 36 bytes (was ~35) are genuine uninitialised heap memory in the real engine (the allocator's memset
    // only clears a count-scaled PREFIX of the whole buffer, not each record). This is a fact about the runtime
    // record's memory layout, not about this table's own XML schema, and is not modelled as a struct field.
};
RadioEvent ParseRadioEvent(const Node* row);
// Row `<Event>` (spec 14/19's own validation-summary naming; the row element itself is not stated as explicitly
// in §14's own prose as most other tables in this group - assumed directly under `<Table>`, the default xtbl
// shape, matching every OTHER table in this group whose row element IS explicitly stated that way - a JUDGMENT
// CALL).
std::vector<RadioEvent> ParseRadioEventsTable(const Document& doc);

// ===========================================================================
// 15. commercial_events.xtbl - the 30-slot registry spec-save-format.md already persists (spec 15)
// ===========================================================================
struct CommercialEvent {
    std::optional<std::string> name;  // Name -> engine CRC-32 (FUN_00D9E740), LOWER-CASED; stored into slot
                                       // DAT_013BBB08[EventValue]'s first dword - ONLY if EventValue < 30 (spec
                                       // 15) - that gate is NOT enforced here, see EventValue below.
    // LABEL: spec-tables-audio-radio.md §15 [CONFIRMED - disassembly, 2026-10-01, re-derived (job
    // 20261001T123123-team-a-ytgi), was OPEN before this pass]: "CRC seed 0" for Name - exactly what this reader
    // already assumed; no behaviour change.
    uint32_t NameHash() const { return name ? sr3xtbl::NameHash(*name) : 0; }

    Always<int32_t> eventValue;  // EventValue (s32, the shared "always" reader FUN_00DABC70) - the slot index.
                                  // The real loader's bound check is an UNSIGNED "above 29" test (2026-10-01,
                                  // disassembly - narrowed from "values >= 30 are dropped"): values >= 30 (0x1E)
                                  // AND every NEGATIVE value are both silently DROPPED (not clamped, nothing can
                                  // index before the array, spec 15); the real base-game file's 15 values are all
                                  // < 30 and non-negative with no duplicates (spec 15.1) - a clean,
                                  // independently-reproduced cross-check against spec-save-format.md's own "new
                                  // game has flags 1, 2, 6, 27 set" claim. NOT enforced by ParseCommercialEventsTable()
                                  // below, same structural judgment call as every other table-level loader guard
                                  // in this group (see the file banner) - ResolveCommercialEventValue() below
                                  // already only matches rows with both Name and a present eventValue, so it is
                                  // unaffected either way.
};
CommercialEvent ParseCommercialEvent(const Node* row);
// Row `<Event>` (spec 15/16 both explicitly state their own row element name without the "directly under Table"
// qualifier most other tables use - assumed directly under `<Table>`, the default xtbl shape - see the same
// judgment-call note on radio_events.xtbl above).
std::vector<CommercialEvent> ParseCommercialEventsTable(const Document& doc);

// ===========================================================================
// 16. commercials.xtbl - patches an entry of the SHARED stride-20 radio-content table (spec 16), and separately
// resolves EnableEvent/DisableEvent against commercial_events.xtbl (a genuinely different relationship, unchanged)
// ===========================================================================
struct Commercial {
    std::optional<std::string> name;  // Name - identifies a PRE-EXISTING entry of the SHARED stride-20
                                       // radio-content table at 0x013BBC18 (count: s16 at 0x013C045C) via the
                                       // Wwise/AK hash (third-party, not reproduced) through FUN_0055E470 (hash ->
                                       // index, a linear scan of the table's own +0x00) and FUN_0055DF80 (index
                                       // -> entry). CORRECTED 2026-10-01, disassembly: this is the SAME table
                                       // §12's playlist slots (PlaylistTrack::wwiseId above) and §14's
                                       // RadioEvent::+0x2C index reference - NOT a separate, dedicated
                                       // "commercial record" registry as this comment previously described. This
                                       // loader only PATCHES a matching entry (`+0x04` Length, `+0x11`/`+0x12`
                                       // enable/disable event slots, `+0x13` enabled flag - all below); a Name
                                       // whose hash matches no entry is silently DROPPED - this loader never
                                       // creates an entry, raw text kept here for identification only.

    std::optional<std::string> initialState;  // InitialState - case-INSENSITIVE _stricmp against exactly
                                                // "Disabled" (spec 16)
    bool EnabledOrDefault() const {  // -> +0x13, default true/enabled; ONLY exactly "Disabled" clears it
        return !(initialState.has_value() && sr3xtbl::NameEquals(*initialState, "Disabled"));
    }

    std::optional<std::string> enableEvent;  // EnableEvent (optional) -> +0x11 (s8, default -1). Hashed via the
                                              // ENGINE'S OWN CRC-32 (FUN_00D9E740, lower-cased) - NOT the Wwise/AK
                                              // hash used for this row's own Name (spec 16) - then resolved
                                              // against commercial_events.xtbl's Name-hash set (see
                                              // ResolveCommercialEventValue() below). Raw text kept here; 33/33
                                              // real values resolve cleanly (spec 16.1).
    std::optional<std::string> disableEvent;  // DisableEvent (optional) -> +0x12 (s8, default -1). Same
                                               // resolution as EnableEvent; 28/28 real values resolve (spec 16.1).

    Always<int32_t> length;  // Length (s32, the shared "always" reader) -> +0x04
};
Commercial ParseCommercial(const Node* row);
// Row `<Commercial>` (spec 16 - see the same judgment-call note as radio_events.xtbl/commercial_events.xtbl above).
std::vector<Commercial> ParseCommercialsTable(const Document& doc);

// spec 16: resolves `eventName` (hashed here via the ENGINE's own lower-cased CRC-32, matching
// commercial_events.xtbl's own Name hash - FUN_00D9E740 both sides) against `commercialEvents`' own NameHash()
// values - a linear scan, first match wins, requiring that matching row's own EventValue to be present. Returns
// the matching row's EventValue if found, else -1 (the loader's own "not found" sentinel, spec 16). This is the
// CONFIRMED, disassembly-traced mechanism (spec 16), empirically an exhaustive 100% match on real data (spec 16.1).
// LABEL: spec-tables-audio-radio.md §16, [OPEN - desk review 2026-09-30]: the CRC-32 seed for EnableEvent/
// DisableEvent is not stated (the 100% match holds for any seed shared by both sides). Seed 0 on both sides is
// OUR ASSUMPTION; the VALIDATED-BY-DATA result does not depend on it.
int32_t ResolveCommercialEventValue(const std::string& eventName, const std::vector<CommercialEvent>& commercialEvents);

// ===========================================================================
// 17. voc_sb_line_sit.xtbl - resolves spec-audio-format.md's `.lm_pc`/`DMLV` open item (spec 17)
// ===========================================================================
struct VocSbLineSit {
    Always<uint32_t> personaId;           // Persona_id (u32, **"always"**, FUN_00DABDF0 - read into a stack slot
                                                  // this loader never reuses; 2026-10-01, disassembly - CORRECTED
                                                  // from "if-present")
    std::optional<std::string> soundbank;        // Soundbank (bounded char[0x41] copy) - resolved against
                                                  // audio_banks.xtbl's own Name (spec 17); a clean, exhaustive
                                                  // 100% match in the real data (all 265 values found, spec 17.1)
    Always<uint32_t> numLineSituations;   // Num_line_situations (u32, **"always"**; 2026-10-01, disassembly -
                                                  // CORRECTED from "if-present")

    // spec 17: this entry is only further consulted (its Num_line_situations accumulated, and a
    // "<Soundbank>.lm_pc" file queued for open) when the MATCHING audio_banks.xtbl record is boot-loaded (its
    // own +0x54 bit 0x8000 - see AudioBank::IsBootLoad() above) - a cross-table runtime gate, NOT enforced here
    // (this reader performs no file I/O at all - pure XML schema parsing).
    //
    // DO NOT ASSUME THIS GATE OPENS A `.lm_pc` FILE AT TABLE-LOAD TIME (spec 17's own "Team B relevance" line).
    // EMPIRICALLY CONFIRMED 2026-10-01: on the ONE confirmed `+0x54` bit-15 writer found so far (audio_banks.xtbl's
    // own loader, which sets it only for base-game rows that are `Init` or `load_at_boot = True`, §2.3), **0 of
    // the 265 real `Soundbank` values ever pass this gate** - every one of them is `load_at_boot = False`,
    // `voice = True`, `streaming = True` in the real `audio_banks.xtbl` (§17.1). So `IsBootLoad()` evaluates
    // false for every real voice bank, and `ExpectedLmPcFilename()`'s result should NOT be treated as something
    // that actually gets opened on retail data through this path. Whether bit 15 is EVER set by some OTHER,
    // not-yet-found writer (candidates 0x00465CC0 / 0x004673C0) stays genuinely OPEN (spec 17/§20 item 9) - this
    // reader does not and should not guess an answer either way.
    //
    // The filename CONSTRUCTION itself is a simple, confirmed string build (Soundbank + ".lm_" + "pc"), captured
    // exactly by the helper below - this is the disassembly-confirmed mechanism that substantially advances
    // spec-audio-format.md's own open `.lm_pc`/`DMLV` question (spec 17/18.1), though that file's own interior
    // format and the `DMLV` acronym remain unresolved and out of this reader's scope (spec 20 item 8).
    std::string ExpectedLmPcFilename() const { return soundbank ? (*soundbank + ".lm_pc") : std::string(); }
};
VocSbLineSit ParseVocSbLineSit(const Node* row);
// Row `<Entries>/<Entry>` (spec 17 - assumed directly under `<Table>`, same judgment call as radio_events.xtbl
// above). Up to a 600-entry LOADER-LOCAL SCRATCH capacity (not a persistent-record cap, spec 17) - not enforced.
std::vector<VocSbLineSit> ParseVocSbLineSitTable(const Document& doc);

}  // namespace sr3tables_audio_radio

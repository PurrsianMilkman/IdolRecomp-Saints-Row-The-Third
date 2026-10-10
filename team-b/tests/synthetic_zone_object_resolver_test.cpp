// Synthetic tests for the real name<->sr3zone::ObjectRecord resolver
// (include/sr3luahost/zone_object_resolver.h, EngineState::
// loadZoneObjectsSection()/resolveZoneObject() - include/sr3luahost/
// engine_state.h) - 2026-10-08 zone-data fast-follow, wiring the existing
// `.czn_pc` Objects (0x2234) reader (sr3zone::ZoneObjects, already proven
// against real shipped data by tests/real_zone_objects_test.cpp and
// tools/zone_objects_population.cpp) into this host's own named-object
// lookup for the first time.
//
// Every fixture here builds a synthetic sr3zone::ObjectsSection/
// ObjectRecord BY HAND (every field is public and default-constructible -
// no raw byte buffer or ZoneObjects::parseAt() needed) rather than real
// shipped data - the real, shipped `Killbane` case is instead exercised
// end-to-end by a separate real-cache mission-drive run (see this task's
// own report), matching this project's established "synthetic ctest for
// mechanics, real-cache run for the real-data proof" split (same split
// tests/synthetic_luahost_test.cpp's own scriptNpcHp fixtures and
// tools/lua_host_run.cpp already use for this exact precedence rule).
//
// script_npc_hp's real wire width/kind is still OPEN (zone_objects.h's own
// DecodePropertyAsUnsignedInt doc comment: Sec11.7 never states it) - this
// test's own synthetic entries use a plain 4-byte little-endian unsigned
// int, the same width DecodePropertyAsUnsignedInt accepts and the same
// convention tests/synthetic_zone_objects_test.cpp already uses for this
// exact property.

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "sr3luahost/engine_state.h"
#include "sr3save/save_crc.h"
#include "sr3zone/zone_objects.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                    \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"       \
                      << __LINE__ << "\n";                                 \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using sr3luahost::EngineState;

// One synthetic property entry: little-endian `value` packed into exactly
// `width` bytes (1/2/4/8 - the only widths DecodePropertyAsUnsignedInt
// accepts, zone_objects.h's own doc comment).
sr3zone::ObjectPropertyEntry MakeUintProperty(uint32_t id, uint64_t value, size_t width = 4) {
    sr3zone::ObjectPropertyEntry e;
    e.id = id;
    e.size = static_cast<uint16_t>(width);
    e.kind = 0; // OPEN per zone_objects.h (Sec11.7 never states the real kind for this family) - unused by the decoder
    for (size_t i = 0; i < width; ++i) {
        e.data.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
    return e;
}

// One synthetic text property entry (script_npc_flags' own shape, Sec11.7):
// raw ASCII bytes, no trailing NUL required (DecodePropertyAsText tolerates
// one if present, zone_objects.h's own doc comment) - kind is OPEN/unused by
// the decoder, same note as MakeUintProperty above.
sr3zone::ObjectPropertyEntry MakeTextProperty(uint32_t id, const std::string& text) {
    sr3zone::ObjectPropertyEntry e;
    e.id = id;
    e.size = static_cast<uint16_t>(text.size());
    e.kind = 7; // matches tests/synthetic_zone_objects_test.cpp's own convention for a text property
    e.data.assign(text.begin(), text.end());
    return e;
}

sr3zone::ObjectRecord MakeNamedRecord(const std::string& name, uint32_t classHash) {
    sr3zone::ObjectRecord rec;
    rec.hasName = true;
    rec.nameText = name;
    rec.classHash = classHash;
    return rec;
}

} // namespace

int main() {
    // 1. A named `script_npc` record carrying a real script_npc_hp value -
    // the exact real-world shape spec-zone-data-format.md Sec11.7 documents
    // for Killbane's two full-fight copies (60000).
    {
        EngineState es;
        sr3zone::ObjectRecord killbane = MakeNamedRecord("Killbane", sr3save::nameHash("script_npc"));
        killbane.tailEntries.push_back(MakeUintProperty(sr3zone::ScriptNpcHpPropertyId(), 60000));

        // Before loading any zone, this project has exactly the documented
        // gap: an arbitrary mission-authored name like "Killbane" is OPEN,
        // not fabricated either way.
        CHECK(!es.objectResolves().known("Killbane"));
        CHECK(es.resolveZoneObject("Killbane") == nullptr);

        sr3zone::ObjectsSection section;
        section.records.push_back(killbane);
        es.loadZoneObjectsSection(section);

        // objectResolves() now resolves true for real - routed through the
        // real resolver, not a test manually calling .set().
        CHECK(es.objectResolves().known("Killbane"));
        CHECK(es.objectResolves().get("Killbane") == true);

        // resolveZoneObject() is case-insensitive (spec-confirmed - see
        // zone_object_resolver.h's own top comment) and returns the real
        // ingested record.
        const sr3zone::ObjectRecord* found = es.resolveZoneObject("killbane");
        CHECK(found != nullptr);
        if (found) CHECK(found->nameText.has_value() && *found->nameText == "Killbane");
        CHECK(es.resolveZoneObject("KILLBANE") != nullptr);
        CHECK(es.resolveZoneObject("Killbane_typo") == nullptr);
        CHECK(es.zoneObjectsLoadedCount() == 1);

        // CharacterState::scriptNpcHp is now routed for real -
        // scriptNpcBound + the override value, with NO test-only
        // applyScriptNpcHitPointsForTesting() call anywhere in this
        // fixture.
        CHECK(es.getOrCreateCharacter("Killbane").scriptNpcBound == true);
        CHECK(es.getOrCreateCharacter("Killbane").scriptNpcHp.known());
        CHECK(es.getOrCreateCharacter("Killbane").scriptNpcHp.get() == 60000);

        // Sec34.2's own CONFIRMED precedence (already implemented by
        // bc7ff16/drawOrGetMaxHitPoints): >= 1 wins verbatim over the
        // table - now reachable end-to-end from real zone data alone.
        CHECK(es.drawOrGetMaxHitPoints("Killbane") == 60000);

        // FIX 2026-10-08 (sibling fix found while re-measuring this task's
        // own probe): this record carries no script_npc_flags property at
        // all - Sec34.3's own CONFIRMED default (off) applies for real,
        // known false rather than left OPEN.
        CHECK(es.getOrCreateCharacter("Killbane").ignoreAI.known());
        CHECK(es.getOrCreateCharacter("Killbane").ignoreAI.get() == false);
    }

    // 1b. A named `script_npc` record whose script_npc_flags property
    // literally contains the `ignore_ai` token (Sec11.7's own real example
    // string) - ignoreAI becomes KNOWN true, not OPEN.
    {
        EngineState es;
        sr3zone::ObjectRecord rec = MakeNamedRecord("npc_ignore_ai_1", sr3save::nameHash("script_npc"));
        rec.tailEntries.push_back(MakeTextProperty(
            sr3zone::ScriptNpcFlagsPropertyId(),
            "never_pickup_weapons ignore_ai should_not_be_grabbed dont_drop_cash use_default_loadout"));
        sr3zone::ObjectsSection section;
        section.records.push_back(rec);
        es.loadZoneObjectsSection(section);

        CHECK(es.getOrCreateCharacter("npc_ignore_ai_1").scriptNpcBound == true);
        CHECK(es.getOrCreateCharacter("npc_ignore_ai_1").ignoreAI.known());
        CHECK(es.getOrCreateCharacter("npc_ignore_ai_1").ignoreAI.get() == true);
    }

    // 1c. A named `script_npc` record WITH a script_npc_flags property that
    // does NOT contain `ignore_ai` as a whole token - ignoreAI is KNOWN
    // false (the override requires the exact token, Sec34.3; a flags string
    // that merely shares a substring must not false-positive).
    {
        EngineState es;
        sr3zone::ObjectRecord rec = MakeNamedRecord("npc_no_ignore_ai", sr3save::nameHash("script_npc"));
        rec.tailEntries.push_back(
            MakeTextProperty(sr3zone::ScriptNpcFlagsPropertyId(), "should_not_be_grabbed dont_drop_cash"));
        sr3zone::ObjectsSection section;
        section.records.push_back(rec);
        es.loadZoneObjectsSection(section);

        CHECK(es.getOrCreateCharacter("npc_no_ignore_ai").scriptNpcBound == true);
        CHECK(es.getOrCreateCharacter("npc_no_ignore_ai").ignoreAI.known());
        CHECK(es.getOrCreateCharacter("npc_no_ignore_ai").ignoreAI.get() == false);
    }

    // 2. A named `script_npc` record with NO script_npc_hp property - the
    // common case (Sec11.7: 1,028 of 6,788 script_npc objects carry one).
    //
    // CORRECTED 2026-10-08 (root cause of probe_blockers_ranked_20261008_
    // post_resolver_merge.md item 1, 26/49 missions): this used to assert
    // that scriptNpcHp stays genuinely OPEN and drawOrGetMaxHitPoints()
    // throws - that was the actual bug, not correct behavior. Once the
    // record is resolved (as it is here), the ABSENCE of script_npc_hp is
    // itself a CONFIRMED fact (spec-lua-api-behaviour.md Sec34.2: the
    // script NPC constructor defaults this field to -1, "use the table" -
    // never set by commando_spawn-produced script NPCs either), not an
    // open question - so scriptNpcHp becomes KNOWN (-1), and
    // drawOrGetMaxHitPoints() falls through to the ordinary table draw
    // (the CHOSEN 100 fallback here, since installSpawnRankHitPoints() is
    // never called in this synthetic test) exactly like a non-scriptNpcBound
    // character, instead of refusing.
    {
        EngineState es;
        sr3zone::ObjectRecord grunt = MakeNamedRecord("npc_grunt_07", sr3save::nameHash("script_npc"));
        sr3zone::ObjectsSection section;
        section.records.push_back(grunt);
        es.loadZoneObjectsSection(section);

        CHECK(es.objectResolves().get("npc_grunt_07") == true);
        CHECK(es.getOrCreateCharacter("npc_grunt_07").scriptNpcBound == true);
        CHECK(es.getOrCreateCharacter("npc_grunt_07").scriptNpcHp.known());
        CHECK(es.getOrCreateCharacter("npc_grunt_07").scriptNpcHp.get() == -1);
        CHECK(es.drawOrGetMaxHitPoints("npc_grunt_07") == 100); // falls through to the table draw, no throw
        // Same sibling fix as test 1 above: no script_npc_flags property at
        // all is also a CONFIRMED "off" (Sec34.3), not OPEN.
        CHECK(es.getOrCreateCharacter("npc_grunt_07").ignoreAI.known());
        CHECK(es.getOrCreateCharacter("npc_grunt_07").ignoreAI.get() == false);
    }

    // 3. A named record of a DIFFERENT class (not script_npc) - resolves
    // true for objectResolves() (any named placed object resolves, per the
    // registry's own set-name step) but gets no scriptNpcBound/scriptNpcHp
    // treatment - that precedence rule is specific to script_npc.
    {
        EngineState es;
        sr3zone::ObjectRecord prop = MakeNamedRecord("some_prop_marker", sr3save::nameHash("object_tree"));
        sr3zone::ObjectsSection section;
        section.records.push_back(prop);
        es.loadZoneObjectsSection(section);

        CHECK(es.objectResolves().get("some_prop_marker") == true);
        CHECK(es.hasCharacter("some_prop_marker") == false); // never touched getOrCreateCharacter at all
        CHECK(es.resolveZoneObject("some_prop_marker") != nullptr);
    }

    // 4. An UNNAMED record (hasName == false) contributes nothing - the
    // registry's own set-name step is itself gated on "the record carries
    // a name" (spec-zone-data-format.md), reproduced here.
    {
        EngineState es;
        sr3zone::ObjectRecord anon;
        anon.hasName = false;
        anon.classHash = sr3save::nameHash("script_npc");
        sr3zone::ObjectsSection section;
        section.records.push_back(anon);
        es.loadZoneObjectsSection(section);

        CHECK(es.zoneObjectsLoadedCount() == 0);
    }

    // 5. Falling back: a name never seen in any loaded zone still behaves
    // EXACTLY as before this change - the existing test-settable map
    // (objectResolves().set()) still works, and an unset name still
    // throws. Real zone data is additive, never a regression.
    {
        EngineState es;
        sr3zone::ObjectRecord killbane = MakeNamedRecord("Killbane", sr3save::nameHash("script_npc"));
        sr3zone::ObjectsSection section;
        section.records.push_back(killbane);
        es.loadZoneObjectsSection(section);

        // A completely different, never-ingested name: still OPEN.
        CHECK(!es.objectResolves().known("some_other_npc"));
        bool threw = false;
        try {
            (void)es.objectResolves().get("some_other_npc");
        } catch (const sr3luahost::OpenStateError&) {
            threw = true;
        }
        CHECK(threw);

        // The existing test-settable map still works exactly as before -
        // no behavior removed, only added to.
        es.objectResolves().set("some_other_npc", true);
        CHECK(es.objectResolves().get("some_other_npc") == true);
        es.objectResolves().set("explicitly_false", false);
        CHECK(es.objectResolves().get("explicitly_false") == false);
    }

    // 6. Re-ingesting the same record twice is idempotent (two
    // loadZoneObjectsSection() calls over the same file, or two files that
    // both place the same name) - last write wins, no accumulation error.
    {
        EngineState es;
        sr3zone::ObjectRecord killbane = MakeNamedRecord("Killbane", sr3save::nameHash("script_npc"));
        killbane.tailEntries.push_back(MakeUintProperty(sr3zone::ScriptNpcHpPropertyId(), 60000));
        sr3zone::ObjectsSection section;
        section.records.push_back(killbane);
        es.loadZoneObjectsSection(section);
        es.loadZoneObjectsSection(section);

        CHECK(es.zoneObjectsLoadedCount() == 1);
        CHECK(es.getOrCreateCharacter("Killbane").scriptNpcHp.get() == 60000);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic_zone_object_resolver_test checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}

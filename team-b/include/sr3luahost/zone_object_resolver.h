// Real name -> sr3zone::ObjectRecord resolver (2026-10-08, orchestrator
// follow-up to commit bc7ff16/50bdb29): the piece this project's own doc
// comments repeatedly flagged as missing - engine_state.h's own top note
// and CharacterState::scriptNpcHp's doc comment both say plainly that
// `sr3zone::ScriptNpcHp()` (include/sr3zone/zone_objects.h) "can decode
// this value from a SPECIFIC ObjectRecord, but nothing in this project
// links a name to one." This header is that link.
//
// WHICH FIELD IS THE LUA-VISIBLE IDENTITY - confirmed, not assumed
// (spec-zone-data-format.md, the Objects-section Sec11 text, read in full
// before writing this): "The object's authored name is simply one such
// property... the record head's +0x18 'name offset' field... is the byte
// offset, within the tail, where this specific property's data (a
// NUL-terminated string) begins" - that property is `ctg_object_name`
// (id `0x355ef946`). The SAME section later traces the real registry's
// own per-object load step (vtable slot +0x2c, `0x00456d80`/`0x008544b0`,
// "read in full this pass"): "If the record carries a name (rec+0x1c
// non-zero)... calls `0x00457650` - confirmed to be the registry's
// set-name method, which inserts into the global, case-insensitive name
// map at registry+0x2660." `rec+0x1c` there is this same record's own
// decoded name (sr3zone::ObjectRecord::nameText, populated from
// `ctg_object_name` exactly when `hasName` is true) - i.e. the spec
// directly confirms, by tracing the real consuming code, that
// `ctg_object_name` IS the string a Lua-visible name lookup resolves
// against, not a guess from this header's own author. The same section
// gives the worked example this project already uses elsewhere
// (spec-lua-api-behaviour.md Sec34.2/Sec34.4, the real `Killbane` boss
// character): "a placed object's name - e.g. a mission-critical NPC like
// `Killbane` - is actually encoded on disk: it is the `ctg_object_name`
// property."
//
// CASE-INSENSITIVITY - also spec-confirmed, not this header's own
// embellishment: the passage above states the real map at registry+0x2660
// is explicitly "case-insensitive." find() below folds both the ingested
// name and the lookup key through a plain ASCII tolower, the SAME
// case-fold convention spec-zone-data-format.md's own Sec11.7 text already
// cites for this exact codebase (`0x00ea739d`, "a plain ASCII `tolower`").
// This is a DELIBERATE divergence from EngineState::objectResolves() (the
// existing, much older flat `OpenValueMap<bool>` simplification,
// Sec3.9/Sec10.6/Sec29) - that map stays exact-case, unchanged, matching
// its own established project-wide convention (every other name inserted
// into it today, e.g. the five applySpecInitialState() literals, is
// exact-case); it is not retrofitted to fold case here because it is a
// generic template shared by many unrelated per-name maps
// (tutorialState_, missionComplete_, etc. - engine_state.h's own
// declarations), and changing its lookup semantics project-wide is out of
// this task's own scope. A caller that cares about the spec-confirmed
// case-insensitive behavior should go through THIS resolver's find()
// instead.
//
// SCOPE: this class only INGESTS records already parsed by
// sr3zone::ZoneObjects (locate()/parseAt()) - it does not itself open any
// file or archive. Wiring "which real `.czn_pc` files get parsed and fed
// in" is the caller's job (EngineState::loadZoneObjectsSection(), and
// above that, tools/lua_host_run.cpp's own real-cache install step) - kept
// separate so this class stays a plain, synchronously-testable in-memory
// map, exactly like every other EngineState-adjacent per-domain header
// (zone_state.h, vint_state.h, etc.).
#pragma once

#include <cctype>
#include <string>
#include <unordered_map>

#include "sr3zone/zone_objects.h"

namespace sr3luahost {

// ASCII-only lower-fold, matching spec-zone-data-format.md Sec11.7's own
// cited case-fold mechanism (`0x00ea739d`, "a plain ASCII `tolower`") -
// not a locale-aware fold (the real engine's own per-character CRC
// case-fold is not locale-aware either, per that same citation).
inline std::string AsciiLowerFold(const std::string& s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

class ZoneObjectResolver {
public:
    // Ingests every NAMED record (hasName == true, i.e. the record's own
    // +0x18 field pointed at a decodable `ctg_object_name` string - see
    // this header's own top comment) from `section`. A record with no name
    // contributes nothing - this mirrors the real registry's own
    // set-name step, which is itself gated on "the record carries a name"
    // (same citation). Later records with the same (case-folded) name
    // OVERWRITE an earlier one - CHOSEN, not spec-stated either way (no
    // section discusses what happens when two distinct records share a
    // name within or across ingested zones); kept simple rather than
    // guessing at a collision rule the spec never gives, and consistent
    // with "last write wins" being this project's own default for every
    // other flat name map already in this codebase (OpenValueMap::set()).
    void ingest(const sr3zone::ObjectsSection& section);

    // Case-insensitive lookup (see this header's own top comment for why).
    // Returns nullptr if no ingested zone ever carried a record with this
    // name.
    const sr3zone::ObjectRecord* find(const std::string& name) const;

    size_t size() const { return byName_.size(); }
    void clear() { byName_.clear(); }

private:
    std::unordered_map<std::string, sr3zone::ObjectRecord> byName_; // key already ASCII-lower-folded
};

} // namespace sr3luahost

#pragma once

// sr3locstr - Saints Row: The Third's `.le_strings` localisation string
// tables and key-hash lookup (spec-localization-format.md).
//
// Loads the shipped `<table>_<LANG>.le_strings` files (Sec2: header,
// power-of-two hash buckets, u32 key hash + 0-terminated u16 text) and
// provides the key-hash -> text lookup (Sec6.1: first match across tables
// searched in load order, hash 0 = no key, no cache). Stored here as a
// plain owned map per table rather than the real loader's zero-copy,
// pointer-into-buffer scheme (Sec2.2/Sec5.1 step 4) - the OBSERVABLE
// lookup behaviour is what Sec6.1 specifies, not the storage shape, and
// nothing in this host needs the real loader's in-place buffer patching.
//
// This host only ever loads the US tables - the only language this
// project models anywhere (fonts, mission data, ...). Nothing here reads
// or writes the language-selection global of Sec5.2, and the four
// big-endian `voice_script_*` anomaly files of Sec2.5 are out of scope:
// `voice_script` is never loaded on PC by any language (Sec2.5/Sec5.3), so
// it is never one of the tables a caller passes in here.
//
// SCOPE: Sec2 (on-disk layout), Sec4 (key hash) and Sec6.1 (the lookup).
// NOT implemented: Sec3 (font-code/charlist decoding - callers already
// treat text as font-code units, matching this project's existing
// `text_tag` byte-widening and Sec3.1's "the stored code units are font
// codes"), Sec5.2-5.4 (language selection, DLC load/unload - this host
// never switches language or mounts DLC), Sec6.2's missing-key wrapper
// texts (the `!!...` forms - callers fall back to the raw `text_tag`
// string instead, a CHOSEN difference, since that raw-string fallback is
// already what this host drew before this unit existed).

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace sr3locstr {

class LeStringsFormatError : public std::runtime_error {
public:
    explicit LeStringsFormatError(const std::string& what) : std::runtime_error(what) {}
};

// One parsed `.le_strings` file (Sec2). `parseLeStrings` throws
// LeStringsFormatError on a bad magic or version (Sec5.1 step 3: the real
// loader checks only these two before taking a slot) or on a bucket/record
// offset that runs past end of file. CHOSEN: this reader DOES bounds-check
// every array/record read for memory safety, rejecting a file whose
// buckets escape it - the real loader "does no bounds check on any count
// or offset" (Sec2.3), but no shipped file ever needs that leniency (Sec2.1
// -2.3's own 161/165 well-formed-file figures).
struct LeStringsTable {
    uint16_t bucketCount = 0;
    uint32_t recordCount = 0; // Sec2.1: not used for lookup, informational only
    std::unordered_map<uint32_t, std::u16string> byHash; // key hash -> text (Sec2.3)
};

LeStringsTable parseLeStrings(const std::vector<uint8_t>& bytes);

// The engine's key hash (Sec4): lower-cased, seed-0, no-final-XOR CRC-32.
// Reuses sr3save::nameHash() directly - Sec4 states this is "the same
// function [that] hashes Vint property names", and this project's own
// include/sr3vintdoc/vint_doc.h already documents Vint property-name
// hashing as sr3save::nameHash (Sec5 there), so this is not a new
// derivation, just the known-identical function under a local name.
uint32_t keyHash(const std::string& key);

// Tables searched in load order (Sec6.1: "first match, in the first slot
// that has it, wins" - slot order is load order, since each table takes
// the first free slot, Sec5.1 step 3).
class LeStringsLookup {
public:
    // Adds a table to the end of the search order. CHOSEN: call in the
    // same order Sec5.2 step 4 loads them (static, hud, activity,
    // diversion, mission, menu, multiplayer, customize, subtitle,
    // platform_PC, patch0) for a faithful search order; this class itself
    // does not enforce or know that order.
    void addTable(LeStringsTable table);

    size_t tableCount() const;

    // Hash 0 returns nullopt at once, without searching (Sec6.1: "Hash 0
    // returns null at once" - the empty string, and every string that is
    // not a key in any loaded table, hashes/resolves to 0, Sec4).
    std::optional<std::u16string> lookup(uint32_t hash) const;

    // Convenience: keyHash(key) then lookup() - an empty key hashes to 0
    // and returns nullopt directly without searching, matching Sec6.2's
    // wrapper 0x00849df0 ("null string -> null").
    std::optional<std::u16string> lookupByKey(const std::string& key) const;

private:
    std::vector<LeStringsTable> tables_;
};

} // namespace sr3locstr

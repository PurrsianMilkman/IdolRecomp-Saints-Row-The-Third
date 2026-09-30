#pragma once

#include <cstdint>
#include <string_view>

namespace vpp {

// Case-insensitive rotate-XOR filename hash used by the engine for runtime
// filename lookups. This is a clean-room re-derivation, in our own words,
// of the algorithm described in spec-vpp-container.md Sec2.2:
//
//   hash = 0
//   for each character c in the filename (until the null terminator):
//       if c is an uppercase ASCII letter ('A'-'Z'):
//           c = lowercase(c)
//       hash = rotate_left_32(hash, 6)
//       hash = hash XOR c
//   return hash
//
// The loop above is CONFIRMED by disassembly: spec-vpp-container.md
// Sec2.2 records that FUN_00da7890 - the routine the .cfmesh_pc
// constructor calls to key its mesh registry - is exactly this loop,
// including the explicit 'A'-'Z' lowercase fold before the rotate/XOR.
// Whether an on-disk directory entry ALSO stores a precomputed copy of
// this hash is explicitly UNCONFIRMED (spec Sec2.2, Sec6 item 5) - this
// function is therefore only used in this library for descriptive/lookup
// purposes (e.g. Entry::nameHash), never to validate or interpret on-disk
// directory-entry bytes.
//
// NOT container-specific, despite living in namespace vpp: it is confirmed
// reused across unrelated formats - archive filenames, .rig_pc bone names
// (22,274/22,274 real entries, spec-rig-format.md Sec5), the .cfmesh_pc
// mesh-registry key, and .ctdg_pc's in-file speaker ids. It lives here
// because the container format is simply where this project first needed
// it, not because its use is limited to containers.
//
// BUT IT IS NOT THE ENGINE'S ONLY STRING HASH, and an earlier version of
// this comment overclaimed by implying it was. spec-vpp-container.md
// Sec2.2 and spec-rig-format.md Sec5 both carry an explicit scope
// correction (2026-09-10): the engine has at least TWO string hashes -
// this rotate-6/XOR one, and a table-driven CRC-32 over the lowercased
// string (FUN_00d9e8b0) that keys the mission-conversation registry.
// .ctdg_pc uses BOTH AT ONCE: CRC-32 for its registry key, rotate-6/XOR
// for the speaker ids inside the file (spec-conversation-format.md Sec6).
// So do NOT assume an unidentified 32-bit name hash in an unfamiliar
// structure is this one - check before reaching for it.
//
// Cheap way to check, worth knowing because it beats brute force: this
// hash can be identified ALGEBRAICALLY from very few samples. Under
// h = rotl32(h, 6) XOR c, two strings differing only in their FINAL
// character produce hashes differing by exactly that character's XOR
// value. That identity is a property CRC-32 provably lacks (its whole
// word scrambles), so a single matching pair distinguishes the two
// without any search - which is how spec-conversation-format.md Sec6
// settled the .ctdg_pc speaker ids after a 3,917-candidate brute-force
// search had returned nothing.
uint32_t hashFilename(std::string_view name);

} // namespace vpp

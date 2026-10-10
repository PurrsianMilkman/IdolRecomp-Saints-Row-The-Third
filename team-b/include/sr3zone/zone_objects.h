// Reader for the `.czn_pc` Objects (0x2234) placement/property stream -
// spec-zone-data-format.md Sec11 (units U0+U1, 2026-10-07, the first
// `.czn_pc`-interior unit landed after the owner lifted the standing
// zone-interior hold). THIS IS THE FIRST READER FOR ANY PART OF `.czn_pc`'s
// own object-placement body: zone_header.h/zone_geometry.h (this project's
// pre-existing `.czh_pc`/`.gzn_pc` readers) explicitly, deliberately, do not
// attempt it - see their own file-level comments for why (Sec11's own
// status header: the hold predates this file).
//
// SCOPE - what this reader does and does not claim:
//
//  * It parses the Objects section's FBOW/version-5 payload header, the
//    32-byte fixed region in front of the directory array, the directory
//    array itself (the zone-unload handle list), the 28-byte fixed
//    per-object record head, and - best-effort, explicitly flagged below -
//    the variable property-list tail.
//  * It does NOT walk `0x02cc9900` (the runtime-populated engine-wide class
//    descriptor table Sec11.5 traces the resolution chain to) - that table
//    reads all-zero in the shipped executable's own bytes and has no
//    statically-enumerable "register all classes" call site (Sec11.9 item
//    4), so it is out of reach for any static reader. What CHANGED
//    2026-10-07 (Sec11.9 item 4, "SUBSTANTIALLY RESOLVED" by unit U2's own
//    dictionary attack + registration-stub reads): 40 of 42 real
//    class-name HASHES are now independently matched to literal name
//    strings - a different, static-data-only route that does not go
//    through `0x02cc9900` at all. This reader cites 21 of those 40 by
//    literal name (ClassNameForHash() below - see its own comment for
//    exactly which 21 and why not all 40); the bucket-index-only behavior
//    (ClassHashBucket(), Sec11.5's `0x00dab2b0`) is unchanged and still the
//    only thing derivable from the raw hash ALONE, without Sec11.7's own
//    name-string correlation.
//
// LOCATING THE OBJECTS PAYLOAD - a provisional strategy of this reader's
// own, NOT a replay of the real loader's own route. The real loader finds
// the Objects section by walking `.czn_pc`'s top-level `{tag,length}` chain
// (Sec11.1, `0x00863430`) until it reaches the record whose tag (high bit
// stripped) equals 0x2234.
//
// REVISITED 2026-10-07, after Team A's own whole-population re-replay of
// Sec11 (unit U2): the ORIGINAL blocker for implementing this walker for
// real - "the walker's own record-header WIDTH is only partly resolved: 8
// bytes in the common case, widening to 12 when some unidentified bit of
// the tag word's top 3 bits is set" - is now RESOLVED, and resolved in the
// simplest possible direction. Sec11.1 (CORRECTED 2026-10-07): the mask
// touches exactly ONE bit (the tag's own bit 31, not "the top 3 bits"),
// `header_size = 8 | ((tag>>29)&4)`, and because every real top-level
// section id in this engine ships as `0x8000xxxx` (bit 31 always set), the
// 8-byte branch is NEVER exercised by any real on-disk record: a
// population-wide walk of all 19,725 top-level records across all 2,971
// shipped `.czn_pc` files found `header_size == 12` in 19,725/19,725
// (100%), zero exceptions. The on-disk top-level record is uniformly
// `{u32 tag, u32 length, u32 extra}`, 12 bytes, and `extra` is confirmed `0`
// for every real `0x2234` occurrence (861/861).
//
// DECISION: still keep the signature-scan strategy below, NOT a real
// top-level-walker reimplementation - but for a DIFFERENT, narrower reason
// than the one that no longer applies. Sec11.1's own paragraph gives the
// walker's (`0x00863430`'s) masking CONSTANTS - `AND 0x80000003`, a
// branching `(x-1 | 0xfffffffc)+1` round-up, `AND 0x7fffffff` - but not
// which field each operates on or their exact operand order; and
// Sec9.5 carries its own explicit, STILL-OPEN "CONFLICT - step alignment"
// note (desk review 2026-09-30, not revisited by the 2026-10-07 pass) that
// this exact 4-byte round-up contradicts this same spec's own Sec1/Sec8.1
// `align8(8 + length)` for the *sibling* tag-dispatch functions
// (`0x00864c60`/`0x007512f0`/`0x008652d0`) in the same walker family,
// unresolved either way. Reconstructing a byte-stepping walker from masking
// constants alone, without the exact operand/order semantics, would mean
// guessing at exactly the kind of 32-bit arithmetic this project's own
// lessons warn is error-prone by hand (vpp/hash.h's file comment makes the
// identical point about a different hash) - and guessing wrong here is
// WORSE than not walking at all: a mis-stepped walker would silently land
// on the wrong buffer offset for any file whose earlier top-level records
// have an unusual length, with no structural signal distinguishing that
// from a correct walk. The signature-scan strategy below has no equivalent
// failure mode - it can only ever reject a candidate (every structural
// bound in parseAt() fails to hold) or accept one that independently
// satisfies every CONFIRMED structural invariant Sec11.3/Sec11.4 give,
// never silently misalign. So: the header-WIDTH ambiguity that originally
// motivated signature-scan is gone, but a narrower, still-genuinely-open
// stepping-arithmetic ambiguity in the same walker family is the real
// reason this reader still does not attempt it - this is a documentation
// correction, not a behavior change (ZoneObjects::locate() below is
// byte-for-byte what it was before this revisit).
//
// So, exactly as zone_geometry.h's ZoneGeometry::locate() already does for
// the embedded Mesh sub-block (its own file-level comment: "locating a Mesh
// sub-block by walking .czn_pc's own top-level chain... is explicitly
// HYPOTHESIS-unconfirmed... locate() instead scans .czn_pc directly for the
// version/check-value SIGNATURE, independent of any record-length
// arithmetic"), ZoneObjects::locate() below scans directly for the
// Objects-payload's OWN signature - `u32 == 'FBOW'` immediately followed by
// `u32 == 5` (Sec11.3), 4-aligned - rather than walking the top-level chain
// at all. This cannot silently misparse: either the signature plus every
// CONFIRMED structural bound below holds at a given offset, or parseAt()
// throws and that offset is skipped. A real Objects section can in
// principle be missed by this strategy (if some real zone's FBOW anchor is
// not 4-aligned relative to the buffer start), but it cannot be silently
// misread: every field this reader reports for an accepted candidate is
// read at the fixed offsets Sec11.3/Sec11.4 give, counted against the two
// independent record/directory counts the payload header itself carries
// (Sec11.3's own two separate count fields), never against an assumed
// outer length this reader does not have.
//
// MEASURED, 2026-10-07 (tools/zone_objects_population.cpp, the complete
// shipped population - all 10 archives spec-zone-data-format.md Sec9.4
// uses, exactly 2,971 `.czn_pc` files found, matching Sec1's own census):
// 861 files yield a located, structurally complete section (383,346
// objects total); 2,110 yield none. This reader cannot tell "genuinely no
// Objects section" apart from "present but missed by this locate-by-
// signature strategy" - both report zero sections - so 861/2,971 is a
// lower bound on real coverage, not a measured miss rate; that zero
// sections anywhere in the run came back structurally INCOMPLETE (a
// declared recordCount the buffer could not satisfy) is at least some
// evidence against systematic misalignment among the 861 it did find.
// Sec11.9 item 1 (which per-object loader a real zone exercises) is now
// RESOLVED (2026-10-07, unit U2): only the batch loader `0x00457c60` -
// `0x00457a50` is real, reachable code but never fired for the Objects
// section by any of the 861/861 real sections this project has located.
// This reader's own record-head/tail decode is unaffected either way (both
// loaders consume the identical on-disk record format, Sec11.6's own
// "Team B's parser should be written to expect either" - still true, this
// reader does not need to know which loader a given file would exercise).
// The top-level walker's own header-width question (the other half of what
// used to be item 2) is also now resolved - see the file-header comment
// above - but the narrower stepping-arithmetic gap described there is why
// this reader still does not replay that walker.
//
// WHAT CONFIRMED MEANS HERE: every fixed-offset field below (magic,
// version, the two counts, the 28-byte record head's seven fields, the
// `+0x1c = 0x1c + tailLength` record-size formula, the align-4 step after
// each record) is read directly off spec-zone-data-format.md Sec11.3/
// Sec11.4's own disassembly-confirmed account of `0x0045bc20`/`0x00456760`/
// `0x00da7f30`. The variable tail's INTERIOR (how many property entries it
// holds, where the entry list ends) is a different matter - see
// ObjectRecord's own comment below before trusting tailEntries().

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3save/save_crc.h" // sr3save::nameHash() - header-only, no extra link dependency
                               // (save_crc.h is all-inline; see its own file comment). Reused,
                               // not reimplemented, per Sec11.7's own finding that the
                               // property/class-name hash IS this project's already-known
                               // save-format/activity/cheat-name CRC-32 (init 0, no final XOR,
                               // lowercased ASCII, reflected poly 0xEDB88320) - confirmed
                               // 256/256 against the standard reflected table.
#include "sr3zone/errors.h"
#include "vpp/byte_view.h"

namespace sr3zone {

using vpp::ByteView;

// 0x2234 - the Objects section's own zone-content-kind id (spec-zone-data-
// format.md Sec1's registration-table entry, independently re-read at
// 0x013026d0 in Sec11.1). NOT used by locate()'s own signature scan (which
// finds the FBOW payload directly, see the file header) - kept here only as
// the format's own name for callers that already have a payload offset
// from elsewhere (e.g. a future top-level-walker implementation).
constexpr uint32_t kObjectsSectionTag = 0x2234u;

// 'F','B','O','W' in ascending memory order == the little-endian u32
// 0x574f4246 (spec-zone-data-format.md Sec11.3). CONFIRMED - disassembly
// (`0x0045bc20`'s own equality test against this exact value).
constexpr uint32_t kObjectsPayloadMagic = 0x574f4246u;

// The Objects-section payload format version. CONFIRMED - disassembly,
// Sec11.3: two bound checks collapsing to an exact equality test against 5.
constexpr uint32_t kObjectsPayloadVersion = 5u;

// The 32-byte fixed region at the front of the payload: magic (4) +
// version (4) + 24 further bytes before the directory array begins. Of
// those 24, only two are read by any function Sec11 disassembled: the
// directory-entry count at +0x0c (read by `0x0045bc20` itself) and the
// per-object record count at +0x08 (read separately, later, by
// `0x00457c60`/`0x00457a50` - Sec11.3's own note that these are "two
// genuinely different count fields... not a transcription error in either
// direction"). The remaining bytes (+0x10-+0x1f, 16 of the 24) are OPEN -
// Sec11.9 item 3. CONFIRMED - disassembly for the region's own size (the
// `align4(dataOffset + 0x20)` arithmetic, Sec11.3).
constexpr size_t kObjectsFixedHeaderSize = 0x20;

// One directory/handle-list entry: {u32 handleLo, u32 handleHi}, 8 bytes.
// CONFIRMED - disassembly, Sec11.3 (the array's own element shape) and
// Sec11.3's separate paragraph confirming its role directly at
// `0x00866950`: this is the zone-unload handle list, not dead weight.
constexpr size_t kObjectDirectoryEntrySize = 8;

// The fixed per-object record head: 28 bytes (0x1c). CONFIRMED -
// disassembly, Sec11.4 (`0x00456760` reads/writes nothing past on-disk
// offset 0x19; the record's own fixed head ends at 0x1c with its last 2
// bytes - 0x1a/0x1b - unread by the generic head-copier).
constexpr size_t kObjectRecordHeadSize = 0x1c;

// 0x1fe = 510. The modulus `0x00dab2b0` is called with when resolving a
// record's class-name hash (Sec11.5). CONFIRMED - disassembly.
constexpr uint32_t kClassHashBucketCount = 510u;

// Sec11.5's mixing function, `0x00dab2b0`: a textbook Thomas-Wang-style
// 32-bit integer avalanche mix (shift-15/not/add, shift-10/xor,
// multiply-by-9 - the disassembly's own `lea ecx,[ecx+ecx*8]` - shift-6/
// xor, shift-11/not/add, shift-16/xor), then the remainder of dividing by
// `modulus`. CONFIRMED - disassembly, the function's full 24-instruction
// body (Sec11.5): "it takes exactly one 32-bit value and one modulus...
// there is no byte-by-byte character loop anywhere in its body."
//
// This is the ONLY step of class-name resolution recoverable by a static
// reader - see the file header comment and ObjectRecord::classHashBucket
// for why the remaining steps (bucket -> class ordinal -> 0x02cc9900 entry)
// are out of reach. `modulus` defaults to the one value the real code ever
// calls this with (510); exposed as a parameter only so a caller can
// reproduce the raw mix independently if a future pass finds a different
// call site.
uint32_t ClassHashBucket(uint32_t classHash, uint32_t modulus = kClassHashBucketCount);

// CORRECTION 2026-10-07 (Sec11.9 item 4, SUBSTANTIALLY RESOLVED by unit U2):
// 40 of 42 distinct real class-name hashes are now named by exact CRC-32
// match - Sec11.7: "the property-id and class-id namespaces are both this
// same CRC-32... confirmed by direct disassembly of the registration code
// itself." This gives a SECOND, independent way to identify a class beyond
// ClassHashBucket()'s own bucket ordinal above - and a NECESSARY one, since
// a bucket index is lossy (many hashes can share a bucket; Sec11.5's own
// 510-modulus mix) while the raw classHash is not: each name below is
// hashed with the exact same sr3save::nameHash() this project already uses
// for save-format/activity/cheat names (never hand-transcribed as a literal
// hex constant, to avoid exactly the kind of 32-bit-arithmetic transcription
// error vpp/hash.h's own file comment warns against), so ClassNameForHash()
// below is a direct, reversible classHash -> name lookup, not a guess.
//
// IMPORTANT - this table intentionally covers only 21 of the "40 of 42"
// classes, not all 40: Sec11.7's own "Resolved classes include" paragraph
// spells out 21 class names *verbatim* (with population counts for most)
// and then says "and 20 more (1-1,927 objects each); 2 of 42 class hashes
// remain unresolved" WITHOUT naming the other 20 - i.e. Sec11.7's own prose
// only gives this reader 21 literal names to cite, even though Team A's own
// underlying data evidently resolved 40. (Flagged for the orchestrator:
// 21 explicitly named + "20 more" + "2 unresolved" sums to 43, not 42 - an
// off-by-one in Sec11.7's own prose that this reader does not attempt to
// resolve by guessing which figure is off.) Only the 21 names actually
// spelled out below are implemented; the other ~19-20 resolved-but-
// unlisted classes and the 2 genuinely unresolved hashes are NOT guessed
// at - ClassNameForHash() returns std::nullopt for any of them, exactly as
// it would for a hash this reader has never seen, and a caller cannot tell
// the three cases apart (deliberately - this reader reports facts it can
// cite, not inferences about which case it is).
struct NamedClassHash {
    const char* name;        // exact spelling Sec11.7 gives (case preserved for display;
                              // hashing itself is case-insensitive, Sec11.5/Sec11.7)
    long long populationCount; // Sec11.7's own figure for this class, -1 if Sec11.7 names
                                // the class but gives no count for it (the 10 names after
                                // `Trigger_Volume` in its own list)
};

// The 21 of 40 real class-name hashes Sec11.7 names explicitly by literal
// string - see the NamedClassHash comment above for exactly what this does
// and does not cover. Hashes are computed via sr3save::nameHash(), not
// hand-transcribed.
const std::vector<NamedClassHash>& KnownClassHashes();

// Looks up `classHash` against KnownClassHashes() and returns the matching
// name, or std::nullopt if classHash does not match any of the 21 this
// reader can cite (see KnownClassHashes()'s own comment for why "not found"
// does not mean "genuinely one of the 2 unresolved hashes" - it may simply
// be one of the ~19-20 resolved-but-not-individually-named-in-Sec11.7
// classes this reader was never given a literal string for).
std::optional<std::string> ClassNameForHash(uint32_t classHash);

// One directory-array entry (Sec11.3; confirmed at `0x00866950` to be the
// zone-unload handle list, one {handle} pair per placed object this
// section's load registered).
struct ObjectDirectoryEntry {
    uint32_t handleLo = 0;
    uint32_t handleHi = 0;
};

// One decoded entry of the variable property-list tail, per the registry's
// OWN generic per-object property-list format (Sec11.7, confirmed directly
// from its writer, `0x00456660`):
//
//   +0x00  u16  kind
//   +0x02  u16  size   (byte length of `data`)
//   +0x04  u32  id     (which named property this is - CORRECTED 2026-10-07:
//                       `ctg_object_name`'s own id is 0x355ef946
//                       (kCtgObjectNamePropertyId below), NOT the literal 0
//                       an earlier draft assumed - see Sec11.7's own
//                       "CORRECTED 2026-10-07" paragraph and
//                       FindTailEntryById()/kCtgObjectNamePropertyId below)
//   +0x08  ...  `size` bytes of property data
//
// The 8-byte header's field layout is CONFIRMED - disassembly (Sec11.7:
// `mov word ptr [eax],cx` / `[eax+2],bp` / `[eax+4],edx`, cross-checked
// against the matching read-back comparisons). What Sec11 does NOT state
// is how consecutive entries PACK (the stride from one entry to the next)
// or how a reader independently finds where the entry LIST ends within one
// record's tail - both resolved empirically by this reader's own
// whole-population real-data run, not by Sec11.7 itself - see
// ObjectRecord's own "tail-interior decode" comment below for the exact
// measurement (156,101 / 156,101 real records, 100%, under a 4-byte
// entry stride).
struct ObjectPropertyEntry {
    uint16_t kind = 0;
    uint16_t size = 0;
    uint32_t id = 0;
    std::vector<uint8_t> data; // exactly `size` bytes, copied from the tail
};

// CORRECTED 2026-10-07 (Sec11.7, units U2 and the character-spawn-props
// pass, independently, same value): the `ctg_object_name` property's own
// on-disk id is 0x355ef946 (895416646 decimal - Sec11.7 states both forms,
// found verbatim at the predicted tail offset in 19,800/19,800 real named
// objects), NOT the literal integer 0 an earlier draft of this reader
// assumed. This is a literal spec-given constant (not computed at runtime)
// because Sec11.7 states the exact hex value directly - but it is ALSO
// exactly `sr3save::nameHash("ctg_object_name")` under this project's own
// already-existing, independently-confirmed CRC-32 (Sec11.7: "the
// property-id and class-id namespaces are both this same CRC-32"), and a
// regression test (tests/synthetic_zone_objects_test.cpp) asserts the two
// agree, rather than trusting the literal unverified.
constexpr uint32_t kCtgObjectNamePropertyId = 0x355ef946u;

// Finds the first entry of `tailEntries` whose `id` matches `propertyId`,
// or nullptr if none does - lets a caller identify/extract a specific named
// property (e.g. `ctg_object_name` via kCtgObjectNamePropertyId above, or
// one of the script_npc_*/script_vehicle_* spawn-override properties below)
// BY ITS ID, rather than by position or guesswork (the gap Sec11.9 item 3's
// own task-brief follow-up names explicitly). Never throws. Returns nullptr
// (not an error) when `rec.tailEntries` is empty, does not contain a
// matching id, or stopped early at a `tailOverrun` before reaching one -
// Sec11.7's own best-effort tail-interior decode is unaffected by this
// lookup, see ObjectRecord's own "tail-interior decode" comment.
const ObjectPropertyEntry* FindTailEntryById(const struct ObjectRecord& rec, uint32_t propertyId);

// Decodes `entry.data` as a plain little-endian UNSIGNED integer, using
// exactly `entry.data.size()` bytes (1, 2, 4, or 8 only). Returns
// std::nullopt for any other width - NEVER guessed at, because Sec11.7
// gives the NAME, the id-derivation mechanism, and population counts/
// example values for the script_npc_*/script_vehicle_* family below, but
// does NOT state their on-disk `kind`/wire-width explicitly (unlike the
// position/transform property, where Sec11.7 does give `kind=2`/`size=12`
// and `kind=3`/`size=28` directly) - so this decoder makes no assumption
// about kind, only about the one thing every integer-valued property must
// satisfy regardless of kind: a byte width that is a plain power-of-two
// machine integer size.
std::optional<uint64_t> DecodePropertyAsUnsignedInt(const ObjectPropertyEntry& entry);

// Decodes `entry.data` as text: every byte verbatim, minus exactly one
// trailing NUL if present (Sec11.7's own `script_npc_flags` example,
// `"never_pickup_weapons ignore_ai should_not_be_grabbed dont_drop_cash
// use_default_loadout"`, is plain space-separated ASCII - a trailing NUL is
// tolerated, matching ctg_object_name's own NUL-terminated string
// convention, but not required).
std::string DecodePropertyAsText(const ObjectPropertyEntry& entry);

// NEW 2026-10-07 (Sec11.7, "CLOSED with real-data proof"): the character/
// vehicle spawn-override property family. Each id below is COMPUTED via
// sr3save::nameHash() on the exact literal name Sec11.7 gives (never
// hand-transcribed as a hex literal - unlike kCtgObjectNamePropertyId
// above, Sec11.7 does not state these ids' own numeric value directly, only
// the name strings, so computing via the project's own existing,
// independently-confirmed hash helper is the only clean-room-safe way to
// get the right number - see this header's own include comment).
//
// Population figures Sec11.7 gives (2,971-file population): script_npc_hp
// authored 1,028 times (values 1 to 100,000); script_vehicle_hp 354 times
// (up to 10,000,000); script_npc_flags 6,702 times, 425 of which literally
// contain the substring "ignore_ai"; script_npc_team 4,104 times, decoding
// to real gang-name strings. Sec11.7 names the REST of the
// `script_vehicle_*` family (`_flags`/`_mp_team`/`_peer`/`_resource_uid`/
// `_type_name`/`_variant_name`) by literal string but gives a population
// count only for `_hp` - `script_vehicle_flags`/`script_vehicle_mp_team`/
// `script_vehicle_type_name`/`script_vehicle_variant_name` below are real,
// spec-cited names, just without an independent count citation of their
// own.
//
// NEW 2026-10-10 (vehicle-class xtbl wiring task): `script_vehicle_type_name`
// is the real zone-spawn-to-vehicle-type connection this project's own
// EngineState (src/lua_engine_state.cpp, loadZoneObjectsSection) was
// missing - a placed `Script_Vehicle` object's own property naming which
// real `<name>_veh.xtbl` row (sr3vehicleinfo::VehicleEntry::name) it is an
// instance of, exactly the join key a caller needs to look up that type's
// REAL class (sr3vehicleinfo::VehicleTypeUnion::vehicleClass) and populate
// VehicleState::flyingType for a zone-placed vehicle instance instead of
// leaving it OPEN. `script_vehicle_variant_name` is spec-cited alongside it
// (a vehicle may ship authored colour/customisation variants per
// spec-vehicle-data.md) but nothing in this project's current scope
// consumes it - exposed here on the same footing as every other named
// property this header already decodes, not because a caller needs it yet.
//
// NOTE: the task brief that requested this family paraphrased the vehicle
// member as "script_vehicle_team" - Sec11.7's own literal text gives
// `script_vehicle_mp_team`. This reader follows Sec11.7's exact spelling
// (the authoritative spec text), not the paraphrase.
//
// This project's own EngineState (src/lua_engine_state.cpp,
// markScriptNpcBoundForTesting, Sec34.4) currently "forgets" the max-HP/
// ignore-AI override specifically because this zone data was previously
// unreadable - these accessors exist so a FUTURE follow-up task can wire
// them in; this task does not itself change lua_engine_state.cpp's logic.
uint32_t ScriptNpcHpPropertyId();
uint32_t ScriptNpcFlagsPropertyId();
uint32_t ScriptNpcTeamPropertyId();
uint32_t ScriptVehicleHpPropertyId();
uint32_t ScriptVehicleFlagsPropertyId();
uint32_t ScriptVehicleMpTeamPropertyId();
uint32_t ScriptVehicleTypeNamePropertyId();
uint32_t ScriptVehicleVariantNamePropertyId();

// Named, typed convenience accessors built on FindTailEntryById() +
// DecodePropertyAsUnsignedInt()/DecodePropertyAsText() above. Each returns
// std::nullopt when `rec` carries no property with the matching id (the
// documented "absence" case - most real objects do NOT carry any one of
// these, Sec11.7's own population figures above are a small minority of the
// full 383,346-object population) or when a numeric property's data width
// is not decodable (1/2/4/8 bytes) - never a guess.
std::optional<uint64_t> ScriptNpcHp(const struct ObjectRecord& rec);
std::optional<std::string> ScriptNpcFlags(const struct ObjectRecord& rec);
std::optional<std::string> ScriptNpcTeam(const struct ObjectRecord& rec);
std::optional<uint64_t> ScriptVehicleHp(const struct ObjectRecord& rec);
std::optional<std::string> ScriptVehicleFlags(const struct ObjectRecord& rec);
std::optional<std::string> ScriptVehicleMpTeam(const struct ObjectRecord& rec);
std::optional<std::string> ScriptVehicleTypeName(const struct ObjectRecord& rec);
std::optional<std::string> ScriptVehicleVariantName(const struct ObjectRecord& rec);

// NEW 2026-10-08 (spec-ai-runtime.md Sec15.1/Sec15.3, the Navpoint/
// `cover_node` movement-target-selection unit): the generic position/
// transform property every placed-object class may carry, plus
// `Navpoint`'s own `navpoint_flags` bitmask. Both live HERE, not in the
// AI-runtime unit that actually consumes them (sr3airuntime/
// navpoint_selector.h) - they are ordinary sr3zone Objects-section
// properties, decoded exactly like the script_npc_*/script_vehicle_*
// family above (same FindTailEntryById()/DecodePropertyAs*() mechanism),
// not anything AI-specific about the READ itself. The AI-specific part -
// self-registration into flat arrays and the annulus-filter/random-pick
// destination-selection algorithm these feed - lives in sr3airuntime,
// which consumes ObjectRecord through this header rather than re-parsing
// anything.

// spec-zone-data-format.md Sec11.7's own "Empirical confirmation" paragraph
// (closing Sec11.9 item 8) states: "`id = 3367956165` (`0xC8BEEEC5`) is the
// position/transform property." Like kCtgObjectNamePropertyId above, this
// is meant to be a PINNED LITERAL constant, NOT computed via
// sr3save::nameHash() - Sec11.7 states this id's own exact numeric value
// directly rather than a name string to hash.
//
// RESOLVED 2026-10-08 (Team A spec-sync, commit 47c8516): an earlier draft
// of Sec11.7 stated a hex parenthetical (`0xC8B3B245`) that did NOT agree
// with its own stated decimal value (3367956165 decimal is actually
// 0xC8BEEEC5) - flagged by this file rather than silently picked, since
// Team B had no way to re-derive the real on-disk value independently. Team
// A's own population replay (19,800 named objects) CONFIRMED the DECIMAL
// value is the real, load-bearing one - 0xC8BEEEC5 - and the hex
// parenthetical was the mis-conversion. The constant below was changed from
// the (wrong) hex-parenthetical reading to the confirmed 0xC8BEEEC5. See
// tests/synthetic_zone_objects_test.cpp's own testPositionPropertyIdPinnedValue
// for the exact numbers. **Any Navpoint/cover_node position read against the
// old 0xC8B3B245 id was silently missing it (ObjectPosition()/
// MakeNavpointFromRecord() would have returned std::nullopt for every real
// instance)** - this fix is expected to change sr3airuntime's
// ExtractNavpoints() / navpoint-selection results materially, not just add
// test coverage.
constexpr uint32_t kPositionPropertyId = 0xC8BEEEC5u;

// Sec11.7: two real, both-common on-disk shapes for the position/transform
// property - `kind=2`/`size=12` (plain float32 X/Y/Z, no orientation) and
// `kind=3`/`size=28` (X/Y/Z plus a 4-float quaternion) - "which shape a
// given object uses is an authoring choice per marker, not a format
// constant." This decodes the leading float32 X/Y/Z triple from EITHER
// shape; the quaternion, when present (kind=3 only), is NOT decoded here -
// Sec11.7 gives no confirmed component order/convention for it, and
// nothing this project currently builds needs a Navpoint/cover_node's
// ORIENTATION, only its position (see sr3airuntime/navpoint_selector.h;
// Sec15.6 separately flags `cover_orient`'s own 6-way jump table OPEN).
// Floats are read via a plain memcpy from the on-disk little-endian bytes,
// matching this project's standing little-endian-host assumption (every
// other multi-byte field in this reader is decoded the same way). Returns
// std::nullopt for any other size - never guessed at.
struct Position3f {
    float x = 0, y = 0, z = 0;
};
std::optional<Position3f> DecodePropertyAsPosition(const ObjectPropertyEntry& entry);

// Convenience accessor: FindTailEntryById(rec, kPositionPropertyId) +
// DecodePropertyAsPosition() - std::nullopt if `rec` carries no
// position/transform property at all. Not every object needs one placed
// this way; a caller must still handle std::nullopt, never assume
// presence (this task did not independently re-verify that every one of
// the real 5,982 Navpoint / 12,956 cover_node instances, spec-ai-runtime.md
// Sec15.4, actually carries this property - only that the property format
// itself, Sec11.7, is real and decodable).
std::optional<Position3f> ObjectPosition(const struct ObjectRecord& rec);

// `navpoint_flags` (spec-ai-runtime.md Sec15.1: "rare bitmask, 9.1% of
// instances"; Sec15.2: bits 0x1/0x2/0x4/0x8 select which of 4 flat global
// arrays a Navpoint self-registers into). Id computed via
// sr3save::nameHash(), per this header's own house convention (never a
// hand-transcribed hex literal when the spec gives a NAME string rather
// than a numeric id directly - contrast kCtgObjectNamePropertyId/
// kPositionPropertyId above, where the spec itself states the numeric id).
uint32_t NavpointFlagsPropertyId();
std::optional<uint64_t> NavpointFlags(const struct ObjectRecord& rec);

// NEW 2026-10-10 (`Trigger_Volume` on-disk shape/extent/radius wiring task):
// spec-lua-api-behaviour.md Sec1.14's own "On-disk gaps - RESOLVED 2026-10-10"
// paragraph (cross-referenced from spec-zone-data-format.md Sec11.7's own
// "A third class resolved" paragraph) gives the real named tail properties a
// `Trigger_Volume` object's class-specific vtable-`+0x04` parser reads:
// `trigger_shape_type` (a STRING, resolved through a name-to-enum lookup
// at load time into the runtime shape code 0/1/2: 0=oriented box, 1=sphere,
// 2=capped cylinder - the exact on-disk STRING SPELLINGS that lookup accepts
// are not given; Sec1.14 itself says "literal shape-name strings not traced
// to full depth"), `trigger_shape_param1`/`trigger_shape_param2` (box: BOTH
// read as 3-component float vectors, the box's own local-space min/max
// corner; sphere: param1 only, a scalar f32 radius, param2 not read at all;
// cylinder: both scalars, param1=radius, param2=length), plus
// `trigger_flags`/`trigger_action`/`trigger_action_param`/
// `trigger_action_param_2`/`trigger_use_message`/`trigger_max_fires`/
// `trigger_delay`/`trigger_sync` (exposed here on the same footing as the
// shape fields - not all are consumed by this project's own
// TriggerVolumeGeometry yet, same "expose everything the spec names, let a
// future caller decide what it needs" convention as script_vehicle_variant_
// name above). Every id computed via sr3save::nameHash(), per this header's
// own house convention (Sec1.14 gives NAME strings for these properties, not
// numeric ids - same situation as the script_npc_*/script_vehicle_*/
// navpoint_flags family above, contrast kCtgObjectNamePropertyId/
// kPositionPropertyId which the spec DOES give numeric ids for directly).
uint32_t TriggerShapeTypePropertyId();
uint32_t TriggerShapeParam1PropertyId();
uint32_t TriggerShapeParam2PropertyId();
uint32_t TriggerFlagsPropertyId();
uint32_t TriggerActionPropertyId();
uint32_t TriggerActionParamPropertyId();
uint32_t TriggerActionParam2PropertyId();
uint32_t TriggerUseMessagePropertyId();
uint32_t TriggerMaxFiresPropertyId();
uint32_t TriggerDelayPropertyId();
uint32_t TriggerSyncPropertyId();

// Generic decoders this family needs beyond DecodePropertyAsUnsignedInt()/
// DecodePropertyAsText()/DecodePropertyAsPosition() above: a plain scalar
// float32 (trigger_shape_param1 for sphere/cylinder) and a plain 3-float
// vector with NO position-property semantics implied (trigger_shape_param1/2
// for a box - the box's own local-space min/max CORNER, not an object
// position; reusing Position3f's own field names (x/y/z) only because the
// shape is identical, not because this is a position). Both return
// std::nullopt for any other byte width - never guessed at, same convention
// as every other DecodePropertyAs*() in this header.
std::optional<float> DecodePropertyAsFloat(const ObjectPropertyEntry& entry);
std::optional<Position3f> DecodePropertyAsVec3(const ObjectPropertyEntry& entry);

// Named, typed convenience accessors, same FindTailEntryById() mechanism as
// every other family in this header. ScriptNpcFlags()-style raw-string
// accessor for the shape-type name (the enum resolution itself - matching
// the on-disk string against whatever literal spellings the real
// name-to-enum lookup accepts - is NOT done here: Sec1.14 does not give
// those spellings, so this reader surfaces the raw authored string and lets
// a caller apply its own empirically-grounded mapping, exactly this
// project's "never guess, read real data instead" convention; see
// TriggerShapeCodeForTypeString() below for that empirical mapping, built
// from real `.czn_pc` population data rather than from the unreached literal
// strings). TriggerShapeParam1/2 return the RAW property entry (not a
// decoded value) because which decoder applies (DecodePropertyAsFloat vs
// DecodePropertyAsVec3) depends on the shape code, which the caller must
// already have resolved.
std::optional<std::string> TriggerShapeType(const struct ObjectRecord& rec);
const ObjectPropertyEntry* TriggerShapeParam1(const struct ObjectRecord& rec);
const ObjectPropertyEntry* TriggerShapeParam2(const struct ObjectRecord& rec);
std::optional<std::string> TriggerActionParam(const struct ObjectRecord& rec);
std::optional<uint64_t> TriggerMaxFires(const struct ObjectRecord& rec);
std::optional<uint64_t> TriggerDelay(const struct ObjectRecord& rec);

// Empirically-grounded shape-type string -> runtime enum mapping (2026-10-10):
// built from a real `.czn_pc` population scan (tools/zone_object_resolver_
// probe.cpp's own trigger-shape extension, run against the real shipped
// `sr3_city_0.vpp_pc`; this project reads real game files for this, never
// disassembles - see this project's own standing clean-room boundary), NOT
// from Sec1.14's own unreached literal strings. RESULT (1,277 real
// Trigger_Volume records, classHash 0xf300989f, 0 missing the property, 0
// unmapped): exactly three distinct on-disk strings, "sphere" (118), "cylinder"
// (254), and "bounding box" (905, NOT the bare word "box") - matched here by a
// case-insensitive SUBSTRING test ("box"/"sphere"/"cylinder") so "bounding
// box" still matches the first. Returns std::nullopt for any text the scan
// did not observe - never guessed, matching Sec1.14's own "any other value
// fails the containment test" framing for the runtime shape code.
std::optional<int> TriggerShapeCodeForTypeString(const std::string& shapeTypeText);

// One 0x2234-record: the 28-byte fixed head (Sec11.4) plus its variable
// property tail (Sec11.7).
struct ObjectRecord {
    // --- fixed 28-byte head, on-disk offsets relative to this record's own
    // start (recordOffset below). Field-by-field CONFIRMED - disassembly,
    // Sec11.4 ("every offset below was read directly off its own MOV
    // instructions"). ---

    uint32_t handleLo = 0;        // +0x00 object's own handle, low dword (0 = allocate fresh)
    uint32_t handleHi = 0;        // +0x04 object's own handle, high dword
    uint32_t ownerHandleLo = 0;   // +0x08 owner/parent handle, low dword
    uint32_t ownerHandleHi = 0;   // +0x0c owner/parent handle, high dword

    // +0x10: CONFIRMED a plain 32-bit integer hash, NOT a string pointer
    // (Sec11.5 - this corrects an early, already-retracted misreading of a
    // prior same-project note). See classHashBucket below for the one step
    // of resolving it this reader can perform.
    uint32_t classHash = 0;

    // +0x14: copied through by the generic head-copier; not read by any
    // registry code disassembled this pass. OPEN - Sec11.4.
    uint16_t fieldAt0x14 = 0;

    // +0x16: CONFIRMED - the length, in bytes, of this record's variable
    // tail. This is the on-disk SOURCE offset; Sec11.4 explicitly flags
    // that a prior same-project note named the in-memory DESTINATION offset
    // (+0x1a) instead, and corrects it here: "the on-disk field is at
    // +0x16, not +0x1a." `recordSize = kObjectRecordHeadSize + tailLength`
    // is the exact formula `0x00457c60`/`0x00457a50`'s shared seek
    // (`0x00da7f30`, mode 1) computes and uses to reach the next record.
    uint16_t tailLength = 0;

    // +0x18: CONFIRMED - byte offset, WITHIN THE TAIL (i.e. relative to
    // recordOffset + 0x1c, not to the record's own start), of the object's
    // `ctg_object_name` property data - a NUL-terminated string - when
    // non-zero. Zero means unnamed. Sec11.4/Sec11.7.
    uint16_t nameOffsetInTail = 0;

    // --- derived ---

    size_t recordOffset = 0; // absolute offset of this record's own start
                              // within the buffer parseAt()/locate() was given
    size_t tailOffset = 0;   // == recordOffset + kObjectRecordHeadSize

    // Sec11.5: wangHash(classHash) % 510. CONFIRMED algorithm, but a bucket
    // ordinal only - NOT a class identity. The chained table mapping a
    // bucket to a registered class ordinal (`0x004586a0` off
    // registry+0xc6c) and the class ordinal -> `0x02cc9900` lookup are both
    // runtime-populated and were not, and cannot currently be, resolved
    // statically via THIS route (Sec11.5) - this field is reported as a
    // bucket index deliberately, never as a guessed name. For a NAME, use
    // `ClassNameForHash(classHash)` above instead (a different, static-
    // data-only route via Sec11.7's own dictionary attack - covers 21 of
    // the 40 classes it resolved, see that function's own comment); do not
    // expect `classHashBucket` itself to ever resolve to one.
    uint32_t classHashBucket = 0;

    bool hasName = false; // == (nameOffsetInTail != 0)

    // The decoded name string, when hasName is true - CONFIRMED mechanism
    // (Sec11.7: "the record head's +0x18 'name offset' field is the byte
    // offset, within the tail, where this specific property's data (a
    // NUL-terminated string) begins"). Read directly from the tail's own
    // bytes at nameOffsetInTail, independent of whether tailEntries() below
    // successfully segmented the generic property list - this one field
    // does not need the tail's interior shape to be understood at all.
    // Left empty (not populated) if nameOffsetInTail falls outside
    // [0, tailLength) or no NUL terminator is found before the tail's own
    // end (a malformed record is reported structurally, never guessed at).
    std::optional<std::string> nameText;

    // The tail's raw bytes, verbatim, length == tailLength. ALWAYS
    // populated for every accepted record - this is the CONFIRMED
    // container (Sec11.4's "+0x16... length of the variable tail"); every
    // other tail-interior field below is a best-effort decode layered on
    // top of this, never a replacement for it.
    std::vector<uint8_t> rawTail;

    // --- tail-interior decode: entry SHAPE is CONFIRMED (Sec11.7), entry
    // STRIDE (how consecutive entries pack) is Team B's own empirical
    // finding, not stated anywhere in Sec11. ---
    //
    // Sec11.7 confirms the shape of one property-list entry (the 8-byte
    // kind/size/id header above) by reading its writer, but Sec11 never
    // states how consecutive entries pack, or how a reader independently
    // knows where the entry LIST ends within a record whose total tail
    // length is already known from the head (+0x16).
    //
    // RESOLVED EMPIRICALLY, 2026-10-07 (tools/zone_objects_population.cpp,
    // this reader's own whole-population real-data run, sr3_city_0.vpp_pc:
    // 2,068 `.czn_pc` files, 603 with a located Objects section, 156,101
    // records): walking consecutive entries with the LITERAL stride
    // `8 + size` tiles its own record's declared tailLength exactly in only
    // 58,146 / 156,101 (37.2%) of records; walking with stride
    // `alignUp(8 + size, 4)` - i.e. each entry individually padded to a
    // 4-byte boundary - tiles EXACTLY in **156,101 / 156,101 (100%), zero
    // overruns, zero leftover bytes, across every record this reader
    // located**. That is a controlled before/after comparison on real
    // shipped data, not a plausibility argument, and it is what this
    // implementation actually uses (src/zone_objects.cpp's
    // decodeTailEntries()). It is reported to the orchestrator as a new
    // finding for Team A to fold into spec-zone-data-format.md directly -
    // this header does not claim Sec11.7 itself already states it, and a
    // future real file this reader has not yet seen could still disagree
    // (tailOverrun below would flag that, not silently hide it).
    //
    // What remains a genuine inference, now strongly corroborated by the
    // same 100%/0-exceptions result rather than merely plausible: that the
    // tail holds NO leading entry-count field or terminal sentinel of its
    // own, and is simply consumed until the running offset reaches
    // tailLength. Team A's own open U2 ("record interiors", Sec11.9 item
    // 8) is still the unit that should fold this into the spec text itself
    // and decide whether a non-`.czn_pc` caller of the same generic
    // property-list format (Sec11.7 names `0x00456660` as a general,
    // zone-independent writer) ever disagrees.
    //
    // Never throws, never trusted as a hard parse boundary: a record whose
    // tail entries do not cleanly tile to tailLength still parses
    // successfully (rawTail above is unaffected), it is simply reported as
    // tailOverrun/not-exact-fit rather than rejected.
    std::vector<ObjectPropertyEntry> tailEntries;
    size_t tailBytesDecoded = 0; // sum of alignUp(8 + size, 4) over every accepted entry
    bool tailExactFit = false;   // tailBytesDecoded == tailLength, zero leftover
    bool tailOverrun = false;    // an entry's declared `size` would read past
                                  // tailLength - decode stopped at that entry,
                                  // tailEntries holds every entry accepted before it
};

// A parsed Objects (0x2234) section: the FBOW/version-5 payload header, the
// directory array, and the per-object record stream.
struct ObjectsSection {
    // Absolute offset, within the buffer passed to parseAt()/locate(), of
    // the payload's own magic field (`P` in Sec11.3's own notation).
    size_t payloadOffset = 0;

    uint32_t version = 0;             // +0x04, CONFIRMED == kObjectsPayloadVersion
    uint32_t recordCount = 0;         // +0x08, CONFIRMED - the per-object record count
                                       // (Sec11.3/Sec11.6; a SEPARATE field from
                                       // directoryEntryCount below, confirmed as two
                                       // distinct reads at two distinct offsets)
    uint32_t directoryEntryCount = 0; // +0x0c, CONFIRMED - directory-array entry count

    size_t directoryOffset = 0;    // CONFIRMED arithmetic: align4(payloadOffset + 0x20)
    size_t recordStreamOffset = 0; // CONFIRMED arithmetic:
                                    // align4(directoryOffset + directoryEntryCount*8)

    std::vector<ObjectDirectoryEntry> directory;
    std::vector<ObjectRecord> records;

    // True iff all `recordCount` records were read without running past the
    // end of the buffer given to parseAt(). A real, well-formed section is
    // always complete; false marks a candidate this reader's locate() chose
    // not to reject outright (bounds held for the header/directory) but
    // whose declared record count the buffer could not actually satisfy -
    // kept for diagnostics, never silently upgraded to "complete".
    bool complete = false;
};

class ZoneObjects {
public:
    // Scans `cznContent` (an entire `.czn_pc` buffer) for Objects-payload
    // candidates - 4-aligned offsets where `u32 == 'FBOW'` is immediately
    // followed by `u32 == 5` - and fully parses each one via parseAt(),
    // keeping only the candidates that satisfy every CONFIRMED structural
    // bound (directory array and record stream both fit within the
    // buffer). See the file-level comment for why this is a
    // locate-by-signature strategy rather than a replay of the real
    // top-level {tag,length} walker.
    //
    // Never throws. A `.czn_pc` with no Objects section (or one this
    // reader's signature scan cannot reach) yields an empty vector - the
    // documented non-error case (Sec11.2 item 1: "a zone with no Objects
    // section is not an error"). Ordinarily returns 0 or 1 entries (a zone
    // has at most one Objects section load, Sec11.1/Sec11.2); returning
    // more than one would mean either a genuinely unusual file or a
    // coincidental signature match elsewhere that nonetheless satisfied
    // every bounds check - both are reported as-is, never collapsed to one
    // silently.
    static std::vector<ObjectsSection> locate(ByteView cznContent);

    // Parses exactly one Objects payload at a KNOWN offset (bypasses the
    // signature scan) - throws FormatError if the magic/version do not
    // match, or if the directory array or the declared `recordCount`
    // records cannot fit within `cznContent`. Exposed directly (not just
    // through locate()) for a caller that already has payloadOffset from
    // elsewhere, and so synthetic tests can assert rejection behaviour at
    // an exact, known offset.
    static ObjectsSection parseAt(ByteView cznContent, size_t payloadOffset);
};

} // namespace sr3zone

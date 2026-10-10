// HIRC walker for the plain `<name>.bnk_pc` sibling (the genuine,
// publicly-documented Wwise SoundBank - spec-audio-format.md Sec3), plus the
// Sound (kind 2) HIRC object's own on-disk source-descriptor parse, and the
// event/sound-id -> embedded-media lookup those two together make possible.
//
// ---------------------------------------------------------------------
// WHY THIS FILE EXISTS NOW, AND NOT BEFORE
// ---------------------------------------------------------------------
// include/sr3audio/media_bank.h's own top comment (written before this spec
// landed) states "there is deliberately no HIRC walker ... and none should
// be added", reasoning that re-deriving Wwise's own per-HIRC-kind object
// model would be exactly the kind of public-format re-derivation this
// project's cleanroom rule exists to avoid. spec-audio-runtime.md Sec8 (HIRC
// load: the bank-chunk reader and the per-kind construction dispatch) and
// Sec9.7/Sec9.8 (2026-10-09, "wave 6") close that gap properly instead of
// working around it: Team A's own disassembly-based RE (never Team B's, and
// never read directly by Team B - only the resulting spec text is) now gives
// a CONFIRMED per-HIRC-kind dispatch table, CONFIRMED real class identities
// for kinds 1-4, and a CONFIRMED on-disk source-descriptor layout for the
// dominant Sound(kind 2) object, closing exactly the open item media_bank.h
// used to flag as out of reach. See media_bank.h's own UPDATE note for the
// scope line between that file and this one: media_bank.h still only reads
// the `_media.bnk_pc` wrapper's OWN directory and never decodes a payload
// byte; this file reads the PLAIN sibling's HIRC chunk far enough to recover
// one Sound object's own declared source/media id, and nothing past that -
// no Vorbis decode, no further Wwise-internal derivation beyond what
// Sec8/Sec9.7/Sec9.8 confirm.
//
// ---------------------------------------------------------------------
// Top-level chunk framing (spec-audio-format.md Sec3; spec-audio-runtime.md
// Sec8.2's own FourCC dispatch read, and Sec8.6's "walked using the public,
// documented Wwise chunk/item framing - no cleanroom RE owed for the
// generic container shape")
// ---------------------------------------------------------------------
// The plain bank is a flat run of `{FourCC(4), u32 size(4), size bytes of
// payload}` chunks from byte 0 to EOF - the same generic, publicly-
// documented shape wwise_bank_id.h already reads the FIRST chunk's own
// header out of (that file deliberately reads no further - this is a
// SEPARATE file precisely so that one's frozen scope is never touched).
// findHircChunk() below walks that same chunk list only far enough to
// locate the one tagged `HIRC` (`0x43524948` as a little-endian dword,
// spec-audio-runtime.md Sec8.2's own CONFIRMED read of the literal compare
// at VA 0x00f5991c), and does not interpret `STMG`/`DATA`/`STID` beyond
// skipping past them by their own declared size.
//
// ---------------------------------------------------------------------
// HIRC item list (spec-audio-runtime.md Sec8.2.1/Sec8.2.3, CONFIRMED by
// direct listing; Sec8.6's install-wide 41,037-item census independently
// replays this same framing over real shipped data)
// ---------------------------------------------------------------------
// Chunk payload: `u32 numItems`, then `numItems` items. Each item is read as
// a 5-byte header - `u8 kind` then `u32 length` (Sec8.2.1: "a 5-byte read
// ... into a local buffer" with "the kind byte sits at buffer offset +0") -
// followed by `length` more bytes, of which the first 4 are the item's own
// declared object id (Sec8.2.3: "id = item_header[+1] ; the HIRC item's own
// declared object id") and the remainder is the kind-specific body. The
// per-item dispatch loop advances past `length` bytes of body regardless of
// whether the kind is recognised (Sec8.2.2), which is what lets a caller
// walk the whole item list without understanding every kind's own body.
//
// Kind byte is 1-based; the real dispatch is a 19-entry jump table (valid
// range 1..19, Sec8.2.1: "kind values are 1-based ... valid kind range is
// [1,19]"), out-of-range falls to a shared "unregistered kind" handler that
// constructs nothing (Sec8.2.2).
//
// ---------------------------------------------------------------------
// Real kind identities - Sec9.7 (2026-10-09), CORRECTING Sec8.2.1's own
// first-pass guess that `vt_ca54`/`vt_ca68` were base/derived and that the
// Action class was kind 1
// ---------------------------------------------------------------------
//   kind 2  -> `vt_ca68`, the real, DOMINANT Sound/SFX HIRC-object class.
//              17,952 items across 717/721 real parsed banks (Sec8.6
//              census) - near-universal. NOT an Action, despite Sec9.4's
//              earlier reading; re-attributed in Sec9.7.
//   kind 3  -> the genuinely populated Action-bearing class. 10,597 items
//              across 668 banks - second-dominant. Together kinds 2+3 are
//              ~70% of all real HIRC content (Sec8.6).
//   kind 4  -> Event. CONFIRMED by a byte-for-byte structural match to
//              `PostEvent`'s own Event lookup (Sec9.2.1 step 2).
//   kind 1  -> `vt_ca54`, a real, separate, non-fallback-dispatched class
//              with its OWN construction code - but ZERO real population
//              anywhere in the shipped corpus (Sec8.6/Sec9.7). Real
//              identity undetermined (dead/legacy or simply unexercised).
//   kinds 10-13 -> CONFIRMED zero population AND no construction code at
//              all (share the shared "unregistered" fallback slot, Sec8.2.1/
//              Sec8.2.2) - HIGH CONFIDENCE (not CONFIRMED: this binary
//              carries no string/RTTI name for any kind value) identified
//              with the public Wwise Music Segment/Track/Switch-Container/
//              Playlist-Container taxonomy, on the strength of "exactly 4
//              consecutive never-implemented kinds" agreeing with an
//              independent code-side finding that the Interactive Music
//              Engine is never fed content (Sec8.6).
//   kinds 15,16,17 -> ALSO zero real population despite having real,
//              non-fallback construction code (Sec8.6) - "real but unused",
//              a different flavor of zero from 10-13's "no code path at
//              all". Not modelled here beyond being valid, walkable kinds.
//
// This header exposes kind 2/3/4's own byte values as named constants
// because they are CONFIRMED identities; it does NOT expose a full
// kind-to-name enum for every one of the 19 values, since most of those
// names are HIGH CONFIDENCE at best (Sec8.2.1/Sec8.7) and this project's
// standing rule is to surface an OPEN field raw rather than invent a label
// for it.
//
// ---------------------------------------------------------------------
// Sound (kind 2) object: the on-disk source descriptor - Sec9.8 (2026-10-09)
// ---------------------------------------------------------------------
// Kind 2's real apply-fields function (`0x00f76de0`) reads the item's raw
// bytes after skipping 4 bytes past the item's own id (role of those 4
// bytes: OPEN, not stated by Sec9.8), then - for the ordinary one-shot-SFX
// case (a single candidate source, Sec9.8's own "count==0" branch; the
// multi-candidate-source container variant, `0x00f60600`, is explicitly
// "not opened further" and is NOT modelled by this file) - reads one source
// descriptor via the shared, kind-agnostic byte-stream parser `0x00f56ba0`:
//
//   Read order | Size   | CONFIRMED mechanics                    | Meaning (HIGH-confidence label, not named by the binary)
//   -----------|--------|-----------------------------------------|---------------------------------------------
//   R1         | dword  | unconditional                          | plugin/codec-type id (low nibble selects branch)
//   R2         | dword  | unconditional, drives every branch     | stream-type selector - structurally 3-valued {0,1,2}
//   R3         | dword  | unconditional                          | source/media id
//   R4         | dword  | unconditional                          | size-or-companion-id
//   R5,R6      | dword,dword | only if R2 != 1                   | extra size/offset fields
//   B1         | byte   | unconditional (bits 0/1 + (R2==2) packed on the OUTPUT side) | per-source flag byte
//   R8,R9      | dword,dword | only if (R1&0xF) is 2 or 5; cursor advances past the blob afterward | embedded-parameter-blob size/companion
//
// Branch structure (CONFIRMED, literal immediates): `(R1&0xF)==0` -> no
// source plugin, empty slot; `(R1&0xF)==1` -> the plain single-id
// codec/source case - the shape a one-shot SFX's embedded Vorbis source
// takes (R2 then checked to be one of {0,1,2}); `(R1&0xF)` is 2 or 5 -> an
// embedded parameter-blob generator source (`CAkFXSrcSine` and similar);
// anything else -> rejected. R2's 3-valued branch is the real
// "bank-resident vs. streamed" split: `R2==1` skips R5/R6 (plain streaming,
// no prefetch); `R2==0`/`R2==2` both read them.
//
// **R3 (source/media id) is CONFIRMED to equal the companion
// `_media.bnk_pc` wrapper's own per-record `tag` field exactly**
// (spec-audio-format.md Sec4.2, resolved by Sec9.8's own worked example: 5
// of 6 real kind-2 Sound items in the real bank pair `sh04.bnk_pc`/
// `sh04_media.bnk_pc` (cross-reference id `0xd7fbe8e8`) had their decoded R3
// field match a real `sh04_media.bnk_pc` record's own `tag` exactly - e.g.
// Sound item `0x297d8b3b` -> R3 `0x3920cac1` -> media record index 3, tag
// `0x3920cac1`). This is the mechanism resolveSoundIdToMedia() below
// implements: Sound HIRC item id -> its own R3 -> the `_media.bnk_pc`
// record whose `tag` equals it -> that record's `offset`/`size`.
//
// ---------------------------------------------------------------------
// SCOPE, stated plainly
// ---------------------------------------------------------------------
// This covers exactly the "ordinary one-shot SFX" shape Sec9.8 confirms and
// its own worked example validates (a Sound item with a single, plain
// embedded-codec source). NOT modelled, consistent with the spec's own
// OPEN items: the multi-candidate-source container variant (`0x00f60600`);
// the literal `RegisterCodec` factory dispatch; decoding the embedded
// parameter-blob generator sources' own blob contents (the blob is skipped,
// never interpreted); the voice-line external-source mechanism (Sec9.8's
// own "genuinely separate id-sourcing mechanism" - out of scope here by
// definition, not an oversight). None of these are guessed at; each throws
// or is simply not reachable through this file's own public API.
//
// Like media_bank.h/wwise_bank_id.h, this file does not own the underlying
// bytes - callers must keep the backing buffer(s) alive for the lifetime of
// any ByteView/HircItem this file returns.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sr3audio/errors.h"
#include "sr3audio/media_bank.h"
#include "vpp/byte_view.h"

namespace sr3audio {

using vpp::ByteView;

// ASCII `HIRC` read as a little-endian u32 - spec-audio-runtime.md Sec8.2's
// own CONFIRMED read of the literal compare at VA 0x00f5991c.
constexpr uint32_t kHircChunkTag = 0x43524948u;

// Locates the HIRC chunk inside a full plain `.bnk_pc` byte buffer (the
// whole bank, starting at its own `BKHD` magic - NOT the `_media.bnk_pc`
// wrapper, see media_bank.h for that). Walks the generic, publicly-
// documented `{FourCC, u32 size, payload}` chunk list (spec-audio-format.md
// Sec3; spec-audio-runtime.md Sec8.6's own "public, documented Wwise
// chunk/item framing" note) until it finds a chunk tagged `HIRC`, and
// returns that chunk's own payload (after its own 8-byte FourCC+size
// header). Returns std::nullopt if no HIRC chunk is present - not asserted
// that every bank must have one. Throws FormatError if a chunk's declared
// size runs past the end of `bank`.
std::optional<ByteView> findHircChunk(ByteView bank);

// One parsed HIRC item: the common 1-byte kind + 4-byte id header every
// kind shares (Sec8.2.1 kind byte; Sec8.2.3 "the HIRC item's own declared
// object id"), plus the kind-specific body bytes that follow the id (the
// item's own declared length, minus the 4 id bytes). `body` is a view into
// the same backing buffer as the chunk passed to walkHircItems().
struct HircItem {
    uint8_t kind = 0;
    uint32_t id = 0;
    ByteView body;
};

// Walks the HIRC chunk payload's own item list: `u32 numItems`, then
// `numItems` times `{u8 kind, u32 length, length bytes (first 4 = id)}`
// (Sec8.2.1/Sec8.2.3). Throws FormatError if a declared item length runs
// past the chunk's own bytes, or if an item's declared length is less than
// 4 (too short to hold even the id).
std::vector<HircItem> walkHircItems(ByteView hircChunkPayload);

// Sec8.2.1: the real dispatch is a 19-entry table, kind byte 1-based,
// valid range [1,19] inclusive; out of range shares the "unregistered"
// fallback (Sec8.2.2) and constructs nothing.
constexpr uint8_t kHircKindMin = 1;
constexpr uint8_t kHircKindMax = 19;

// Sec9.7: the real, dominant Sound/SFX class (17,952 items/717 banks).
constexpr uint8_t kHircKindSound = 2;
// Sec9.7/Sec8.6: the genuinely populated Action-bearing class (10,597
// items/668 banks) - second-dominant, NOT kind 1 (Sec8.2.1's original guess,
// corrected by Sec9.7).
constexpr uint8_t kHircKindAction = 3;
// Sec9.7: CONFIRMED Event, by structural match to PostEvent's own lookup.
constexpr uint8_t kHircKindEvent = 4;

// Sec8.2.1/Sec8.6: kinds 10-13 share the shared fallback with NO
// construction code at all, and are CONFIRMED zero population install-wide
// - HIGH CONFIDENCE (not CONFIRMED - no RTTI/string name in this binary for
// any kind) identified with the public Wwise Music Segment/Track/
// Switch-Container/Playlist-Container taxonomy.
constexpr uint8_t kHircKindUnregisteredFallbackMin = 10;
constexpr uint8_t kHircKindUnregisteredFallbackMax = 13;

// True for a kind byte in [10,13] - the structurally-confirmed
// "unregistered, no construction code" range (Sec8.2.1/Sec8.2.2/Sec8.6).
// Does NOT claim anything about kinds that are merely unpopulated-but-coded
// (1, 15, 16, 17 - Sec8.6) - those are valid, walkable kinds with real
// construction helpers this project just never saw exercised.
bool isHircKindInUnregisteredFallbackRange(uint8_t kind);

// Sec9.8's own shared byte-stream parser (`0x00f56ba0`) output, restated as
// a struct. Field names describe this project's own HIGH-confidence
// reading of each field's role (see the table in this file's own top
// comment); `pluginOrCodecTypeId`/`streamTypeSelector`/`sourceMediaId`/
// `sizeOrCompanionId` and the conditional fields are CONFIRMED to exist and
// be read in this order and under these conditions.
struct SoundSourceDescriptor {
    uint32_t pluginOrCodecTypeId = 0;  // R1 - low nibble selects the branch below
    uint32_t streamTypeSelector = 0;   // R2 - "register only" on the real side; surfaced here for callers, drives every branch
    uint32_t sourceMediaId = 0;        // R3 - CONFIRMED == the companion _media.bnk_pc record's own `tag` (spec-audio-format.md Sec4.2)
    uint32_t sizeOrCompanionId = 0;    // R4
    bool hasPrefetchFields = false;    // true iff streamTypeSelector != 1 (R5/R6 present)
    uint32_t extraField1 = 0;          // R5 - only meaningful if hasPrefetchFields
    uint32_t extraField2 = 0;          // R6 - only meaningful if hasPrefetchFields
    uint8_t sourceFlags = 0;           // B1 - per-source flag byte, role of individual bits OPEN
    bool hasEmbeddedParamBlob = false; // true iff (pluginOrCodecTypeId & 0xF) is 2 or 5
    uint32_t paramBlobSize = 0;        // R8 - only meaningful if hasEmbeddedParamBlob; the blob itself is SKIPPED, never decoded
    uint32_t paramBlobCompanion = 0;   // R9 - only meaningful if hasEmbeddedParamBlob
};

// Sec9.8's own branch classification ("Branch structure, CONFIRMED literal
// immediates"), read from `pluginOrCodecTypeId & 0xF`.
enum class SoundSourcePluginKind {
    kNone,             // (R1&0xF)==0 - no source plugin, empty slot
    kPlainCodec,        // (R1&0xF)==1 - the plain single-id codec/source case: ordinary one-shot SFX
    kEmbeddedParamBlob, // (R1&0xF) is 2 or 5 - CAkFXSrcSine/CAkToneGen/CAkModalSynth/CAkFXSrcAudioInput
    kRejected,          // anything else
};

SoundSourcePluginKind classifySoundSourcePlugin(const SoundSourceDescriptor& d);

// Parses one source descriptor starting at the FRONT of `soundSourceBytes`
// (i.e. at R1) per the read order/conditions in this file's own top
// comment. Throws FormatError if `soundSourceBytes` is too short for the
// fields the branch structure requires (including the embedded-parameter
// blob's own declared size, for the kEmbeddedParamBlob case).
SoundSourceDescriptor parseSoundSourceDescriptor(ByteView soundSourceBytes);

// Convenience: given a kind-2 HircItem, skips the 4 (OPEN-role) bytes
// Sec9.8 says the real apply-fields function skips past the id, then calls
// parseSoundSourceDescriptor() on what remains. Throws FormatError if
// `item.kind != kHircKindSound`, or if `item.body` is too short to hold
// even the 4 skipped bytes.
SoundSourceDescriptor parseSoundHircItem(const HircItem& item);

// The resolved result of the full id -> media lookup.
struct ResolvedMedia {
    uint32_t soundObjectId = 0;   // the HIRC item's own declared id (input)
    uint32_t sourceMediaId = 0;   // R3 - equal to the matched record's own `tag`
    size_t mediaRecordIndex = 0;  // index into the `mediaRecords` vector passed in
    uint32_t offset = 0;          // MediaBankRecord::offset of the matched record
    uint32_t size = 0;            // MediaBankRecord::size of the matched record
};

// The lookup itself (spec-audio-runtime.md Sec9.8's own worked example,
// restated as code): finds the kind-2 Sound HIRC item whose own declared id
// equals `soundObjectId`, parses its source descriptor, requires it to
// classify as kPlainCodec (the ordinary one-shot-SFX case - kNone/
// kEmbeddedParamBlob/kRejected are all genuinely different cases this
// function does not try to collapse into one), then searches
// `mediaRecords` (as already walked by MediaBank::walk() on the companion
// `_media.bnk_pc`) for the one record whose own `tag` equals the
// descriptor's `sourceMediaId`.
//
// Throws FormatError if: no kind-2 item has that id; the matched item's
// source descriptor is not kPlainCodec; or no record in `mediaRecords` has
// a matching `tag`. Never guesses a "closest" or "first" record on a
// near-miss - a lookup either resolves exactly or is reported as a failure.
ResolvedMedia resolveSoundIdToMedia(const std::vector<HircItem>& hircItems, uint32_t soundObjectId,
                                    const std::vector<MediaBankRecord>& mediaRecords);

} // namespace sr3audio

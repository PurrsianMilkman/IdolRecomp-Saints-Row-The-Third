// Per-identity matlib RESOLUTION/ATTACHMENT model - spec-tables-
// customization.md §12.1b ("FUN_00be0630's real job: per-identity asset
// precache, and how the matlib is actually consumed").
//
// §12.1b traces the real matlib-consumption mechanism end to end, from the
// executable:
//
//   FUN_00da8790 (pure filename compose: strip extension, append the
//                 platform-selected one)
//     -> FUN_00da90d0 (file-exists check)
//     -> FUN_006f6c20 / FUN_006f7330 (open/attach a typed slot to a shared
//                 composite, keyed by the identity's own Name)
//     -> FUN_00db48a0 (the real cache: a name-lookup short-circuits on a
//                 hit; only a miss issues a load via the deeper streaming
//                 primitive FUN_00dd35e0, not expanded)
//
// The matlib is registered under resource TYPE TAG 0x16, CONFIRMED at all 4
// of its real call sites - the SAME per-resource-TYPE attachment pattern the
// engine uses for mesh (type 5/7), texture peg (type 0x10) and morph
// (type 7): each is independently filename-composed, existence-checked, and
// registered as its OWN typed slot under the same name-keyed composite.
//
// EXPLICITLY CONFIRMED NOT TO BE (§12.1b): a wholesale override of the
// mesh's embedded material table, and NOT a per-texture-slot splice. Nowhere
// in the traced chain does code reach into an already-loaded mesh's
// material table and patch individual slots.
//
// EXPLICITLY OPEN, NOT MODELLED HERE (§12.1b, do not invent): the true
// render-time consumer that turns a cached 0x16-tagged matlib handle into
// actual D3D texture-stage bindings lies PAST this chain (named as living
// somewhere past FUN_00dd35e0 / FUN_00db3c80 -> FUN_00db3b40) and was not
// traced. This file models the RESOLUTION/ATTACHMENT step only - i.e. it
// answers "does this identity have a matlib, and what does it contain as
// its own independent resource" - not "which material slot(s) does it bind
// to at render time." Replaces tools/sr3_viewer.cpp's prior
// `--experimental-head-matlib` texture-NAME-swap heuristic, which assumed a
// per-slot splice model the spec has since refuted.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sr3geometry/material_block.h"

namespace sr3geometry {

// Resource type tags for the identity-keyed, name-keyed attachment
// composite (§12.1b). Only kMatlib is implemented by this file (the other
// three are named here only so the convention this type tag belongs to is
// documented honestly, not to claim they are implemented - their own
// filename-compose/exists-check/attach steps were not built here).
enum class CharacterResourceType : uint32_t {
    kMesh = 5,        // also seen as 7 for some mesh variants (§12.1b) - NOT implemented here
    kMorph = 7,        // NOT implemented here
    kTexturePeg = 0x10, // NOT implemented here
    kMatlib = 0x16,     // CONFIRMED at all 4 real call sites (§12.1b) - this file's own subject
};

// One identity-keyed, type-tagged resource attachment record - what
// FUN_006f6c20/FUN_006f7330 build in the real engine for a Character's
// matlib. `identityName` is the cache key (the Character's own Name, per
// §12.1b: "keyed by the identity's own Name"), NOT a texture or mesh name.
struct CharacterMatlibAttachment {
    std::string identityName;                               // the cache key
    CharacterResourceType type = CharacterResourceType::kMatlib; // 0x16, confirmed
    std::string composedFileName;   // FUN_00da8790's result, e.g. "brad_head.matlib_pc"
    bool locatedInArchive = false;  // FUN_00da90d0's file-exists-check outcome

    // The attached resource's OWN parsed content - its own independent
    // typed slot, never spliced into any mesh's embedded material table.
    // Populated only when locatedInArchive is true, via the already-
    // CONFIRMED, general-purpose MaterialBlock reader (spec-geometry-
    // format.md Sec3.1 / material_block.h) - not a new parser invented for
    // this mechanism. Default-constructed (empty) otherwise.
    MaterialBlock resource;

    // Convenience: whether this attachment resolved to real, parsed
    // content. False whenever locatedInArchive is false, OR the located
    // bytes failed to parse as a MaterialBlock (a real archive hit that
    // isn't actually a material-block-shaped file is not reported as a
    // usable attachment - see ResolveCharacterMatlibAttachment()'s own
    // comment).
    bool attached() const { return locatedInArchive; }
};

// FUN_00da8790's job, per §12.1b: "pure filename compose: strip extension,
// append the platform-selected one." `.matlib_pc` is this project's own
// confirmed PC-platform extension for this resource type (the same "_pc"
// suffix convention every other resource extension in this codebase uses -
// .ccmesh_pc, .rig_pc, .cpeg_pc, etc.).
std::string ComposeMatlibFileName(const std::string& baseName);

// Resolves + attaches `identityName`'s matlib, reproducing the real
// engine's traced 3-step chain (compose -> exists-check -> attach-as-its-
// own-typed-slot) without this file depending on vpp::Container directly:
// `loadByName` is a caller-supplied existence+load predicate (FUN_00da90d0's
// exists-check plus the byte-read a caller like findEntry() against a real
// vpp::Container already does in one step) - returns true and fills `out`
// iff the composed name exists, false otherwise. This keeps the resolution
// logic itself unit-testable against synthetic byte buffers, independent of
// any real archive.
//
// A `loadByName` hit whose bytes do not parse as a MaterialBlock is NOT
// reported as `attached()` - a real exists-check hit on a mis-shaped file
// is not modelled as a usable resource attachment, consistent with every
// other reader in this codebase refusing to guess past a parse failure.
CharacterMatlibAttachment ResolveCharacterMatlibAttachment(
    const std::string& identityName, const std::string& baseName,
    const std::function<bool(const std::string& composedFileName, std::vector<uint8_t>& out)>& loadByName);

// ---------------------------------------------------------------------
// Real-bind follow-up (spec-tables-customization.md §12.1f, 2026-10-08):
// §12.1f CLOSES the "does the live GPU skin-composite pipeline serve NPCs"
// question (negative - §12.1e/§12.1f Q1-Q3, scoped to a 2-slot PLAYER
// co-op record pool, never bound to any character mesh) and confirms NPCs
// instead use individually-named, pre-baked, shipped per-part textures,
// named directly by the character's own `.matlib_pc` "via the same generic
// by-name mechanism §12.1c already confirmed."
//
// IMPORTANT - what §12.1c/§12.1f do NOT confirm: a stated rule for WHICH
// matlib entry replaces WHICH mesh material slot. §12.1c traces the
// matlib's own file-format constructor (`FUN_0074d9a0`) in full and finds
// its texture-name table (the thing MaterialBlock below parses) is
// "explicitly skipped and never retained" by the real engine at runtime -
// i.e. the shipped binary itself has no traced instruction that reads this
// table back out and matches an entry to a mesh slot by name, hash, or
// position. The table is real, static, on-disk DATA (confirmed, and this
// project's own MaterialBlock reader already parses it correctly), but
// the ASSOCIATION between one of its entries and one of the mesh's own
// per-material TextureRef slots (material_binding.h) is NOT a spec-
// confirmed mechanism - material_binding.h's TextureRef carries a
// confirmed parameter-name hash (role) per slot; MaterialBlock's
// textureNames carries no analogous per-entry tag at all, just a flat,
// position-ordered list.
//
// PickMatlibTextureByRoleSuffix() is therefore a CHOSEN (not spec-
// confirmed) heuristic, not a re-derivation of a traced engine mechanism -
// label it as such at every call site, gate it behind an explicit opt-in
// flag, and never freeze a golden baseline against its output. It reuses
// two things that ARE already confirmed, population-wide, in this same
// codebase rather than inventing new numbers: (1) material_binding.h's own
// measured naming convention - the diffuse parameter-hash role correlates
// with a `_d`/`_dp` filename suffix (854/854 measured), and the normal-map
// role correlates with a `_n` suffix (1,833/1,833 measured); (2) a required
// substring (`partNameHint`, e.g. "head") to disambiguate between a
// character's several same-suffixed utility textures living in the SAME
// matlib - confirmed necessary directly from brad_head.matlib_pc's own
// real 6-entry list (HANDOFF.md 2026-10-08 follow-up): `brad_sm_head_d.tga`,
// `brad_sm_head_n.tga`, `skin_ca_02_sb.tga`, `shd_whiteopaque.tga`,
// `eye_reflect_01_sb.tga`, `f_eyelashes_d.tga` - both `brad_sm_head_d.tga`
// AND `f_eyelashes_d.tga` end in `_d`, so suffix alone is ambiguous; only
// the former also contains "head".
//
// Returns a pointer into `textureNames` (first on-disk-order match), or
// nullptr if no entry satisfies both the hint and any listed suffix.
const std::string* PickMatlibTextureByRoleSuffix(const std::vector<std::string>& textureNames,
                                                  const std::string& partNameHint,
                                                  const std::vector<std::string>& suffixes);

}  // namespace sr3geometry

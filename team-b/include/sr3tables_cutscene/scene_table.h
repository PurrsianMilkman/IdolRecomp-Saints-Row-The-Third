#pragma once

// sr3tables_cutscene - the zscene / cutscene scene table built by 0x0073bfb0
// from `cutscene.xtbl` (names) and one `<name>.cte_xtbl` per scene (fields).
//
// Source: spec-lua-api-behaviour.md Sec26.25 ("Globals", "Scene entry (stride
// 0xf8)", lifecycle item 5 "[Replaced, job nnlt]") - the ONLY source used for
// the rules below - plus spec-vpp-container.md Sec2 ("a linear,
// case-insensitive, name-string search across every mounted container's
// directory") for how a scene file is found. Built on sr3xtbl's document
// model and accessors (FindChild / ChildText / Children / GetBool /
// NameHash); nothing here comes from the executable.
//
// LABELS used in the comments: REAL = the spec states it and the section's
// review status marks it CONFIRMED; CHOSEN = this project's own choice where
// the spec says nothing (stated where it happens); OPEN = the spec does not
// give it - the field is std::nullopt / flagged and a consumer must refuse,
// never assume.
//
// What 0x0073bfb0 does (REAL, Sec26.25 item 5, "CONFIRMED - disassembly,
// full listing"):
//   * cutscene.xtbl contributes only the names: each `cutscene` element's
//     `name` child. For the "main" container, names starting with "dlc" or
//     "patch" are skipped; for a patch container only the names starting with
//     the container's own name are taken; at most 200.
//   * The table is allocated for the "main" call with capacity = accepted
//     names + 12, capped at 200; patch containers append.
//   * For each name the scene file `<name>.cte_xtbl` is opened; a missing file
//     means no entry. Its `Cutscene` element is parsed by 0x0073ada0 into the
//     entry fields (SceneFields below).
//   * Entry +0x0 is a heap copy of the name, +0x4 the CRC-32 of the
//     lower-cased name (0x00d9e8b0, seed 0) - sr3xtbl::NameHash is that
//     routine (lower-cased, reflected table CRC, given seed, no final XOR).
//
// Not modelled (no consumer in this project, or OPEN): the two streaming
// request groups +0xc / +0x10 (handles; the female variant), the
// CameraScript / per-shot animation requests (skipped anyway in retail: the
// shipping-mode byte 0x0149365c is always set), the entry constructor
// 0x007233c0 and every field Sec26.25 does not list (OPEN there).
//
// OPEN in the spec and therefore refused here (never guessed):
//   * which patch containers exist and when they are parsed (only the filter
//     rule is given) - BuildSceneTable() takes the container name from the
//     caller; lua_host_run only runs the "main" call;
//   * whether the dlc/patch/container-name prefix tests ignore case - a name
//     that matches a prefix only when case is ignored is reported in
//     NameFilterResult::caseAmbiguous and neither accepted nor skipped;
//   * whether the `CutsceneType` comparison ignores case, and what kind an
//     entry gets with no CutsceneType text - SceneFields::kind is nullopt;
//   * the audio ids of the two Soundtrack halves (0x00462960, "HIGH
//     CONFIDENCE: name -> audio id", not modelled anywhere in this project):
//     only the two name strings are kept; the ids are 0 only when the element
//     is absent (REAL: "Both 0 when absent");
//   * a Soundtrack text that is not of the form `left : right`;
//   * the three fixed resource names an `IsAngel` character contributes;
//   * how a Vehicle's `Variant` text becomes the "Variant id".

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_cutscene {

// Kinds of entry +0x8 (REAL, Sec26.25 "Scene entry": "Zscene" -> 1 (2 when
// 0x0153ba02 is set), "Story" -> 2, "Designer" -> 0, any other text -> 2;
// only kind 1 can be prepared by zscene_prep and the auto-prep).
constexpr int kKindDesigner = 0;
constexpr int kKindZscene = 1;
constexpr int kKindStory = 2;

// The three parser option bytes 0x0153ba00 / 0x0153ba01 / 0x0153ba02 (REAL:
// "All zero; no writer dumped"). Their purpose is HYPOTHESIS in the spec;
// only 0x0153ba02's effect on the kind is used here (the other two choose
// between the +0xc / +0x10 request groups, which are not modelled).
struct ParserOptions {
    bool noFemaleVariant = false;    // 0x0153ba00
    bool femaleVariantOnly = false;  // 0x0153ba01
    bool zsceneAsStory = false;      // 0x0153ba02: "Zscene" scenes become kind 2
};

// One resource of the +0x20 list (REAL: "array pointer, capacity and count of
// the resources of a story cutscene's Characters/Character (its Mesh and
// Killbane names, or the three fixed names when IsAngel is set) and
// Vehicles/Vehicle (the Variant id) ... Empty for a zscene").
struct SceneResource {
    enum class Source { CharacterMesh, CharacterKillbane, AngelFixedNames, VehicleVariant };
    Source source = Source::CharacterMesh;
    // The element text (Mesh / Killbane / Variant). Empty for AngelFixedNames:
    // the three fixed names are OPEN in the spec.
    std::string text;
    // VehicleVariant with no Variant text: what the engine appends is OPEN.
    bool textPresent = true;
};

// What 0x0073ada0 writes from one `<name>.cte_xtbl` `Cutscene` element.
struct SceneFields {
    bool cutsceneElementFound = false; // no Cutscene element: every field below OPEN

    // +0x8. nullopt = OPEN (no CutsceneType text, or a spelling that differs
    // from "Zscene"/"Story"/"Designer" only in case - comparison case rule OPEN).
    std::optional<int> kind;
    std::optional<std::string> cutsceneTypeText; // as written

    // +0xac: 1 when the scene file has a SceneLightset element (REAL).
    bool hasLightset = false;
    // +0xad: the SceneLightset text, strncpy'd into 64 bytes (REAL). Holds the
    // first min(64, length) bytes. nullopt when the element has no text (what
    // the copy reads then is OPEN).
    std::optional<std::string> lightsetText;
    // strncpy writes no terminating NUL when the text is 64 bytes or longer;
    // what the later CRC lookup of +0xad then reads is OPEN (REAL fact about
    // strncpy; the consequence is not in the spec). Every shipped lightset
    // path is longer than 64 bytes.
    bool lightsetUnterminated = false;

    // +0xf0 / +0xf4: the two halves of the Soundtrack text, split at the first
    // ':' with one character dropped on each side (REAL: "the form
    // `left : right`"). +0xf4 (right) is the soundtrack stream started at
    // promotion. The audio ids (0x00462960) are OPEN; only the names are kept.
    bool soundtrackPresent = false;
    std::optional<std::string> soundtrackLeft;   // name feeding +0xf0
    std::optional<std::string> soundtrackRight;  // name feeding +0xf4
    // true when Soundtrack is present but not of the `left : right` form (no
    // ':', nothing before it, or nothing after the dropped character): OPEN.
    bool soundtrackSplitOpen = false;
    // REAL: both ids are 0 when the element is absent. When present, the ids
    // are OPEN (0x00462960) - this is the only case they are known.
    bool soundtrackIdsKnownZero() const { return !soundtrackPresent; }

    // +0x20 list. nullopt = OPEN: the spec describes it for a story cutscene
    // (kind 2) and says it is empty for a zscene (kind 1); for kind 0
    // ("Designer") and an OPEN kind it does not say.
    std::optional<std::vector<SceneResource>> resources;
};

// 0x0073ada0 over one parsed `<name>.cte_xtbl`. Never throws for a parsed
// document; missing elements become the OPEN / absent values above.
SceneFields ParseSceneFile(const sr3xtbl::Document& cteXtbl, const ParserOptions& options = {});

// The name pass over cutscene.xtbl for one container.
struct NameFilterResult {
    std::vector<std::string> accepted;      // in document order, at most 200
    std::vector<std::string> skipped;       // rejected by the container's prefix rule
    std::vector<std::string> caseAmbiguous; // prefix rule decided only by the OPEN case rule
    size_t cutsceneElements = 0;            // `cutscene` elements seen
    size_t elementsWithoutName = 0;         // `cutscene` elements with no name text (no name to take)
    bool cappedAt200 = false;               // accepted names stopped at 200
};
constexpr size_t kMaxSceneNames = 200;      // REAL: "at most 200"
constexpr size_t kMainCapacitySpare = 12;   // REAL: capacity = accepted names + 12, capped at 200

NameFilterResult AcceptedSceneNames(const sr3xtbl::Document& cutsceneXtbl, std::string_view container);
// REAL: the "main" call's allocation size.
size_t MainCapacity(size_t acceptedNames);

struct SceneEntry {
    std::string name;  // +0x0, as written in cutscene.xtbl
    uint32_t crc = 0;  // +0x4, NameHash(name, 0) (lower-cased)
    std::string fileName; // the `<name>.cte_xtbl` that was opened (as found in the container)
    SceneFields fields;
};

struct SceneTable {
    std::vector<SceneEntry> entries;  // table order
    size_t capacity = 0;              // MainCapacity() for the "main" call
    NameFilterResult names;
    std::vector<std::string> missingSceneFiles;  // accepted names with no `<name>.cte_xtbl`: no entry (REAL)
    std::vector<std::string> unparseableSceneFiles; // the file exists but sr3xtbl rejects it: no entry (CHOSEN; the engine's behaviour is not in the spec)
    // First entry whose +0x4 equals NameHash(name) - 0x00721be0's linear scan
    // (REAL: "the lookup key used by 0x00721be0, a linear scan"). nullptr if none.
    const SceneEntry* find(std::string_view name) const;
};

// Opens `<name>.cte_xtbl` (the caller decides where to look: lua_host_run uses
// a case-insensitive name search in the container cutscene.xtbl came from,
// per spec-vpp-container.md Sec2). Returns the bytes and the file name as
// found, or nullopt when there is no such file.
struct OpenedFile {
    std::string fileName;
    std::string bytes;
};
using SceneFileOpener = std::function<std::optional<OpenedFile>(const std::string& fileName)>;

// The "main" call of 0x0073bfb0: names from cutscene.xtbl ("main" filter),
// capacity, then one entry per name whose scene file exists, in name order.
// Throws sr3xtbl::FormatError only if cutscene.xtbl itself is not XML.
SceneTable BuildMainSceneTable(std::string_view cutsceneXtblBytes, const SceneFileOpener& open,
                               const ParserOptions& options = {});

} // namespace sr3tables_cutscene

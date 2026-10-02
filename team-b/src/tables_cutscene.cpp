// sr3tables_cutscene: see include/sr3tables_cutscene/scene_table.h for the
// sources and the REAL / CHOSEN / OPEN labels.
#include "sr3tables_cutscene/scene_table.h"

#include <algorithm>
#include <cctype>

namespace sr3tables_cutscene {

namespace {

bool startsWithExact(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool startsWithIgnoringCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

// The row container: <root><Table> in every shipped file; the document root
// when there is no Table (CHOSEN fallback, no shipped file needs it).
const sr3xtbl::Node* rowParent(const sr3xtbl::Document& doc) {
    const sr3xtbl::Node* t = doc.table();
    return t ? t : doc.root();
}

// "Zscene" -> 1 (2 with 0x0153ba02), "Story" -> 2, "Designer" -> 0, any other
// text -> 2 (REAL). A spelling equal to one of the three only when case is
// ignored gets a different kind depending on the OPEN case rule: nullopt.
std::optional<int> kindFromCutsceneType(const std::string& text, const ParserOptions& options) {
    if (text == "Zscene") return options.zsceneAsStory ? kKindStory : kKindZscene;
    if (text == "Story") return kKindStory;
    if (text == "Designer") return kKindDesigner;
    for (const char* known : {"Zscene", "Story", "Designer"}) {
        if (sr3xtbl::NameEquals(text, known)) return std::nullopt;
    }
    return kKindStory;
}

} // namespace

SceneFields ParseSceneFile(const sr3xtbl::Document& cteXtbl, const ParserOptions& options) {
    SceneFields f;
    const sr3xtbl::Node* cs = sr3xtbl::FindChild(rowParent(cteXtbl), "Cutscene");
    if (!cs) return f; // every field OPEN
    f.cutsceneElementFound = true;

    // +0x8 kind.
    if (const std::string* t = sr3xtbl::ChildText(cs, "CutsceneType")) {
        f.cutsceneTypeText = *t;
        f.kind = kindFromCutsceneType(*t, options);
    }

    // +0xac / +0xad lightset: strncpy(dst, text, 64).
    if (const sr3xtbl::Node* ls = sr3xtbl::FindChild(cs, "SceneLightset")) {
        f.hasLightset = true;
        if (const std::string* t = sr3xtbl::ChildText(ls, {})) {
            f.lightsetText = t->substr(0, std::min<size_t>(t->size(), 64));
            f.lightsetUnterminated = t->size() >= 64;
        }
    }

    // +0xf0 / +0xf4 soundtrack: split at the first ':', one character dropped
    // on each side.
    if (const sr3xtbl::Node* st = sr3xtbl::FindChild(cs, "Soundtrack")) {
        f.soundtrackPresent = true;
        const std::string* t = sr3xtbl::ChildText(st, {});
        const size_t colon = t ? t->find(':') : std::string::npos;
        if (!t || colon == std::string::npos || colon == 0 || colon + 2 > t->size()) {
            f.soundtrackSplitOpen = true;
        } else {
            f.soundtrackLeft = t->substr(0, colon - 1);
            f.soundtrackRight = t->substr(colon + 2);
        }
    }

    // +0x20 resource list: described for a story cutscene, empty for a zscene.
    if (f.kind && *f.kind == kKindZscene) {
        f.resources = std::vector<SceneResource>{};
    } else if (f.kind && *f.kind == kKindStory) {
        std::vector<SceneResource> list;
        for (const sr3xtbl::Node* ch : sr3xtbl::Children(sr3xtbl::FindChild(cs, "Characters"), "Character")) {
            const std::optional<bool> angel = sr3xtbl::GetBool(ch, "IsAngel");
            if (angel && *angel) {
                list.push_back({SceneResource::Source::AngelFixedNames, std::string(), false});
                continue;
            }
            if (const std::string* m = sr3xtbl::ChildText(ch, "Mesh"))
                list.push_back({SceneResource::Source::CharacterMesh, *m, true});
            if (const std::string* k = sr3xtbl::ChildText(ch, "Killbane"))
                list.push_back({SceneResource::Source::CharacterKillbane, *k, true});
        }
        for (const sr3xtbl::Node* v : sr3xtbl::Children(sr3xtbl::FindChild(cs, "Vehicles"), "Vehicle")) {
            const std::string* var = sr3xtbl::ChildText(v, "Variant");
            list.push_back({SceneResource::Source::VehicleVariant, var ? *var : std::string(), var != nullptr});
        }
        f.resources = std::move(list);
    }
    return f;
}

NameFilterResult AcceptedSceneNames(const sr3xtbl::Document& cutsceneXtbl, std::string_view container) {
    NameFilterResult r;
    const bool mainContainer = container == "main";
    for (const sr3xtbl::Node* c : sr3xtbl::Children(rowParent(cutsceneXtbl), "cutscene")) {
        ++r.cutsceneElements;
        const std::string* name = sr3xtbl::ChildText(c, "name");
        if (!name) {
            ++r.elementsWithoutName;
            continue;
        }
        bool accept;
        bool ambiguous = false;
        if (mainContainer) {
            const bool exact = startsWithExact(*name, "dlc") || startsWithExact(*name, "patch");
            const bool loose = startsWithIgnoringCase(*name, "dlc") || startsWithIgnoringCase(*name, "patch");
            accept = !exact;
            ambiguous = !exact && loose;
        } else {
            const bool exact = startsWithExact(*name, container);
            const bool loose = startsWithIgnoringCase(*name, container);
            accept = exact;
            ambiguous = !exact && loose;
        }
        if (ambiguous) {
            r.caseAmbiguous.push_back(*name);
            continue;
        }
        if (!accept) {
            r.skipped.push_back(*name);
            continue;
        }
        if (r.accepted.size() >= kMaxSceneNames) {
            r.cappedAt200 = true;
            continue;
        }
        r.accepted.push_back(*name);
    }
    return r;
}

size_t MainCapacity(size_t acceptedNames) {
    return std::min(acceptedNames + kMainCapacitySpare, kMaxSceneNames);
}

const SceneEntry* SceneTable::find(std::string_view name) const {
    const uint32_t crc = sr3xtbl::NameHash(name, 0);
    for (const SceneEntry& e : entries) {
        if (e.crc == crc) return &e;
    }
    return nullptr;
}

SceneTable BuildMainSceneTable(std::string_view cutsceneXtblBytes, const SceneFileOpener& open,
                               const ParserOptions& options) {
    SceneTable table;
    const sr3xtbl::Document names = sr3xtbl::ParseDocument(cutsceneXtblBytes);
    table.names = AcceptedSceneNames(names, "main");
    table.capacity = MainCapacity(table.names.accepted.size());
    for (const std::string& name : table.names.accepted) {
        std::optional<OpenedFile> file = open ? open(name + ".cte_xtbl") : std::nullopt;
        if (!file) {
            table.missingSceneFiles.push_back(name);
            continue;
        }
        if (table.entries.size() >= table.capacity) break; // never reached: capacity >= accepted names
        SceneEntry e;
        e.name = name;
        e.crc = sr3xtbl::NameHash(name, 0);
        e.fileName = file->fileName;
        try {
            const sr3xtbl::Document doc = sr3xtbl::ParseDocument(file->bytes);
            e.fields = ParseSceneFile(doc, options);
        } catch (const sr3xtbl::FormatError&) {
            table.unparseableSceneFiles.push_back(file->fileName);
            continue;
        }
        table.entries.push_back(std::move(e));
    }
    return table;
}

} // namespace sr3tables_cutscene

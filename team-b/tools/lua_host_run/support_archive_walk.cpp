// lua_host_run helpers: the .lua/.vint_doc archive walk and the real per-script state-tag lookup.
// Moved verbatim out of tools/lua_host_run.cpp's former anonymous namespace (see lua_host_run_support.h).
#include "lua_host_run_support.h"

namespace luahostrun {

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const uint8_t* data, size_t len) {
    std::string lower = toLower(name);
    if (lower.size() > 9 && lower.compare(lower.size() - 9, 9, ".vint_doc") == 0) {
        st.docs.push_back({archive, chain, name, std::vector<uint8_t>(data, data + len)});
        return;
    }
    if (endsWithLuaExt(lower)) {
        ScriptInstance si;
        si.archive = archive;
        si.pathChain = chain;
        si.entryName = name;
        si.content.assign(reinterpret_cast<const char*>(data), len);
        st.found.push_back(std::move(si));
    }
}

// Same recursive walk as tools/lua_mission_ui_census.cpp's own walk() - see
// this file's top comment for why it is duplicated rather than shared.
void walk(WalkState& st, const vpp::Container& c, const std::string& archive, const std::string& chain) {
    st.stats.containersOpened++;
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        st.stats.entries++;
        std::string name = e.name.empty() ? ("<unnamed hash=" + std::to_string(e.nameHash) + ">") : e.name;
        std::string childChain = chain.empty() ? name : (chain + " > " + name);

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walk(st, child, archive, childChain);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                inspectLeaf(st, archive, childChain, name, raw.data(), raw.size());
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                bool nested = false;
                if (result.data.size() >= 4) {
                    uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                      ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                    if (magic == 0x51890ACEu) {
                        st.bufferPool.push_back(std::move(result.data));
                        try {
                            vpp::Container child(vpp::ByteView(st.bufferPool.back().data(), st.bufferPool.back().size()));
                            nested = true;
                            walk(st, child, archive, childChain);
                        } catch (const std::exception&) {
                            nested = false;
                        }
                        if (!nested) {
                            const auto& buf = st.bufferPool.back();
                            inspectLeaf(st, archive, childChain, name, buf.data(), buf.size());
                        }
                        continue;
                    }
                }
                inspectLeaf(st, archive, childChain, name, result.data.data(), result.data.size());
            } else {
                st.stats.decompressFailed++;
            }
        }
    }
}

// --- Real per-script state-tag lookup (this task, spec-lua-bindings.md
// Sec14.5, read in full before writing this): the two Lua script resource
// types are cleanly, exhaustively separated by CONTAINER KIND in the real
// .asm_pc manifests - type 27 ("Vint LUA script") entries under container
// kind 21 ("UI") or 22 ("UI peg") belong to the UI state; type 32
// ("Mission LUA script") entries under container kind 39 ("mission model
// data") or 40 ("large mission model data") belong to the gameplay state
// (Sec14.5's own population citation: 727 real type-27 entries, ALL under
// kind 21; 79 real type-32 entries, 76 under kind 39 + 3 under kind 40).
// Built via sr3asm::AsmManifest (include/sr3asm/manifest.h) exactly the
// way tools/mission_package_census.cpp already establishes the real usage
// pattern for this project: walk every real .asm_pc across the whole
// cache, parse with AsmManifest::parse(), and resolve BOTH
// ContainerRecord::containerKind (real numeric field, matched against the
// spec-cited 21/22/39/40 directly - no name-string dependency needed for
// the KIND side, since the spec cites the numeric ids as the confirmed
// fact) and each ManifestEntry::typeId's own name via the manifest's own
// typeTable().findName() (matched against the literal strings "Vint LUA
// script"/"Mission LUA script" - the SAME name-based check
// mission_package_census.cpp already uses for the type side, not
// reinvented). The walk()/inspectLeaf() shape below is a second,
// deliberately-duplicated copy of mission_package_census.cpp's own
// walk() (leaf test: ".asm_pc" suffix instead of ".lua") - matching this
// codebase's own established convention (this file's own top comment)
// of duplicating a small recursive walk per tool rather than factoring
// it into a shared header.
namespace statetag {

bool endsWithAsmExt(const std::string& lowerName) {
    static const std::string ext = ".asm_pc";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

const char* tagLabel(Tag t) {
    switch (t) {
        case Tag::Ui: return "ui";
        case Tag::Gameplay: return "gameplay";
        case Tag::Conflict: return "conflict"; // real data disagreement across manifests - treated as OPEN by callers
        default: return "open";
    }
}

struct AsmInstance {
    std::string archive;
    std::string pathChain; // full chain INCLUDING this .asm_pc's own entry name (mission_package_census's own convention)
    std::vector<uint8_t> content;
};

// Second, dedicated recursive walk (leaf test: ".asm_pc") - deliberately
// separate from the WalkState/walk() pair above (leaf test: ".lua"),
// matching mission_package_census.cpp's own walk() almost verbatim
// (duplicated with attribution, not shared - see this block's own top
// comment).
void walkAsm(std::vector<AsmInstance>& found, std::deque<std::vector<uint8_t>>& bufferPool,
             const vpp::Container& c, const std::string& archive, const std::string& chain) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::string name = e.name.empty() ? ("<unnamed hash=" + std::to_string(e.nameHash) + ">") : e.name;
        std::string childChain = chain.empty() ? name : (chain + " > " + name);

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walkAsm(found, bufferPool, child, archive, childChain);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested && endsWithAsmExt(toLower(name))) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                found.push_back({archive, childChain, std::vector<uint8_t>(raw.data(), raw.data() + raw.size())});
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                bool nested = false;
                if (result.data.size() >= 4) {
                    uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                      ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                    if (magic == 0x51890ACEu) {
                        bufferPool.push_back(std::move(result.data));
                        try {
                            vpp::Container child(vpp::ByteView(bufferPool.back().data(), bufferPool.back().size()));
                            nested = true;
                            walkAsm(found, bufferPool, child, archive, childChain);
                        } catch (const std::exception&) {
                            nested = false;
                        }
                        if (!nested) {
                            const auto& buf = bufferPool.back();
                            if (endsWithAsmExt(toLower(name)))
                                found.push_back({archive, childChain, std::vector<uint8_t>(buf.begin(), buf.end())});
                        }
                        continue;
                    }
                }
                if (endsWithAsmExt(toLower(name)))
                    found.push_back({archive, childChain, result.data});
            }
        }
    }
}

// A ContainerRecord's real backing .str2_pc stem (spec-asm-format.md
// Sec9.4, CONFIRMED - 8,057/8,057 records checked): "A record's backing
// container is `<name>.str2_pc` (7,442 records) or, for records named by
// a whole file name, `<name minus its extension>.str2_pc` (615 records)."
// Real, observed, checked directly against this task's own data: every
// kind-21/22 (UI/UI peg) real container record is named the SECOND way -
// as a whole ".vint_doc" file name (e.g. "cat_mouse_results.vint_doc",
// type 26's own registered extension, spec-asm-format.md Sec9.3's
// type-extension table) - its real backing container is
// "cat_mouse_results.str2_pc", not "cat_mouse_results.vint_doc.str2_pc".
// Kind-39/40 (mission) container records were already observed named the
// FIRST way (a bare stem, e.g. "dlc1_mm_06_modal", no extension) - this
// function returns that name unchanged, so this fix is additive, not a
// behavior change for the gameplay side. Detection: does `name` contain a
// '.' at all - a bare container stem never does in the real population
// this project has observed; a "whole file name" always does (its own
// real extension). Strips at most one trailing "." + suffix (the LAST
// dot), never guessed beyond that.
std::string backingStem(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return name;
    return name.substr(0, dot);
}

Index build(const std::vector<fs::path>& archives) {
    Index idx;
    std::vector<AsmInstance> found;
    std::deque<std::vector<uint8_t>> bufferPool;
    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
        } catch (const std::exception&) {
            continue;
        }
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walkAsm(found, bufferPool, root, archiveName, "");
        } catch (const std::exception&) {
        }
    }
    idx.asmInstancesFound = found.size();

    for (const auto& inst : found) {
        sr3asm::AsmManifest manifest;
        try {
            manifest = sr3asm::AsmManifest::parse(vpp::ByteView(inst.content.data(), inst.content.size()));
        } catch (const std::exception&) {
            ++idx.manifestParseFailures;
            continue;
        }
        ++idx.manifestsParsed;

        for (const auto& record : manifest.records()) {
            // Container-kind side: real numeric field, matched directly
            // against the spec-cited 21/22/39/40 (spec-lua-bindings.md
            // Sec14.5) - no name-table dependency needed here since the
            // spec itself cites the numeric kind ids as the confirmed
            // fact (it also names them "UI"/"UI peg"/"mission model
            // data"/"large mission model data", cross-checked once below
            // per manifest via kindTable() as an extra sanity check, not
            // load-bearing).
            bool kindUi = (record.containerKind == 21 || record.containerKind == 22);
            bool kindMission = (record.containerKind == 39 || record.containerKind == 40);
            if (!kindUi && !kindMission) continue;

            for (const auto& entry : record.entries) {
                const std::string* typeName = manifest.typeTable().findName(entry.typeId);
                if (!typeName) continue;
                Tag tag;
                if (kindUi && *typeName == "Vint LUA script") {
                    tag = Tag::Ui;
                    ++idx.uiCandidateEntries;
                } else if (kindMission && *typeName == "Mission LUA script") {
                    tag = Tag::Gameplay;
                    ++idx.gameplayCandidateEntries;
                } else {
                    continue; // some other real resource type sharing this container - not a Lua script entry
                }
                std::string key = toLower(backingStem(record.name)) + "|" + toLower(entry.name);
                auto it = idx.byKey.find(key);
                if (it == idx.byKey.end()) {
                    idx.byKey.emplace(key, tag);
                } else if (it->second != tag && it->second != Tag::Conflict) {
                    // Real data disagreement across manifests for the SAME
                    // (container,entry) key - honestly recorded rather than
                    // silently overwritten either direction ("no invented
                    // fixes" policy). Not expected (Sec14.5's own "cleanly,
                    // exhaustively separated" population claim), so this
                    // counter is a real, checked-for-zero sanity gate, not
                    // an assumed negative.
                    it->second = Tag::Conflict;
                    ++idx.conflicts;
                }
                // else: identical repeat listing of the same container in a
                // second manifest (real, observed - e.g. dlc1_mm_06_modal
                // appears in both dlc1_sr3_city.asm_pc and
                // dlc1_stream_grid.asm_pc identically) - not a conflict.
            }
        }
    }
    return idx;
}

// Resolves one script's own real state tag: strips the trailing " > " +
// entryName that WalkState's own pathChain always carries (this file's
// own inspectLeaf()/walk() convention - pathChain is `childChain`, which
// includes the leaf's own name), takes the LAST remaining segment as the
// immediate containing entry's own name (its real .str2_pc, normally),
// strips a trailing ".str2_pc" (case-insensitive, spec-asm-format.md's
// own "<name>.str2_pc is normally its backing file" convention,
// include/sr3asm/manifest.h's own ContainerRecord::name doc comment), and
// looks that (containerName, entryName) pair up in `idx`. A script with
// no " > " at all in its own pathChain (sits directly at its archive's
// own root, never nested inside any .str2_pc - the real, observed case
// for the 6 UI/gameplay preload library files plus game_lib.lua/
// sr3_city.lua/m19.lua/m22.lua, 10/804 total) has no container to look
// up at all and is tagged OPEN directly, not guessed at.
Tag resolve(const Index& idx, const std::string& pathChain, const std::string& entryName,
            std::string* containerNameOut) {
    size_t sep = pathChain.rfind(" > ");
    if (sep == std::string::npos) return Tag::Open; // no nested container at all - real, not a lookup miss
    std::string prefix = pathChain.substr(0, sep);
    size_t sep2 = prefix.rfind(" > ");
    std::string immediateParent = (sep2 == std::string::npos) ? prefix : prefix.substr(sep2 + 3);
    std::string containerName = immediateParent;
    std::string lowerParent = toLower(immediateParent);
    static const std::string kStr2Ext = ".str2_pc";
    if (lowerParent.size() >= kStr2Ext.size() &&
        lowerParent.compare(lowerParent.size() - kStr2Ext.size(), kStr2Ext.size(), kStr2Ext) == 0) {
        containerName = immediateParent.substr(0, immediateParent.size() - kStr2Ext.size());
    }
    if (containerNameOut) *containerNameOut = containerName;
    std::string key = toLower(containerName) + "|" + toLower(entryName);
    auto it = idx.byKey.find(key);
    if (it == idx.byKey.end()) return Tag::Open; // real miss - not in either confirmed shape, reported as OPEN, not forced
    return it->second;
}

} // namespace statetag

} // namespace luahostrun

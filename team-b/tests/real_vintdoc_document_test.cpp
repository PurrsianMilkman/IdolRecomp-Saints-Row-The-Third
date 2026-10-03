// Real-data test for sr3vintdoc::parseDocument and the Lua host's real
// document loading (2026-10-03). Reads the shipped archive
// interface_startup.vpp_pc from CRREISH_GAME_CACHE_DIR (a CMake cache
// variable, default the owner's install path) and exits 77 - reported by
// ctest as SKIPPED, never as a pass - when it is not there (CI, other
// machines). Nothing from the archive is copied into the repository; the
// expected values below were read by hand from the files' own bytes (a
// throwaway hex/Python walk, independent of this C++ parser) and are
// small identifiers/numbers only.
//
// What it pins:
//  * population: every one of the 159 .vint_doc entries parses and lands
//    exactly (tree ends at header +0x16, string pool ends at EOF), 5,326
//    records, every type a registered name;
//  * vdo_grid_button.vint_doc (237 bytes): the whole document - 1 element `grid_icon` (bitmap, no children) with its
//    5 properties;
//  * bg_saints.vint_doc (7,257 bytes): the header counts against the
//    parsed tree (3 elements + 6 animations, 69 records in all), the
//    screen_grp > bg > crib_bg_grp > bg_tile (36 children) nesting, a
//    640x480 override, signed tag-1 values;
//  * the same document loaded into EngineState and queried from Lua through
//    the real registered stubs (vint_object_find / first_child / clone /
//    get_property / set_property) in its own script's document context.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sr3luahost/host.h"
#include "sr3save/save_crc.h"
#include "sr3vintdoc/vint_doc.h"
#include "vpp/container.h"

using namespace sr3vintdoc;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

struct Entry {
    std::string chain;
    std::string name;
    std::vector<uint8_t> bytes;
};

void collect(const vpp::Container& c, const std::string& chain, std::vector<Entry>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        std::string childChain = chain.empty() ? e.name : chain + " > " + e.name;
        bool isDoc = e.name.size() > 9 && e.name.compare(e.name.size() - 9, 9, ".vint_doc") == 0;
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                if (isDoc) {
                    vpp::ByteView r = c.rawEntryBytes(i);
                    out.push_back({childChain, e.name, std::vector<uint8_t>(r.data(), r.data() + r.size())});
                } else {
                    try {
                        collect(c.openNested(i), childChain, out);
                    } catch (const std::exception&) {
                    }
                }
            } else if (isDoc) {
                auto r = c.decompressEntry(i);
                if (r.status == vpp::DecodeStatus::Ok) out.push_back({childChain, e.name, std::move(r.data)});
            } else if (e.name.size() > 8 && e.name.compare(e.name.size() - 8, 8, ".str2_pc") == 0) {
                auto r = c.decompressEntry(i);
                if (r.status == vpp::DecodeStatus::Ok) {
                    std::vector<uint8_t> buf = std::move(r.data); // kept alive for the nested walk only
                    collect(vpp::Container(vpp::ByteView(buf.data(), buf.size())), childChain, out);
                }
            }
        } catch (const std::exception&) {
        }
    }
}

const ElementNode* child(const ElementNode& n, const std::string& name) {
    for (const auto& c : n.children) if (c.name == name) return &c;
    return nullptr;
}
const Property* prop(const PropertyList& l, const char* name) {
    for (const auto& p : l.properties) if (p.nameHash == sr3save::nameHash(name)) return &p;
    return nullptr;
}
const Property* propHash(const PropertyList& l, uint32_t h) {
    for (const auto& p : l.properties) if (p.nameHash == h) return &p;
    return nullptr;
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

} // namespace

int main() {
    fs::path archive = fs::path(CRREISH_GAME_CACHE_DIR) / "interface_startup.vpp_pc";
    std::ifstream f(archive, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cout << "SKIPPED: " << archive.string() << " not found (real-data test; set CRREISH_GAME_CACHE_DIR)\n";
        return 77;
    }
    std::vector<uint8_t> ab(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(ab.data()), static_cast<std::streamsize>(ab.size()));
    vpp::Container root(vpp::ByteView(ab.data(), ab.size()));
    std::vector<Entry> docs;
    collect(root, "", docs);

    // --- population ---------------------------------------------------------
    CHECK(docs.size() == 159);
    size_t landed = 0, records = 0, registered = 0;
    const Entry* gridButton = nullptr;
    const Entry* bgSaints = nullptr;
    std::function<void(const ElementNode&)> count = [&](const ElementNode& n) {
        ++records;
        registered += isRegisteredElementType(n.type);
        for (const auto& c : n.children) count(c);
    };
    for (const auto& e : docs) {
        try {
            Document d = parseDocument(vpp::ByteView(e.bytes.data(), e.bytes.size()));
            landed += d.landsExactly();
            for (const auto& n : d.elements) count(n);
            for (const auto& n : d.animations) count(n);
        } catch (const std::exception& ex) {
            std::cerr << "parse failed: " << e.chain << ": " << ex.what() << "\n";
        }
        if (e.name == "vdo_grid_button.vint_doc" && !gridButton) gridButton = &e;
        if (e.name == "bg_saints.vint_doc" && !bgSaints) bgSaints = &e;
    }
    std::cout << "interface_startup.vpp_pc: " << docs.size() << " documents, " << landed << " land exactly, " << records
              << " records, " << registered << " with a registered type\n";
    CHECK(landed == 159);
    CHECK(records == 5326);
    CHECK(registered == 5326);

    // --- vdo_grid_button.vint_doc, the whole document ----------------------
    CHECK(gridButton != nullptr);
    if (gridButton) {
        CHECK(gridButton->bytes.size() == 237);
        std::cout << "vdo_grid_button.vint_doc found at: " << gridButton->chain << "\n";
        Document d = parseDocument(vpp::ByteView(gridButton->bytes.data(), gridButton->bytes.size()));
        CHECK(d.header.version == 2 && d.header.elementCount == 1 && d.header.animationCount == 0);
        CHECK(d.header.metadataCount == 1 && d.header.criticalResourceCount == 0);
        CHECK(d.header.secondaryOffsetRaw == 0x74 && d.treeEnd == 0x74);
        CHECK(d.strings.strings.size() == 7 && d.strings.endsAtEof);
        CHECK(d.metadataValue("lua_script_file") && *d.metadataValue("lua_script_file") == "vdo_grid_button");
        CHECK(d.totalRecordCount() == 1);
        const ElementNode& e = d.elements[0];
        CHECK(e.name == "grid_icon" && e.type == "bitmap" && e.children.empty() && e.overrides.empty());
        CHECK(e.fileOffset == 0x26);
        CHECK(e.baseline.properties.size() == 5);
        const Property* image = prop(e.baseline, "image");
        CHECK(image && image->tag == 4 && d.strings.strings[image->rawU32()] == "ui_blank");
        const Property* sourceSe = prop(e.baseline, "source_se");
        CHECK(sourceSe && sourceSe->tag == 7 && sourceSe->f32(0) == 16.0f && sourceSe->f32(1) == 16.0f);
        const Property* autoOffset = prop(e.baseline, "auto_offset");
        CHECK(autoOffset && autoOffset->tag == 4 && d.strings.strings[autoOffset->rawU32()] == "c");
        const Property* offset = prop(e.baseline, "offset");
        CHECK(offset && offset->tag == 7 && offset->f32(0) == -8.0f && offset->f32(1) == -8.0f);
        const Property* tint = prop(e.baseline, "tint");
        CHECK(tint && tint->tag == 6 && tint->f32(0) == 1.0f && tint->f32(1) == 1.0f && tint->f32(2) == 1.0f);
    }

    // --- bg_saints.vint_doc, header counts vs the parsed tree ------------------
    CHECK(bgSaints != nullptr);
    if (!bgSaints) return 1;
    CHECK(bgSaints->bytes.size() == 7257);
    Document bg = parseDocument(vpp::ByteView(bgSaints->bytes.data(), bgSaints->bytes.size()));
    {
        CHECK(bg.landsExactly() && bg.header.secondaryOffsetRaw == 5709);
        CHECK(bg.header.elementCount == 3 && bg.elements.size() == 3);
        CHECK(bg.header.animationCount == 6 && bg.animations.size() == 6);
        CHECK(bg.totalRecordCount() == 69);
        CHECK(bg.strings.strings.size() == 89);
        CHECK(bg.metadataValue("lua_script_file") && *bg.metadataValue("lua_script_file") == "bg_saints");
        CHECK(bg.metadataValue("document_depth") && *bg.metadataValue("document_depth") == "0");
        CHECK(bg.elements[0].name == "background_bmp" && bg.elements[0].type == "bitmap");
        const Property* depth = prop(bg.elements[0].baseline, "depth");
        CHECK(depth && depth->tag == 1 && static_cast<int32_t>(depth->rawU32()) == 500);
        const Property* alpha = prop(bg.elements[0].baseline, "alpha");
        CHECK(alpha && alpha->tag == 3 && alpha->f32(0) == 0.5f);

        const ElementNode& video = bg.elements[1];
        CHECK(video.name == "bg_video" && video.type == "video");
        CHECK(video.overrides.size() == 1 && video.overrides[0].resolutionName == "640x480");
        const Property* sdAnchor = prop(video.overrides[0].list, "anchor");
        CHECK(sdAnchor && sdAnchor->f32(0) == -106.0f && sdAnchor->f32(1) == 0.0f);
        CHECK(prop(video.baseline, "anchor") == nullptr);
        const Property* frameEvent = prop(video.baseline, "frame_event_num");
        CHECK(frameEvent && frameEvent->tag == 1 && static_cast<int32_t>(frameEvent->rawU32()) == -1);
        auto sdProps = video.effectiveProperties("640x480");
        auto hdProps = video.effectiveProperties("");
        CHECK(hdProps.size() == video.baseline.properties.size());
        CHECK(sdProps.size() == video.baseline.properties.size() + 1); // anchor added; scale overridden, not duplicated

        const ElementNode& screen = bg.elements[2];
        CHECK(screen.name == "screen_grp" && screen.type == "group" && screen.children.size() == 2);
        const ElementNode* bgGroup = child(screen, "bg");
        CHECK(bgGroup && bgGroup->children.size() == 2);
        const ElementNode* crib = bgGroup ? child(*bgGroup, "crib_bg_grp") : nullptr;
        CHECK(crib && crib->children.size() == 3);
        const ElementNode* tile = crib ? child(*crib, "bg_tile") : nullptr;
        CHECK(tile && tile->type == "group" && tile->children.size() == 36);
        CHECK(tile && tile->children[0].name == "bg_2_1_1" && tile->children[35].name == "st_bg");
        const Property* tileAnchor = tile ? prop(tile->baseline, "anchor") : nullptr;
        CHECK(tileAnchor && tileAnchor->f32(0) == -184.0f && tileAnchor->f32(1) == 41.0f);
        const Property* rotation = tile ? prop(tile->baseline, "rotation") : nullptr;
        CHECK(rotation && near(rotation->f32(0), -0.2617994f));
        const ElementNode* shadows = child(screen, "shadow_group");
        CHECK(shadows && shadows->children.size() == 2);

        std::vector<std::pair<std::string, size_t>> anims = {
            {"crib_bg_loop_anim", 1}, {"mask_drop_anim", 4},      {"mask_morph_anim", 4},
            {"mask_slide_in_anim", 2}, {"mask_slide_out_anim", 2}, {"mask_stronghold_out_anim", 2}};
        for (size_t i = 0; i < anims.size(); ++i) {
            CHECK(bg.animations[i].name == anims[i].first && bg.animations[i].type == "animation");
            CHECK(bg.animations[i].children.size() == anims[i].second);
            for (const auto& t : bg.animations[i].children) CHECK(t.type == "tween");
        }
        const ElementNode& tween = bg.animations[0].children[0];
        CHECK(tween.name == "crib_bg_grp_1_twn");
        const Property* target = prop(tween.baseline, "target_name");
        CHECK(target && bg.strings.strings[target->rawU32()] == "bg_tile");
        const Property* targetProp = prop(tween.baseline, "target_property");
        CHECK(targetProp && bg.strings.strings[targetProp->rawU32()] == "anchor");
    }

    // --- loaded into the Lua host, queried through the real stubs ------------
    {
        std::vector<sr3luahost::RegisteredName> names = {
            {"vint_object_find", "ui"}, {"vint_object_first_child", "ui"}, {"vint_object_clone", "ui"},
            {"vint_set_property", "ui"}, {"vint_get_property", "ui"}};
        sr3luahost::Host host(names);
        std::optional<uint32_t> docHandle;
        host.setDocumentContextResolver([&](const std::string& chunk) -> std::optional<uint32_t> {
            if (chunk != "bg_saints.lua") return std::nullopt;
            if (!docHandle) docHandle = host.engineState().loadVintDocument("bg_saints.vint_doc", bg);
            return docHandle;
        });
        auto r = host.runChunk(host.uiState(),
            "local tile = vint_object_find('bg_tile')\n"
            "assert(tile ~= 0, 'bg_tile')\n"
            "local x, y = vint_get_property(tile, 'anchor')\n"
            "assert(x == -184 and y == 41, 'bg_tile anchor')\n"
            "local first = vint_object_first_child(tile)\n"
            "assert(first == vint_object_find('bg_2_1_1', tile), 'first child')\n"
            "local img = vint_get_property(first, 'image')\n"
            "assert(img == 'ui_sr3_bg_tile', 'image')\n"
            "local crib = vint_object_find('crib_bg_grp')\n"
            "assert(vint_object_find('bg_tile', crib) == tile, 'parent-scoped')\n"
            "local video = vint_object_find('bg_video')\n"
            "assert(vint_get_property(video, 'frame_event_num') == -1, 'signed')\n"
            "assert(vint_get_property(video, 'visible') == false, 'bool')\n"
            "assert(select('#', vint_get_property(video, 'anchor')) == 0, 'no file anchor at the baseline')\n"
            "local sx, sy = vint_get_property(video, 'scale')\n"
            "assert(sx == 1280 and sy == 720, 'baseline scale')\n"
            "assert(vint_get_property(vint_object_find('background_bmp'), 'depth') == 500, 'depth')\n"
            "local twn = vint_object_find('crib_bg_grp_1_twn')\n"
            "assert(vint_get_property(twn, 'target_name') == 'bg_tile', 'tween target')\n"
            "assert(vint_object_find('mask_drop_anim') ~= 0, 'animation root')\n"
            "assert(vint_object_clone(first) ~= 0, 'clone')\n"
            "vint_set_property(tile, 'anchor', 1, 2)\n"
            "local x2, y2 = vint_get_property(tile, 'anchor')\n"
            "assert(x2 == 1 and y2 == 2, 'set')\n",
            "bg_saints.lua");
        CHECK(r.loadOk && r.pcallOk);
        if (!r.pcallOk) std::cerr << r.pcallError << "\n";
        const auto* ld = docHandle ? host.engineState().loadedVintDocument(*docHandle) : nullptr;
        CHECK(ld && ld->objectCount == 69 && ld->elementHandles.size() == 3 && ld->animationHandles.size() == 6);
    }

    if (g_failures == 0) std::cout << "ALL real .vint_doc DOCUMENT TESTS PASSED\n";
    return g_failures == 0 ? 0 : 1;
}

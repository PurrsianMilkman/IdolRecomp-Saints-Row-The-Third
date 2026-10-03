// Synthetic tests for the full-document walk (sr3vintdoc::parseDocument) and
// its wiring into the Lua host's Vint object model
// (EngineState::loadVintDocument + Host::setDocumentContextResolver),
// 2026-10-03. The fixture is laid out byte by byte from the layout in
// spec-vint-doc-format.md as synced at main 67455c3 (CONFIRMED by
// disassembly): header, critical resources and metadata from 0x1E, the
// element tree, then the string table at header +0x16 as the file's last
// section; element record = name index, type index, u16 child count, one
// skipped byte, then the property block inline (u32 baseline offset, u8
// pair count, 8-byte pairs, file-absolute offsets, override lists then the
// baseline list). No game data. The real-data counterpart is
// tests/real_vintdoc_document_test.cpp.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sr3luahost/host.h"
#include "sr3save/save_crc.h"
#include "sr3vintdoc/vint_doc.h"

using namespace sr3vintdoc;
using sr3luahost::EngineState;
using sr3luahost::HookGroup;
using sr3luahost::HookSpec;
using sr3luahost::Host;
using sr3luahost::RegisteredName;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

template <typename F>
bool throwsFormatError(F f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

struct Buf {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) u8((v >> (8 * i)) & 0xFF); }
    void f32(float f) { uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
    void patch32(size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = (v >> (8 * i)) & 0xFF; }
    vpp::ByteView view() const { return vpp::ByteView(b.data(), b.size()); }
};

// One property record (tag, name hash, value) - the value already encoded.
struct P {
    uint8_t tag;
    uint32_t hash;
    std::vector<uint8_t> value;
};
P pInt(const char* name, int32_t v) { Buf w; w.u32(static_cast<uint32_t>(v)); return {1, sr3save::nameHash(name), w.b}; }
P pUns(const char* name, uint32_t v) { Buf w; w.u32(v); return {2, sr3save::nameHash(name), w.b}; }
P pFloat(const char* name, float v) { Buf w; w.f32(v); return {3, sr3save::nameHash(name), w.b}; }
P pStr(const char* name, uint32_t idx) { Buf w; w.u32(idx); return {4, sr3save::nameHash(name), w.b}; }
P pBool(const char* name, bool v) { return {5, sr3save::nameHash(name), {static_cast<uint8_t>(v ? 1 : 0)}}; }
P pVec3(const char* name, float a, float b, float c) { Buf w; w.f32(a); w.f32(b); w.f32(c); return {6, sr3save::nameHash(name), w.b}; }
P pVec2(const char* name, float a, float b) { Buf w; w.f32(a); w.f32(b); return {7, sr3save::nameHash(name), w.b}; }

size_t listSize(const std::vector<P>& l) {
    size_t n = 1;
    for (const auto& p : l) n += 1 + 4 + p.value.size();
    return n;
}
void writeList(Buf& w, const std::vector<P>& l) {
    for (const auto& p : l) {
        w.u8(p.tag);
        w.u32(p.hash);
        for (uint8_t x : p.value) w.u8(x);
    }
    w.u8(0);
}

// Element record head + property block, lists inline in the shipped order
// (overrides first, baseline last).
void writeElement(Buf& w, uint32_t nameIdx, uint32_t typeIdx, uint16_t children, uint8_t rawByte,
                  const std::vector<std::pair<uint32_t, std::vector<P>>>& overrides, const std::vector<P>& baseline) {
    w.u32(nameIdx);
    w.u32(typeIdx);
    w.u16(children);
    w.u8(rawByte);
    size_t afterTable = w.b.size() + 4 + 1 + 8 * overrides.size();
    size_t at = afterTable;
    std::vector<uint32_t> ovOffsets;
    for (const auto& o : overrides) { ovOffsets.push_back(static_cast<uint32_t>(at)); at += listSize(o.second); }
    w.u32(static_cast<uint32_t>(at)); // baseline offset, file-absolute
    w.u8(static_cast<uint8_t>(overrides.size()));
    for (size_t i = 0; i < overrides.size(); ++i) { w.u32(overrides[i].first); w.u32(ovOffsets[i]); }
    for (const auto& o : overrides) writeList(w, o.second);
    writeList(w, baseline);
}

const std::vector<std::string> kStrings = {
    "synth.vint_doc", "ui_peg", "lua_script_file", "synth.lua", "root", "group", "640x480", "kid_a",
    "bitmap", "ui_blank", "kid_b", "text", "anim", "animation", "twn", "tween",
};

// The fixture: 1 critical resource, 1 metadata entry, 1 element ("root",
// group, 2 children, one 640x480 override) and 1 animation ("anim", 1 tween
// child).
std::vector<uint8_t> buildFixture() {
    Buf w;
    w.u32(0x00003027);  // +0x00 magic
    w.u32(0);           // +0x04
    w.u16(2);           // +0x08 version 2
    w.u32(0);           // +0x0A
    w.u32(1);           // +0x0E metadata count
    w.u32(1);           // +0x12 critical-resource count
    w.u32(0);           // +0x16 string table offset, patched below
    w.u16(1);           // +0x1A elements
    w.u16(1);           // +0x1C animations
    // critical resource: selector 0, string index 1, v2 autoload byte 1
    w.u8(0); w.u32(1); w.u8(1);
    // metadata: lua_script_file = synth.lua
    w.u32(2); w.u32(3);
    writeElement(w, 4, 5, 2, 0, {{6, {pVec2("anchor", 5, 6)}}},
                 {pVec2("anchor", 1, 2), pInt("depth", -3), pFloat("alpha", 0.5f)});
    writeElement(w, 7, 8, 0, 1, {}, {pStr("image", 9), pBool("visible", true)});
    writeElement(w, 10, 11, 0, 0, {}, {pUns("leading", 0xFFFFFFF0u), pVec3("tint", 0.25f, 0.5f, 0.75f)});
    writeElement(w, 12, 13, 1, 0, {}, {});
    writeElement(w, 14, 15, 0, 0, {}, {pStr("target_name", 7)});
    w.patch32(0x16, static_cast<uint32_t>(w.b.size()));
    // string table: count, pool size, offsets, pool
    uint32_t pool = 0;
    std::vector<uint32_t> offs;
    for (const auto& s : kStrings) { offs.push_back(pool); pool += static_cast<uint32_t>(s.size()) + 1; }
    w.u32(static_cast<uint32_t>(kStrings.size()));
    w.u32(pool);
    for (uint32_t o : offs) w.u32(o);
    for (const auto& s : kStrings) { for (char c : s) w.u8(static_cast<uint8_t>(c)); w.u8(0); }
    return w.b;
}

const Property* findProp(const std::vector<Property>& v, const char* name) {
    for (const auto& p : v) if (p.nameHash == sr3save::nameHash(name)) return &p;
    return nullptr;
}

std::vector<RegisteredName> vintNames() {
    return {{"vint_object_find", "ui"}, {"vint_object_first_child", "ui"}, {"vint_object_clone", "ui"},
            {"vint_set_property", "ui"}, {"vint_get_property", "ui"}};
}

} // namespace

int main() {
    // --- the engine name hash matches real on-disk hashes ----------------
    // (measured on real files: "anchor" is stored as 0xd693b0de, "tint" as
    // 0x0f4d3468, "image" as 0x031ff342 - tests/real_vintdoc_document_test.cpp)
    CHECK(sr3save::nameHash("anchor") == 0xd693b0deu);
    CHECK(sr3save::nameHash("tint") == 0x0f4d3468u);
    CHECK(sr3save::nameHash("image") == 0x031ff342u);

    const std::vector<uint8_t> bytes = buildFixture();
    vpp::ByteView v(bytes.data(), bytes.size());

    // --- parseDocument ---------------------------------------------------
    {
        Document d = parseDocument(v);
        CHECK(d.landsExactly());
        CHECK(d.treeEnd == d.header.secondaryOffsetRaw);
        CHECK(d.strings.strings.size() == kStrings.size());
        CHECK(d.strings.strings[9] == "ui_blank");
        CHECK(d.criticalResources.size() == 1);
        CHECK(d.criticalResources[0].selectorRaw == 0 && d.criticalResources[0].valueRaw == 1);
        CHECK(d.criticalResources[0].hasVersion2Byte && d.criticalResources[0].version2ByteRaw == 1);
        CHECK(d.metadataValue("lua_script_file") && *d.metadataValue("lua_script_file") == "synth.lua");
        CHECK(d.metadataValue("document_depth") == nullptr);
        CHECK(d.elements.size() == 1 && d.animations.size() == 1);
        CHECK(d.totalRecordCount() == 5);
        const ElementNode& root = d.elements[0];
        CHECK(root.name == "root" && root.type == "group");
        CHECK(root.children.size() == 2);
        CHECK(root.children[0].name == "kid_a" && root.children[0].type == "bitmap" && root.children[0].rawByte == 1);
        CHECK(root.children[1].name == "kid_b" && root.children[1].type == "text");
        CHECK(root.listsInlineInOrder && root.children[0].listsInlineInOrder);
        CHECK(root.overrides.size() == 1 && root.overrides[0].resolutionName == "640x480");
        CHECK(root.baseline.properties.size() == 3);
        CHECK(d.animations[0].name == "anim" && d.animations[0].type == "animation");
        CHECK(d.animations[0].children.size() == 1 && d.animations[0].children[0].type == "tween");

        // Sec5 selection: baseline only without a matching resolution...
        auto base = root.effectiveProperties("");
        CHECK(base.size() == 3);
        CHECK(findProp(base, "anchor") && findProp(base, "anchor")->f32(0) == 1.0f && findProp(base, "anchor")->f32(1) == 2.0f);
        CHECK(root.effectiveProperties("1280x720").size() == 3);
        // ...and override-then-baseline, the overridden baseline record skipped.
        auto sd = root.effectiveProperties("640x480");
        CHECK(sd.size() == 3);
        CHECK(findProp(sd, "anchor") && findProp(sd, "anchor")->f32(0) == 5.0f && findProp(sd, "anchor")->f32(1) == 6.0f);
        CHECK(findProp(sd, "depth") && static_cast<int32_t>(findProp(sd, "depth")->rawU32()) == -3);
    }

    // --- structural refusals -----------------------------------------------
    {
        // string count 0: the loader fails the whole document (Sec3.1)
        std::vector<uint8_t> b = bytes;
        uint32_t s16 = vpp::ByteView(b.data(), b.size()).readU32LE(0x16);
        b[s16] = b[s16 + 1] = b[s16 + 2] = b[s16 + 3] = 0;
        CHECK(throwsFormatError([&] { parseDocument(vpp::ByteView(b.data(), b.size())); }));
    }
    {
        // string table past EOF
        std::vector<uint8_t> b(bytes.begin(), bytes.end() - 1);
        CHECK(throwsFormatError([&] { parseDocument(vpp::ByteView(b.data(), b.size())); }));
    }
    {
        // a tag outside 1-7 in the first element's override list (first byte after the 13-byte table)
        std::vector<uint8_t> b = bytes;
        size_t firstList = 0x1E + 6 + 8 + 11 + 13;
        CHECK(b[firstList] == 7);
        b[firstList] = 9;
        CHECK(throwsFormatError([&] { parseDocument(vpp::ByteView(b.data(), b.size())); }));
    }
    {
        // a type index outside the string table
        std::vector<uint8_t> b = bytes;
        size_t typeField = 0x1E + 6 + 8 + 4;
        b[typeField] = 0xEE;
        CHECK(throwsFormatError([&] { parseDocument(vpp::ByteView(b.data(), b.size())); }));
    }
    {
        // a baseline offset outside the file
        std::vector<uint8_t> b = bytes;
        size_t baseField = 0x1E + 6 + 8 + 11;
        b[baseField + 3] = 0x7F;
        CHECK(throwsFormatError([&] { parseDocument(vpp::ByteView(b.data(), b.size())); }));
    }
    {
        // a property list stored out of line still parses (the loader just
        // jumps to it) and is reported by the diagnostic, not refused: point
        // root's override at the baseline list - both offsets then name the
        // same list, and the old override bytes are simply jumped over.
        std::vector<uint8_t> b = bytes;
        size_t pairOffsetField = 0x1E + 6 + 8 + 11 + 4 + 1 + 4;
        uint32_t baseOff = vpp::ByteView(b.data(), b.size()).readU32LE(0x1E + 6 + 8 + 11);
        for (int i = 0; i < 4; ++i) b[pairOffsetField + i] = (baseOff >> (8 * i)) & 0xFF;
        Document d = parseDocument(vpp::ByteView(b.data(), b.size()));
        CHECK(!d.elements[0].listsInlineInOrder);
        CHECK(d.elements[0].overrides[0].list.properties.size() == 3);
        CHECK(d.treeEndsAtStringTable()); // the cursor still resumes after the baseline list
    }

    // --- EngineState::loadVintDocument -----------------------------------
    Document doc = parseDocument(v);
    {
        EngineState es;
        uint32_t dh = es.loadVintDocument("synth.vint_doc", doc);
        const auto* ld = es.loadedVintDocument(dh);
        CHECK(ld && ld->luaScriptFile == "synth.lua" && ld->objectCount == 5);
        CHECK(ld && ld->elementHandles.size() == 1 && ld->animationHandles.size() == 1);
        CHECK(ld && ld->propertyCount == 3 + 2 + 2 + 1);
        CHECK(es.loadedVintDocument(dh + 1) == nullptr);
        uint32_t root = ld->elementHandles[0];
        const auto* ro = es.vdoObjectForTesting(root);
        CHECK(ro && ro->name == "root" && ro->typeName == "group" && ro->parentHandle == 0 && ro->docHandle == dh);
        uint32_t kidA = es.vdoObjectFirstChild(root);
        const auto* ka = es.vdoObjectForTesting(kidA);
        CHECK(ka && ka->name == "kid_a" && ka->parentHandle == root && ka->nextSiblingHandle != 0);
        const auto* kb = es.vdoObjectForTesting(ka->nextSiblingHandle);
        CHECK(kb && kb->name == "kid_b" && kb->nextSiblingHandle == 0 && es.vdoObjectFirstChild(ka->nextSiblingHandle) == 0);
        CHECK(es.findVdoObject("kid_b", true, root, false, 0) == ka->nextSiblingHandle);
        CHECK(es.findVdoObject("kid_b", false, 0, true, dh) == ka->nextSiblingHandle);
        CHECK(es.findVdoObject("twn", false, 0, true, dh) != 0);
        CHECK(es.findVdoObject("nope", false, 0, false, 0) == 0); // no such name anywhere: the OPEN document is not read
        const auto* depth = es.findVintProperty(root, "depth");
        CHECK(depth && depth->size() == 1 && (*depth)[0].number == -3.0);
        // A second load is a separate document with its own objects.
        uint32_t dh2 = es.loadVintDocument("synth_copy.vint_doc", doc, "640x480");
        CHECK(dh2 != dh);
        uint32_t root2 = es.findVdoObject("root", false, 0, true, dh2);
        CHECK(root2 != 0 && root2 != root);
        const auto* anchor2 = es.findVintProperty(root2, "anchor");
        CHECK(anchor2 && anchor2->size() == 2 && (*anchor2)[0].number == 5.0); // override applied
    }

    // --- through Lua, with the per-chunk document context ----------------
    {
        Host host(vintNames());
        lua_State* ui = host.uiState();
        std::optional<uint32_t> docHandle;
        host.setDocumentContextResolver([&](const std::string& chunk) -> std::optional<uint32_t> {
            if (chunk == "synth.lua") {
                if (!docHandle) docHandle = host.engineState().loadVintDocument("synth.vint_doc", doc);
                return docHandle;
            }
            return std::nullopt;
        });
        auto r = host.runChunk(ui,
            "local root = vint_object_find('root')\n"
            "assert(root ~= 0, 'root')\n"
            "local a = vint_object_first_child(root)\n"
            "assert(a ~= nil and a == vint_object_find('kid_a', root), 'first child')\n"
            "local x, y = vint_get_property(root, 'anchor')\n"
            "assert(x == 1 and y == 2, 'anchor')\n"
            "assert(vint_get_property(root, 'depth') == -3, 'depth')\n"
            "assert(vint_get_property(root, 'alpha') == 0.5, 'alpha')\n"
            "assert(vint_get_property(a, 'image') == 'ui_blank', 'image')\n"
            "assert(vint_get_property(a, 'visible') == true, 'visible')\n"
            "local b = vint_object_find('kid_b')\n"
            "assert(vint_get_property(b, 'leading') == 4294967280, 'unsigned')\n"
            "local r, g, bl = vint_get_property(b, 'tint')\n"
            "assert(r == 0.25 and g == 0.5 and bl == 0.75, 'tint')\n"
            "assert(vint_object_first_child(b) == nil, 'no children')\n"
            "assert(select('#', vint_get_property(b, 'no_such_property')) == 0, 'miss')\n"
            "assert(vint_get_property(vint_object_find('twn'), 'target_name') == 'kid_a', 'tween')\n"
            "vint_set_property(root, 'anchor', 9, 8)\n"
            "local x2, y2 = vint_get_property(root, 'anchor')\n"
            "assert(x2 == 9 and y2 == 8, 'set wins')\n"
            "local c = vint_object_clone(a)\n"
            "assert(c ~= 0, 'clone')\n"
            "function synth_probe_hook() SYNTH_FOUND = vint_object_find('kid_b') end\n",
            "synth.lua");
        CHECK(r.loadOk && r.pcallOk);
        if (!r.pcallOk) std::cerr << r.pcallError << "\n";
        // Back outside: the previous (OPEN) current document is restored.
        CHECK(!host.engineState().currentDefaultDocHandle().known());

        // A chunk with no document: a name that exists somewhere needs the
        // OPEN current document and refuses; an absent name is plain 0.
        auto r2 = host.runChunk(ui, "local h = vint_object_find('root')", "other.lua");
        CHECK(r2.loadOk && !r2.pcallOk && r2.pcallError.find("current default vint document") != std::string::npos);
        auto r3 = host.runChunk(ui, "assert(vint_object_find('never_anywhere') == 0)", "other.lua");
        CHECK(r3.loadOk && r3.pcallOk);
        // An explicit document handle still works from anywhere.
        auto r4 = host.runChunk(ui, "assert(vint_object_find('kid_b', nil, " + std::to_string(*docHandle) + ") ~= 0)",
                                "other.lua");
        CHECK(r4.loadOk && r4.pcallOk);

        // A hook defined by synth.lua, fired with no chunk context of its
        // own, runs in synth.lua's document (its defining chunk).
        std::vector<HookSpec> hooks(1);
        hooks[0].name = "synth_probe_hook";
        hooks[0].group = HookGroup::Group1_GeneralNamed;
        auto fired = host.fireConfirmedHooks(ui, hooks, "other.lua");
        CHECK(fired.size() == 1 && fired[0].attemptedCall && fired[0].callOk);
        lua_getglobal(ui, "SYNTH_FOUND");
        CHECK(lua_type(ui, -1) == LUA_TNUMBER && lua_tonumber(ui, -1) != 0);
        lua_pop(ui, 1);
        CHECK(!host.engineState().currentDefaultDocHandle().known());
    }

    // --- without a resolver nothing changes (the pre-2026-10-03 path) -----
    {
        Host host(vintNames());
        host.engineState().loadVintDocument("synth.vint_doc", doc);
        auto r = host.runChunk(host.uiState(), "local h = vint_object_find('root')", "synth.lua");
        CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("current default vint document") != std::string::npos);
        host.engineState().currentDefaultDocHandle().set(1);
        auto r2 = host.runChunk(host.uiState(), "assert(vint_object_find('root') ~= 0)", "synth.lua");
        CHECK(r2.loadOk && r2.pcallOk);
        CHECK(host.engineState().currentDefaultDocHandle().get() == 1); // untouched without a resolver
    }

    if (g_failures == 0) std::cout << "ALL sr3vintdoc DOCUMENT TESTS PASSED\n";
    return g_failures == 0 ? 0 : 1;
}

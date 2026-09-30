// Seed capture for the fuzz corpora (cloud phase, 2026-09-30).
//
// Linked into a copy of each tests/synthetic_*_test.cpp executable with
// -Wl,--wrap=<mangled parse symbol> for every symbol below (CMakeLists.txt,
// CRREISH_FUZZ). Each wrapper writes the exact bytes the synthetic test hands
// the real reader to $CRREISH_SEED_DIR/<target>/<fnv1a>.bin, in the input
// layout of that target's harness, then calls the real function. The tests
// themselves are unchanged. The declarations below restate each function's
// real signature under its mangled name with C linkage; on the Itanium C++
// ABI (GCC/Clang on Linux) that is call-compatible, including the hidden
// return-slot pointer for the class return types.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3asm/manifest.h"
#include "sr3clmesh/level_mesh.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3lua/parser.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/rig.h"
#include "sr3save/save_directory.h"
#include "sr3save/save_snapshot.h"
#include "sr3texture/texture_pair.h"
#include "sr3vintdoc/vint_doc.h"
#include "sr3xtbl/xtbl.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "vpp/container.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif

namespace {

void save(const char* target, const std::vector<uint8_t>& bytes) {
    const char* dir = std::getenv("CRREISH_SEED_DIR");
    if (!dir || bytes.empty() || bytes.size() > (1u << 20)) return;
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes) h = (h ^ b) * 1099511628211ull;
    std::filesystem::path p = std::filesystem::path(dir) / target;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(h));
    if (FILE* f = std::fopen((p / name).string().c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
}
void save(const char* target, vpp::ByteView v) {
    save(target, std::vector<uint8_t>(v.data(), v.data() + v.size()));
}
void put32(std::vector<uint8_t>& o, uint64_t v) {
    uint32_t x = static_cast<uint32_t>(v);
    uint8_t b[4];
    std::memcpy(b, &x, 4);
    o.insert(o.end(), b, b + 4);
}

} // namespace

#define WRAP1(target, Ret, sym)                                                   \
    extern "C" Ret __real_##sym(vpp::ByteView);                                   \
    extern "C" Ret __wrap_##sym(vpp::ByteView v) {                                \
        save(target, v);                                                          \
        return __real_##sym(v);                                                   \
    }

WRAP1("texture", sr3texture::TexturePair, _ZN10sr3texture11TexturePair5parseEN3vpp8ByteViewE)
WRAP1("vintdoc", sr3vintdoc::Header, _ZN10sr3vintdoc11parseHeaderEN3vpp8ByteViewE)
WRAP1("geometry", sr3geometry::MaterialBlock, _ZN11sr3geometry13MaterialBlock5parseEN3vpp8ByteViewE)
WRAP1("asm", sr3asm::AsmManifest, _ZN6sr3asm11AsmManifest5parseEN3vpp8ByteViewE)
WRAP1("fxo", sr3fxo::ShaderWrapper, _ZN6sr3fxo13ShaderWrapper5parseEN3vpp8ByteViewE)
WRAP1("rig", sr3rig::Rig, _ZN6sr3rig3Rig5parseEN3vpp8ByteViewE)
WRAP1("anim", sr3anim::Animation, _ZN7sr3anim9Animation5parseEN3vpp8ByteViewE)
WRAP1("save", sr3save::SaveSnapshot, _ZN7sr3save12SaveSnapshot5parseEN3vpp8ByteViewE)
WRAP1("zoneheader", sr3zone::ZoneHeader, _ZN7sr3zone10ZoneHeader5parseEN3vpp8ByteViewE)
WRAP1("d3d9bc", sr3d3d9bc::DisassembledShader, _ZN9sr3d3d9bc11disassembleEN3vpp8ByteViewE)

extern "C" sr3geometry::GeometryBlock __real__ZN11sr3geometry13GeometryBlock5parseEN3vpp8ByteViewERKNS_13MaterialBlockE(vpp::ByteView, const sr3geometry::MaterialBlock&);
extern "C" sr3geometry::GeometryBlock __wrap__ZN11sr3geometry13GeometryBlock5parseEN3vpp8ByteViewERKNS_13MaterialBlockE(vpp::ByteView v, const sr3geometry::MaterialBlock& m) {
    save("geometry", v);
    return __real__ZN11sr3geometry13GeometryBlock5parseEN3vpp8ByteViewERKNS_13MaterialBlockE(v, m);
}

extern "C" void __real__ZN3vpp9ContainerC1ENS_8ByteViewE(vpp::Container*, vpp::ByteView);
extern "C" void __wrap__ZN3vpp9ContainerC1ENS_8ByteViewE(vpp::Container* self, vpp::ByteView v) {
    save("vpp", v);
    __real__ZN3vpp9ContainerC1ENS_8ByteViewE(self, v);
}

extern "C" sr3lua::ScriptAnalysis __real__ZN6sr3lua6Parser7analyzeERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE(const std::string&);
extern "C" sr3lua::ScriptAnalysis __wrap__ZN6sr3lua6Parser7analyzeERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE(const std::string& s) {
    save("lua", std::vector<uint8_t>(s.begin(), s.end()));
    return __real__ZN6sr3lua6Parser7analyzeERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE(s);
}

extern "C" sr3mesh::MeshBlock __real__ZN7sr3mesh9MeshBlock5parseEN3vpp8ByteViewEmS2_mm(vpp::ByteView, size_t, vpp::ByteView, size_t, size_t);
extern "C" sr3mesh::MeshBlock __wrap__ZN7sr3mesh9MeshBlock5parseEN3vpp8ByteViewEmS2_mm(vpp::ByteView c, size_t off, vpp::ByteView g, size_t gOff, size_t disp) {
    std::vector<uint8_t> o;
    put32(o, c.size()); put32(o, off); put32(o, gOff); put32(o, disp);
    o.insert(o.end(), c.data(), c.data() + c.size());
    o.insert(o.end(), g.data(), g.data() + g.size());
    save("mesh", o);
    return __real__ZN7sr3mesh9MeshBlock5parseEN3vpp8ByteViewEmS2_mm(c, off, g, gOff, disp);
}

extern "C" sr3save::SaveDirectory __real__ZN7sr3save13SaveDirectory5parseEN3vpp8ByteViewENS0_10HashPolicyE(vpp::ByteView, sr3save::SaveDirectory::HashPolicy);
extern "C" sr3save::SaveDirectory __wrap__ZN7sr3save13SaveDirectory5parseEN3vpp8ByteViewENS0_10HashPolicyE(vpp::ByteView v, sr3save::SaveDirectory::HashPolicy p) {
    save("save", v);
    return __real__ZN7sr3save13SaveDirectory5parseEN3vpp8ByteViewENS0_10HashPolicyE(v, p);
}

extern "C" sr3xtbl::Document __real__ZN7sr3xtbl13ParseDocumentEPKhmRKNS_12ParseOptionsE(const uint8_t*, size_t, const sr3xtbl::ParseOptions&);
extern "C" sr3xtbl::Document __wrap__ZN7sr3xtbl13ParseDocumentEPKhmRKNS_12ParseOptionsE(const uint8_t* d, size_t n, const sr3xtbl::ParseOptions& o) {
    save("xtbl", std::vector<uint8_t>(d, d + n));
    return __real__ZN7sr3xtbl13ParseDocumentEPKhmRKNS_12ParseOptionsE(d, n, o);
}
extern "C" sr3xtbl::Document __real__ZN7sr3xtbl13ParseDocumentESt17basic_string_viewIcSt11char_traitsIcEERKNS_12ParseOptionsE(std::string_view, const sr3xtbl::ParseOptions&);
extern "C" sr3xtbl::Document __wrap__ZN7sr3xtbl13ParseDocumentESt17basic_string_viewIcSt11char_traitsIcEERKNS_12ParseOptionsE(std::string_view s, const sr3xtbl::ParseOptions& o) {
    save("xtbl", std::vector<uint8_t>(s.begin(), s.end()));
    return __real__ZN7sr3xtbl13ParseDocumentESt17basic_string_viewIcSt11char_traitsIcEERKNS_12ParseOptionsE(s, o);
}

extern "C" sr3clmesh::LevelMesh __real__ZN9sr3clmesh9LevelMesh5parseEN3vpp8ByteViewERKNS_11WalkOptionsE(vpp::ByteView, const sr3clmesh::WalkOptions&);
extern "C" sr3clmesh::LevelMesh __wrap__ZN9sr3clmesh9LevelMesh5parseEN3vpp8ByteViewERKNS_11WalkOptionsE(vpp::ByteView v, const sr3clmesh::WalkOptions& o) {
    save("clmesh", v);
    return __real__ZN9sr3clmesh9LevelMesh5parseEN3vpp8ByteViewERKNS_11WalkOptionsE(v, o);
}

extern "C" std::vector<sr3zone::ZoneMeshBlockEntry> __real__ZN7sr3zone12ZoneGeometry6locateEN3vpp8ByteViewES2_(vpp::ByteView, vpp::ByteView);
extern "C" std::vector<sr3zone::ZoneMeshBlockEntry> __wrap__ZN7sr3zone12ZoneGeometry6locateEN3vpp8ByteViewES2_(vpp::ByteView c, vpp::ByteView g) {
    std::vector<uint8_t> o;
    put32(o, c.size());
    o.insert(o.end(), c.data(), c.data() + c.size());
    o.insert(o.end(), g.data(), g.data() + g.size());
    save("zonegeom", o);
    return __real__ZN7sr3zone12ZoneGeometry6locateEN3vpp8ByteViewES2_(c, g);
}

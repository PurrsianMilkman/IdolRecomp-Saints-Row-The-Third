// One libFuzzer harness per single-buffer reader. The CRREISH_FUZZ_TARGET
// macro (set per executable in CMakeLists.txt) picks the reader.
#include <string>
#include <string_view>

#include "fuzz_common.h"

#if CRREISH_FUZZ_TARGET_vpp
#include "vpp/container.h"
#elif CRREISH_FUZZ_TARGET_xtbl
#include "sr3xtbl/xtbl.h"
#elif CRREISH_FUZZ_TARGET_texture
#include "sr3texture/texture_pair.h"
#elif CRREISH_FUZZ_TARGET_geometry
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3geometry/material_binding.h"
#elif CRREISH_FUZZ_TARGET_rig
#include "sr3rig/rig.h"
#elif CRREISH_FUZZ_TARGET_anim
#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#elif CRREISH_FUZZ_TARGET_clmesh
#include "sr3clmesh/level_mesh.h"
#elif CRREISH_FUZZ_TARGET_zoneheader
#include "sr3zone/zone_header.h"
#elif CRREISH_FUZZ_TARGET_save
#include "sr3save/save_directory.h"
#include "sr3save/save_snapshot.h"
#elif CRREISH_FUZZ_TARGET_fxo
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/d3d9_blob.h"
#include "sr3fxo/wrapper_header.h"
#elif CRREISH_FUZZ_TARGET_d3d9bc
#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/hlsl_translator.h"
#elif CRREISH_FUZZ_TARGET_lua
#include "sr3lua/lexer.h"
#include "sr3lua/parser.h"
#elif CRREISH_FUZZ_TARGET_asm
#include "sr3asm/manifest.h"
#elif CRREISH_FUZZ_TARGET_vintdoc
#include "sr3vintdoc/vint_doc.h"
#else
#error "CRREISH_FUZZ_TARGET_<name> not set"
#endif

using fuzzcommon::guarded;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    vpp::ByteView v(data, size);
#if CRREISH_FUZZ_TARGET_vpp
    guarded([&] {
        vpp::Container c(v);
        for (size_t i = 0; i < c.entries().size() && i < 64; ++i) {
            guarded([&] {
                if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
                    (void)c.rawEntryBytes(i);
                    guarded([&] { vpp::Container n = c.openNested(i); (void)n.entries().size(); });
                } else {
                    (void)c.decompressEntry(i);
                }
            });
        }
    });
#elif CRREISH_FUZZ_TARGET_xtbl
    guarded([&] { (void)sr3xtbl::ParseDocument(data, size); });
    guarded([&] {
        sr3xtbl::ParseOptions o;
        o.decodeEntities = false;
        (void)sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(data), size), o);
    });
#elif CRREISH_FUZZ_TARGET_texture
    guarded([&] {
        auto tp = sr3texture::TexturePair::parse(v);
        for (size_t i = 0; i < tp.records().size() && i < 64; ++i) {
            guarded([&] { (void)tp.levelLayout(i); });
            guarded([&] { (void)tp.pixelBytes(i, v); }); // the same bytes stand in for the g-file
        }
        guarded([&] { tp.validateAgainstGpeg(v); });
    });
#elif CRREISH_FUZZ_TARGET_geometry
    guarded([&] {
        auto m = sr3geometry::MaterialBlock::parse(v);
        guarded([&] {
            auto g = sr3geometry::GeometryBlock::parse(v, m);
            for (size_t i = 0; i < g.arrays().size() && i < 16; ++i) guarded([&] { (void)g.arrayBytes(i, v); });
            guarded([&] { (void)sr3geometry::MaterialBindings::parse(v, g.offset(), v.size()); });
        });
        guarded([&] { (void)sr3geometry::GeometryBlock::parseAt(v, m.totalSize); });
    });
#elif CRREISH_FUZZ_TARGET_rig
    guarded([&] { (void)sr3rig::Rig::parse(v); });
#elif CRREISH_FUZZ_TARGET_anim
    guarded([&] {
        auto a = sr3anim::Animation::parse(v);
        (void)a.rootRotationNormSquared();
        guarded([&] {
            auto p = sr3anim::Payload::walk(v, a);
            for (size_t i = 0; i < p.tracks().size() && i < 64; ++i) guarded([&] { (void)p.rotations(v, i); });
        });
    });
#elif CRREISH_FUZZ_TARGET_clmesh
    guarded([&] { (void)sr3clmesh::LevelMesh::parse(v); });
#elif CRREISH_FUZZ_TARGET_zoneheader
    guarded([&] { (void)sr3zone::ZoneHeader::parse(v); });
#elif CRREISH_FUZZ_TARGET_save
    guarded([&] { (void)sr3save::SaveDirectory::parse(v, sr3save::SaveDirectory::HashPolicy::Ignore); });
    guarded([&] { (void)sr3save::SaveSnapshot::parse(v); });
#elif CRREISH_FUZZ_TARGET_fxo
    guarded([&] {
        auto sw = sr3fxo::ShaderWrapper::parse(v);
        for (const auto& es : sw.shaders())
            if (es.offset <= size && es.length <= size - es.offset)
                (void)sr3fxo::inspectD3d9Blob(vpp::ByteView(data + es.offset, es.length));
    });
    guarded([&] {
        sr3fxo::WrapperHeader h;
        std::string why;
        if (!sr3fxo::WrapperHeader::tryParse(v, h, why)) return;
        size_t end = 0;
        for (const auto& b : h.layoutBlobs(end))
            if (b.offset <= size && b.length <= size - b.offset)
                (void)sr3fxo::inspectD3d9Blob(vpp::ByteView(data + b.offset, b.length));
    });
#elif CRREISH_FUZZ_TARGET_d3d9bc
    guarded([&] {
        auto d = sr3d3d9bc::disassemble(v);
        guarded([&] {
            auto ct = sr3d3d9bc::readConstantTable(v, d);
            guarded([&] { (void)sr3d3d9bc::translateToHlsl(d, ct, v, sr3d3d9bc::HlslTarget::SM3Legacy); });
            guarded([&] { (void)sr3d3d9bc::translateToHlsl(d, ct, v, sr3d3d9bc::HlslTarget::SM4_5); });
        });
    });
#elif CRREISH_FUZZ_TARGET_lua
    std::string src(reinterpret_cast<const char*>(data), size);
    guarded([&] {
        sr3lua::Lexer lx(src);
        for (int i = 0; i < 1000000; ++i) {
            if (lx.next().type == sr3lua::TokType::Eof) break;
        }
    });
    guarded([&] { (void)sr3lua::Parser::analyze(src); });
#elif CRREISH_FUZZ_TARGET_asm
    guarded([&] { (void)sr3asm::AsmManifest::parse(v); });
#elif CRREISH_FUZZ_TARGET_vintdoc
    guarded([&] {
        if (!sr3vintdoc::looksLikeVintDoc(v)) return;
        auto h = sr3vintdoc::parseHeader(v);
        auto t = sr3vintdoc::parseStringTable(v);
        std::string s;
        for (uint32_t i = 0; i < t.offsets.size() && i < 4096; ++i) (void)sr3vintdoc::resolveStringNulTerminated(v, t, i, s);
        // Exercise every positioned decoder from the string-pool base and
        // from header +0x16 (both candidate positions, see vint_doc.h).
        for (size_t start : {t.base, static_cast<size_t>(h.secondaryOffsetRaw)}) {
            guarded([&] {
                sr3vintdoc::Cursor c(v, start);
                for (uint32_t i = 0; i < h.criticalResourceCount && i < 256; ++i) (void)sr3vintdoc::readCriticalResource(c, h.version);
                for (uint32_t i = 0; i < h.metadataCount && i < 256; ++i) (void)sr3vintdoc::readMetadataEntry(c);
                (void)sr3vintdoc::readElementHead(c);
                auto ph = sr3vintdoc::readPropertyBlockHeader(c);
                (void)sr3vintdoc::selectPropertyListOffset(ph, [](uint32_t) { return false; });
                (void)sr3vintdoc::readPropertyList(c);
            });
        }
    });
#endif
    return 0;
}

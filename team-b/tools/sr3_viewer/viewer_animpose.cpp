#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_common.h"
#include "viewer_multichannel.h"
#include "viewer_skinning.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3geometry/character_matlib.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3anim/sample.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"
#include "vpp/container.h"

using namespace sr3viewer;


// Stage 2 of animation playback (HANDOFF.md "RESUME HERE", Sec9.53/
// Sec9.53.1, and this session's stage-2 follow-on): drives the SAME
// stage-1 skinning pipeline with REAL `.anim_pc` decoded keyframe data,
// sampled over time, instead of stage 1b's one hand-authored test
// rotation. Builds on:
//   * sr3anim::sampleClipAtTime() (sr3anim/sample.h) - Payload + a time ->
//     one rotation + one translation delta per bone. See that header for
//     the time model: translation key timing is REAL (the confirmed
//     per-key duration byte); rotation key timing is an explicit, flagged
//     ASSUMPTION (uniform spacing across the clip), because the confirmed
//     payload format carries no per-key duration for rotation at all.
//   * sr3rig::computeAnimatedSkinningMatrices() (sr3rig/animated_pose.h) -
//     layers the sparse translation deltas on top of stage 1's
//     computeSkinningMatrices() WITHOUT modifying it (see that header for
//     why the "just perturb restPositions" shortcut is actively wrong,
//     not merely imprecise).
//   * sr3rig::conjugateToMeshSpace() - the pose is computed ENTIRELY IN
//     RIG SPACE (rig-space rest positions, rotations exactly as decoded,
//     no conjugation in the middle); only the FINAL per-bone skin matrix
//     is converted to mesh space, once. Proven algebraically AND
//     numerically equivalent to converting every rest position/rotation
//     to mesh space individually before composing (validate_animated_
//     pose.cpp case 5) - this is an equivalent reformulation, not a new
//     approximation.
//
// Prints the bone-length-preservation check for every rendered frame -
// see the printed NOTE at the end of this function for what it does and
// does not prove; it is a regression floor for arithmetic bugs, not a
// visual-correctness check.
int runAnimPose(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr,
                     "usage: sr3_viewer animpose <characters.vpp_pc> <name.ccmesh_pc> "
                     "<name.rig_pc>\n"
                     "                          <anim_archive.vpp_pc> <clip.anim_pc> <out_prefix>\n"
                     "                          [--mode uv|checker|normal|material]\n"
                     "                          [--size W H] [--yaw D] [--pitch D]\n"
                     "                          [--channel N] [--group N] "
                     "[--times f0,f1,f2,...]\n"
                     "                          [--bone-palette] [--legacy-skinning]\n"
                     "       writes <out_prefix>_bind.png and <out_prefix>_t<i>_frac<f>.png\n"
                     "       Skinning convention is AUTO-DETECTED per mesh (HANDOFF Sec9.63.9): "
                     "single-set bone\n"
                     "       palette -> palette + (-x,-y,-z) [DEFAULT, promoted from opt-in]; "
                     "multi-set -> the\n"
                     "       block's own per-draw-range set selector (Sec9.63.10), refusing only "
                     "if that array\n"
                     "       or its per-vertex assignment is ambiguous; no palette declared -> REFUSED\n"
                     "       (no spec-backed rule; vertex Sec9 / rig Sec11.15).\n"
                     "       --bone-palette: kept for compatibility - a no-op now that this is "
                     "the default.\n"
                     "       --legacy-skinning: FORCE the pre-promotion direct-index + (x,-y,-z) "
                     "reading even on a\n"
                     "                          single-set or no-palette mesh (RETRACTED in spec; debugging only), to reproduce Sec9.56.x's "
                     "pre-Sec9.63 numbers.\n"
                     "       --times are FRACTIONS of the clip's own duration (+0x06), default "
                     "0,0.33,0.66,1.0\n");
        return 1;
    }
    const std::string charArchivePath = argv[2];
    const std::string cmeshName = argv[3];
    const std::string rigName = argv[4];
    const std::string animArchivePath = argv[5];
    const std::string clipName = argv[6];
    const std::string outPrefix = argv[7];
    if (outPrefix.rfind("--", 0) == 0) {
        std::fprintf(stderr,
                     "output prefix '%s' looks like a flag - it is POSITIONAL and must come "
                     "before any --options\n", outPrefix.c_str());
        return 2;
    }

    uint32_t width = 720, height = 900;
    float yaw = 0.0f, pitch = 0.1f;
    size_t channelIndex = 0;
    size_t drawGroup = 0;
    sr3render::MeshDrawMode mode = sr3render::MeshDrawMode::Checker;
    std::vector<float> fractions = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    // DIAGNOSTIC ONLY, added while investigating stage 2's open rig-space
    // rotation-handedness question (HANDOFF Sec9.53): negates each sampled
    // quaternion's vector part (x,y,z) before conversion, i.e. uses the
    // quaternion's CONJUGATE/INVERSE rotation instead of the one decoded
    // as-is. For a unit quaternion this is exactly the opposite-handed
    // rotation by the same angle about the same axis. Not a claim that
    // either convention is correct - a bone-length-preservation pass
    // cannot distinguish them (both are orthogonal transforms) - purely
    // so a human can compare the two renders side by side.
    bool invertRotation = false;
    // DIAGNOSTIC ONLY: when non-empty, every bone whose (lowercased) name
    // does not CONTAIN this substring is forced to identity/zero-delta
    // regardless of its track - isolates one real decoded joint rotation
    // at a time, the same way stage 1b isolated one hand-authored
    // rotation, to tell apart "many real joints composed together" from
    // "a single real joint in isolation" when judging a render.
    std::string isolateBoneSubstr;
    // DIAGNOSTIC ONLY, added while root-causing HANDOFF Sec9.56's
    // whole-skeleton fragmentation. Freezes the four non-skeletal
    // "camera shot" bones (spec-rig-format.md Sec5/Sec8.1: `camera`,
    // `camtarget`, `camfov`, `camdof` - real, named, ordinary bones in
    // the SAME array/hash-table as every skeletal joint, but "helper
    // bones deliberately placed outside the body": on brad, bone 47
    // `camera` sits at rig-space (0,0,1), a metre in front of the
    // character, per that section's own worked example) to identity
    // rotation / zero translation delta regardless of their track. This
    // clip (`auto_entrl_extct_shot.anim_pc` - an "entrance/extraction
    // SHOT") drives those four bones with a real, multi-unit scripted
    // camera path, and brad.ccmesh_pc genuinely assigns real vertex
    // blend weight to those same bone-array slots (confirmed
    // numerically, not assumed - see the investigation's bisection
    // probe), so without this flag those camera-path translations get
    // applied to real body vertices as if they were an ordinary bone's
    // animated pose. NOT known from the spec to be the engine's actual
    // behavior (no disassembly consulted, per this project's rule) -
    // this is a diagnostic isolation, exactly like --isolate-bone above,
    // not a claimed-correct default.
    bool excludeCameraBones = false;
    // HANDOFF Sec9.63.9: --bone-palette is now a NO-OP, kept only for
    // compatibility with prior invocations - the palette + (-x,-y,-z)
    // reading it used to opt into is the auto-detected DEFAULT for
    // single-set meshes (resolveSkinningMode() above runPose). Recorded
    // just so a stale --bone-palette in a script/README does not error out.
    bool bonePaletteFlagPassed = false;
    // HANDOFF Sec9.63.9: FORCE the pre-promotion direct-index + (x,-y,-z)
    // reading (what every command produced before Sec9.62/Sec9.63)
    // regardless of what the mesh's own palette declares - the escape
    // hatch for reproducing Sec9.56.x's historical figures on demand, per
    // this project's measurement-discipline rule about not losing the
    // ability to regenerate a prior result.
    bool legacySkinning = false;

    for (int i = 8; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--size" && i + 2 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--yaw" && i + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && i + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--channel" && i + 1 < argc) {
            channelIndex = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--group" && i + 1 < argc) {
            drawGroup = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--invert-rotation") {
            invertRotation = true;
        } else if (arg == "--isolate-bone" && i + 1 < argc) {
            isolateBoneSubstr = argv[++i];
            for (auto& ch : isolateBoneSubstr)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (arg == "--exclude-camera-bones") {
            excludeCameraBones = true;
        } else if (arg == "--bone-palette") {
            bonePaletteFlagPassed = true;
        } else if (arg == "--legacy-skinning") {
            legacySkinning = true;
        } else if (arg == "--times" && i + 1 < argc) {
            fractions.clear();
            std::string list = argv[++i];
            size_t pos = 0;
            while (pos <= list.size()) {
                size_t comma = list.find(',', pos);
                std::string tok = list.substr(
                    pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty()) fractions.push_back(static_cast<float>(std::atof(tok.c_str())));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            if (fractions.empty()) fractions = {0.0f};
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string m = argv[++i];
            if (m == "uv") mode = sr3render::MeshDrawMode::UvAsColour;
            else if (m == "checker") mode = sr3render::MeshDrawMode::Checker;
            else if (m == "normal") mode = sr3render::MeshDrawMode::NormalAsColour;
            else if (m == "material") mode = sr3render::MeshDrawMode::MaterialIdAsColour;
            else {
                std::fprintf(stderr, "unknown --mode '%s' (uv, checker, normal, material)\n",
                             m.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }
    if (bonePaletteFlagPassed && legacySkinning) {
        std::fprintf(stderr,
                     "--bone-palette and --legacy-skinning are contradictory (the first asks for "
                     "the palette reading, now default anyway; the second forces the old "
                     "direct-index reading) - pass at most one\n");
        return 2;
    }

    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';

    std::vector<uint8_t> charArchive = readFile(charArchivePath);
    if (charArchive.empty()) {
        std::fprintf(stderr, "could not read %s\n", charArchivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> cb, gb, rb, unusedSibling;
    try {
        vpp::Container container(vpp::ByteView(charArchive.data(), charArchive.size()));
        if (!findEntry(container, cmeshName, cb, gb, gmeshName)) {
            std::fprintf(stderr, "could not find '%s'\n", cmeshName.c_str());
            return 1;
        }
        if (!findEntry(container, rigName, rb, unusedSibling, "")) {
            std::fprintf(stderr, "could not find '%s'\n", rigName.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    if (gb.empty()) {
        std::fprintf(stderr, "found '%s' but not its paired '%s'\n", cmeshName.c_str(),
                     gmeshName.c_str());
        return 1;
    }

    std::vector<uint8_t> animArchive = readFile(animArchivePath);
    if (animArchive.empty()) {
        std::fprintf(stderr, "could not read %s\n", animArchivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> clipBytesVec, unusedSibling2;
    try {
        vpp::Container animContainer(vpp::ByteView(animArchive.data(), animArchive.size()));
        if (!findEntry(animContainer, clipName, clipBytesVec, unusedSibling2, "")) {
            std::fprintf(stderr, "could not find '%s' in %s\n", clipName.c_str(),
                         animArchivePath.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "anim archive error: %s\n", ex.what());
        return 1;
    }

    std::printf("mesh     : %s (%zu bytes) + %s (%zu bytes)\n", cmeshName.c_str(), cb.size(),
                gmeshName.c_str(), gb.size());
    std::printf("rig      : %s (%zu bytes)\n", rigName.c_str(), rb.size());
    std::printf("clip     : %s (%zu bytes)\n", clipName.c_str(), clipBytesVec.size());

    sr3geometry::MaterialBlock material =
        sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry =
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    if (!geometry.hasMeshSubBlock()) {
        std::fprintf(stderr, "no Mesh sub-block in this file\n");
        return 1;
    }
    sr3mesh::MeshBlock mesh =
        sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                  geometry.meshSubBlockOffset(), vpp::ByteView(gb.data(), gb.size()));
    if (channelIndex >= mesh.channels().size()) {
        std::fprintf(stderr, "channel %zu out of range\n", channelIndex);
        return 1;
    }
    if (mesh.drawGroupsLocated() && drawGroup >= mesh.drawGroups().size()) {
        std::fprintf(stderr, "group %zu does not exist (%zu available)\n", drawGroup,
                     mesh.drawGroups().size());
        return 2;
    }
    const auto& channel = mesh.channels()[channelIndex];
    std::printf("channel  : layout %u, %u texcoord set(s), stride %zu, %u vertices\n",
                channel.layoutCode, channel.texcoordCount, channel.stride(), channel.elementCount);
    if (!sr3mesh::layoutInfoFor(channel.layoutCode).hasSkinning) {
        std::fprintf(stderr,
                     "channel %zu's layout carries no blend weights/indices - nothing to pose\n",
                     channelIndex);
        return 1;
    }

    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
    const auto& bones = rig.bones();
    std::printf("rig      : %zu bones, hash table %s\n", bones.size(),
                rig.hashTableMatchesNames() ? "OK" : "MISMATCH");

    vpp::ByteView clipBytes(clipBytesVec.data(), clipBytesVec.size());
    sr3anim::Animation anim = sr3anim::Animation::parse(clipBytes);
    sr3anim::Payload payload = sr3anim::Payload::walk(clipBytes, anim);
    std::printf("anim     : flags=0x%02X duration(+0x06)=%u tracks(+0x0A)=%u hasTable=%s "
                "walkComplete=%s landedOnDeclaredEnd=%s hasUnaccountedPayload=%s\n",
                anim.flags(), anim.durationTotal(),
                static_cast<unsigned>(anim.field_0x0A_rawCount()),
                anim.hasTrackBoneTable() ? "yes" : "no", payload.walkComplete() ? "yes" : "no",
                payload.landedOnDeclaredEnd() ? "yes" : "no",
                payload.hasUnaccountedPayload() ? "yes" : "no");
    if (!payload.walkComplete() || payload.hasUnaccountedPayload() ||
        (anim.hasTrailingOffset() && !payload.landedOnDeclaredEnd())) {
        std::fprintf(stderr,
                     "WARNING: this clip is not in the confirmed-clean population "
                     "(spec-anim-format.md Sec6c.4) - proceeding best-effort, results may be "
                     "unreliable\n");
    }
    const float duration = static_cast<float>(anim.durationTotal());

    // Compatibility check: every track's mapped bone index must be inside
    // this rig, or the pose is meaningless for those bones (see
    // tools/validation/match_anim_rig.cpp, which is how this clip/rig pair
    // was found).
    size_t maxBoneTouched = 0;
    const bool hasTable = anim.hasTrackBoneTable();
    for (size_t t = 0; t < payload.tracks().size(); ++t) {
        size_t bi = t;
        if (hasTable) {
            size_t at = static_cast<size_t>(anim.trackBoneTableOffset()) + t;
            if (at < clipBytes.size()) bi = clipBytes.at(at);
        }
        if (bi > maxBoneTouched) maxBoneTouched = bi;
    }
    std::printf("compat   : clip touches bone indices up to %zu, rig has %zu bones (%s)\n",
                maxBoneTouched, bones.size(),
                maxBoneTouched < bones.size() ? "COMPATIBLE" : "INCOMPATIBLE");
    if (maxBoneTouched >= bones.size()) {
        std::fprintf(stderr, "clip is not compatible with this rig - aborting\n");
        return 1;
    }

    // RIG-SPACE rest positions - deliberately NOT converted to mesh space
    // (see this function's header comment: the whole pose is computed in
    // rig space, and only the final skin matrix is conjugated).
    std::vector<uint32_t> parentIdx(bones.size());
    std::vector<std::array<float, 3>> restRig(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        parentIdx[i] = bones[i].parentIndex;
        restRig[i] = bones[i].restPosition;
    }

    std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(channelIndex);
    if (verts.empty()) {
        std::fprintf(stderr, "channel produced no vertices\n");
        return 1;
    }

    // HANDOFF Sec9.63.9: auto-detect palette vs. direct-index skinning from
    // the mesh's own declaration (resolveSkinningMode(), defined above
    // runPose). A multi-set mesh refuses here, exactly as --bone-palette
    // already refused it - the refusal is now automatic, not gated on the
    // flag.
    const SkinningModeResult skinModeResult = resolveSkinningMode(mesh, legacySkinning);
    if (isSkinningRefusal(skinModeResult.mode)) {
        std::fprintf(stderr, "%s\n", skinModeResult.refusalMessage.c_str());
        return 1;
    }
    const bool useBonePalette = skinModeResult.mode == SkinningMode::kPaletteSingleSet ||
                                skinModeResult.mode == SkinningMode::kPaletteMultiSet;
    std::printf("skinning : %s\n", skinningModeLabel(skinModeResult.mode));
    if (bonePaletteFlagPassed) {
        std::printf(
            "note     : --bone-palette is a no-op now - %s\n",
            skinModeResult.mode == SkinningMode::kPaletteSingleSet
                ? "this reading is already the DEFAULT for this single-set mesh (HANDOFF Sec9.63.9)"
            : skinModeResult.mode == SkinningMode::kPaletteMultiSet
                ? "this multi-set mesh uses its own per-draw-range set selector (HANDOFF Sec9.63.10)"
                : "this block declares no bone palette at all, so there is nothing to opt into");
    }

    sr3render::VertexDuplication vertexDuplication; // HANDOFF Sec9.68; stays empty except for `reynolds`
    bool multiChannelNeeded = false; // HANDOFF <multi-channel section>: see below
    if (skinModeResult.mode == SkinningMode::kPaletteMultiSet) {
        // HANDOFF Sec9.63.10/Sec9.68 - see the same block in runPose() for
        // the vertex-duplication conflict resolution.
        sr3rig::BlendIndexRemapStats remap;
        sr3rig::VertexDuplicationResult dup =
            applyMultiSetSkinning(mesh, verts, bones.size(), drawGroup, remap);
        if (!dup.usable) {
            std::fprintf(stderr, "%s\n", dup.problem.c_str());
            return 1;
        }
        if (dup.duplicatesCreated != 0) {
            vertexDuplication.sourceIndex = dup.duplicateSourceIndex;
            vertexDuplication.redirectPerRange = dup.redirectPerRange;
            std::printf("dup      : %lld conflicting vertex-uses resolved by duplication "
                        "(HANDOFF Sec9.68)\n", dup.duplicatesCreated);
        }
        std::printf("palette  : %zu entries in %zu sets (rig %zu bones); group %zu: %zu vertices "
                    "total (%lld duplicated); lanes remapped=%lld dropped(no palette slot)=%lld "
                    "dropped(rig index out of range)=%lld\n",
                    mesh.bonePalette().size(), mesh.bonePaletteSets().size(), bones.size(),
                    drawGroup, verts.size(), dup.duplicatesCreated,
                    remap.lanesRemapped, remap.lanesDroppedPaletteOutOfRange,
                    remap.lanesDroppedRigOutOfRange);
        // A dropped lane means a vertex carries a blend index its range's
        // named set cannot hold - which on a correct reading never happens:
        // measured 0 dropped lanes on all 46 renderable multi-set meshes at
        // draw group 0 (HANDOFF Sec9.63.10/Sec9.68). It DOES happen on the
        // LOD groups of the two multi-channel meshes, where the range draws
        // from a channel this command did not upload (`alien_e01 --group 4`:
        // 2,213 lanes). WHICH channel is no longer open (fixed 2026-09-29,
        // HANDOFF Sec9.89/Sec9.103, rule 16): DrawRange::submeshIndex IS the
        // channel index, confirmed both on the 393-vehicle population AND
        // directly re-checked against alien_e01 itself here (28/28 real
        // draw ranges across all 8 groups, including group 4 specifically,
        // land exactly inside their own submeshIndex-selected channel's
        // real vertex count). FIXED (multi-channel section, 2026-09-29):
        // this used to be a hard refusal here - now the cause is KNOWN, so
        // instead of giving up, fall through to the real per-channel
        // multi-channel render (MultiChannelMultiSetRender, defined above
        // runPose()) once this block finishes printing its own (still
        // accurate) diagnostic numbers. Every SINGLE-channel mesh
        // (single-set or multi-set) still gets
        // lanesDroppedPaletteOutOfRange == 0 here and never reaches this
        // branch at all - the regression guarantee is structural, not
        // behavioural: the code above executes first and unconditionally
        // for every mesh.
        if (remap.lanesDroppedPaletteOutOfRange != 0) {
            std::printf(
                "channel  : %lld blend lane(s) in draw group %zu do not fit --channel %zu's own "
                "bone-palette set - this group's ranges span more than one vertex channel "
                "(DrawRange::submeshIndex, HANDOFF Sec9.89/Sec9.103/Sec9.104). Switching to a "
                "per-channel multi-channel render instead of refusing.\n",
                remap.lanesDroppedPaletteOutOfRange, drawGroup, channelIndex);
            multiChannelNeeded = true;
        }
    } else if (useBonePalette) {
        const std::vector<uint8_t>& palette = mesh.bonePalette();
        sr3rig::BlendIndexRemapStats remap;
        verts = sr3rig::remapBlendIndicesThroughPalette(verts, palette, bones.size(), &remap);
        std::printf("palette  : %zu entries (rig %zu bones), first=%u last=%u; lanes remapped=%lld "
                    "dropped(no palette slot)=%lld dropped(rig index out of range)=%lld\n",
                    palette.size(), bones.size(), static_cast<unsigned>(palette.front()),
                    static_cast<unsigned>(palette.back()), remap.lanesRemapped,
                    remap.lanesDroppedPaletteOutOfRange, remap.lanesDroppedRigOutOfRange);
    }

    if (multiChannelNeeded) {
        // Mirrors the per-frame sampling math of the loop below EXACTLY
        // (same sampleClipAtTime/computeAnimatedSkinningMatrices/
        // conjugateToMeshSpaceInverted calls, same --isolate-bone/
        // --exclude-camera-bones/--invert-rotation handling, same
        // bone-length-preservation bookkeeping) - duplicated rather than
        // shared with the single-channel loop below so that loop's own
        // code path (and therefore its MD5 output on every single-channel
        // mesh) is provably untouched by this addition. useBonePalette is
        // always true when multiChannelNeeded is true (both are gated on
        // SkinningMode::kPaletteMultiSet), so restMesh always uses the
        // (-x,-y,-z) palette reading, unconditionally, unlike the general
        // form below.
        std::vector<std::array<float, 3>> restMesh(bones.size());
        for (size_t i = 0; i < bones.size(); ++i)
            restMesh[i] = sr3rig::rigToMeshSpaceInverted(bones[i].restPosition);
        std::vector<sr3rig::Mat3x4> bindSkin =
            sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);

        sr3render::RenderDevice mcDevice;
        std::string mcError;
        if (!mcDevice.initialise(width, height, mcError)) {
            std::fprintf(stderr, "device init failed: %s\n", mcError.c_str());
            return 1;
        }
        std::printf("device   : %s (%s)\n",
                    mcDevice.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                    mcDevice.adapterName().c_str());

        std::vector<MultiChannelFrame> frames;
        frames.push_back({outPrefix + "_bind.png", bindSkin});

        double overallMaxDeviation = 0.0;
        size_t overallMaxDeviationBoneIdx = static_cast<size_t>(-1);
        size_t overallMaxDeviationTimeIdx = static_cast<size_t>(-1);
        size_t bonePairCount = 0;
        for (size_t i = 0; i < bones.size(); ++i)
            if (!bones[i].isRoot()) ++bonePairCount;

        for (size_t ti = 0; ti < fractions.size(); ++ti) {
            const float frac = fractions[ti];
            const float t = frac * duration;
            std::vector<sr3anim::BoneSample> samples =
                sr3anim::sampleClipAtTime(clipBytes, anim, payload, bones.size(), t);

            std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
            std::vector<std::array<float, 3>> deltasRig(bones.size(),
                                                         std::array<float, 3>{0.0f, 0.0f, 0.0f});
            size_t rotTracks = 0, transTracks = 0;
            for (size_t b = 0; b < bones.size(); ++b) {
                if (!isolateBoneSubstr.empty()) {
                    std::string nm = bones[b].name;
                    for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (nm.find(isolateBoneSubstr) == std::string::npos) continue;
                }
                if (excludeCameraBones) {
                    std::string nm = bones[b].name;
                    for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (nm == "camera" || nm == "camtarget" || nm == "camfov" || nm == "camdof") continue;
                }
                const auto& s = samples[b];
                const float sign = invertRotation ? -1.0f : 1.0f;
                rotsRig[b] = sr3rig::quatToRotation3(sign * s.rotation.x, sign * s.rotation.y,
                                                     sign * s.rotation.z, s.rotation.w);
                deltasRig[b] = s.translationDelta;
                if (s.hasRotationTrack) ++rotTracks;
                if (s.hasTranslationTrack) ++transTracks;
            }

            std::vector<sr3rig::Mat3x4> skinRig =
                sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);

            double frameMaxDeviation = 0.0;
            size_t frameMaxDeviationBone = static_cast<size_t>(-1);
            for (size_t b = 0; b < bones.size(); ++b) {
                if (bones[b].isRoot()) continue;
                const uint32_t p = bones[b].parentIndex;
                std::array<float, 3> worldB = skinRig[b].transformPoint(restRig[b]);
                std::array<float, 3> worldP = skinRig[p].transformPoint(restRig[p]);
                double dx = worldB[0] - worldP[0], dy = worldB[1] - worldP[1], dz = worldB[2] - worldP[2];
                double animLen = std::sqrt(dx * dx + dy * dy + dz * dz);
                double rdx = restRig[b][0] - restRig[p][0], rdy = restRig[b][1] - restRig[p][1],
                       rdz = restRig[b][2] - restRig[p][2];
                double restLen = std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
                double dev = std::fabs(animLen - restLen);
                if (dev > frameMaxDeviation) {
                    frameMaxDeviation = dev;
                    frameMaxDeviationBone = b;
                }
                if (dev > overallMaxDeviation) {
                    overallMaxDeviation = dev;
                    overallMaxDeviationBoneIdx = b;
                    overallMaxDeviationTimeIdx = ti;
                }
            }

            std::vector<sr3rig::Mat3x4> skinMesh(bones.size());
            for (size_t b = 0; b < bones.size(); ++b)
                skinMesh[b] = sr3rig::conjugateToMeshSpaceInverted(skinRig[b]);

            char frameLabel[64];
            std::snprintf(frameLabel, sizeof frameLabel, "_t%zu_frac%.2f", ti,
                         static_cast<double>(frac));
            frames.push_back({outPrefix + frameLabel + ".png", skinMesh});

            std::printf(
                "frame %zu: t=%.3f (frac=%.2f)  bones-with-rotation=%zu/%zu  "
                "bones-with-translation=%zu/%zu  bone-length max deviation=%.6e at bone %zu (\"%s\") "
                "over %zu bone-pairs  (multi-channel: lane drops checked per channel at setup, "
                "see \"channels\" above)\n",
                ti, static_cast<double>(t), static_cast<double>(frac), rotTracks, bones.size(),
                transTracks, bones.size(), frameMaxDeviation,
                frameMaxDeviationBone == static_cast<size_t>(-1) ? 0 : frameMaxDeviationBone,
                frameMaxDeviationBone == static_cast<size_t>(-1)
                    ? ""
                    : bones[frameMaxDeviationBone].name.c_str(),
                bonePairCount);
        }

        if (!runMultiChannelFrames(mcDevice, mesh, drawGroup, bones.size(), width, height, yaw, pitch,
                                   mode, frames)) {
            return 1;
        }

        std::printf(
            "\nbone-length-preservation summary: max deviation = %.6e at bone %zu (\"%s\"), "
            "time-sample %zu (t=%.3f), over %zu bone-pairs x %zu sample times = %zu checks\n",
            overallMaxDeviation, overallMaxDeviationBoneIdx == static_cast<size_t>(-1)
                                     ? 0
                                     : overallMaxDeviationBoneIdx,
            overallMaxDeviationBoneIdx == static_cast<size_t>(-1)
                ? ""
                : bones[overallMaxDeviationBoneIdx].name.c_str(),
            overallMaxDeviationTimeIdx == static_cast<size_t>(-1) ? 0 : overallMaxDeviationTimeIdx,
            overallMaxDeviationTimeIdx == static_cast<size_t>(-1)
                ? 0.0
                : static_cast<double>(fractions[overallMaxDeviationTimeIdx]) * duration,
            bonePairCount, fractions.size(), bonePairCount * fractions.size());
        std::printf(
            "NOTE: this check catches arithmetic bugs (composition order, interpolation, a units\n"
            "error) - it CANNOT confirm the animation is not mirrored/inverted/rotating the wrong\n"
            "way, since both a correct and an incorrectly-handed rotation convention preserve\n"
            "rigidity equally (both are orthogonal transforms). A non-zero deviation at a bone whose\n"
            "track carries translation keys is EXPECTED (that bone's pair distance is deliberately\n"
            "animated there), not a bug by itself - see which bone name is reported above.\n");
        return 0;
    }

    // BIND pose (identity), mesh-space, rendered once as a visual baseline
    // and used to build the shared camera (same pattern as runPose()).
    std::vector<std::array<float, 3>> restMesh(bones.size());
    for (size_t i = 0; i < bones.size(); ++i)
        restMesh[i] = useBonePalette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                     : sr3rig::rigToMeshSpace(bones[i].restPosition);
    std::vector<sr3rig::Mat3x4> bindSkin = sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
    std::vector<std::array<float, 3>> bindPositions = sr3rig::skinVertices(verts, bindSkin, nullptr);
    const sr3render::VertexDuplication* dupPtr =
        vertexDuplication.sourceIndex.empty() ? nullptr : &vertexDuplication;

    sr3render::RenderDevice device;
    std::string error;
    if (!device.initialise(width, height, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());

    sr3render::MeshRenderer renderer;
    if (!renderer.initialise(device.device(), error)) {
        std::fprintf(stderr, "mesh renderer init failed: %s\n", error.c_str());
        return 1;
    }
    if (!renderer.createDepth(device.device(), width, height, error)) {
        std::fprintf(stderr, "depth buffer failed: %s\n", error.c_str());
        return 1;
    }

    if (!renderer.upload(device.device(), mesh, channelIndex, 0, error, drawGroup, &bindPositions,
                         dupPtr)) {
        std::fprintf(stderr, "bind-pose upload failed: %s\n", error.c_str());
        return 1;
    }
    const float* mn = renderer.boundsMin();
    const float* mx = renderer.boundsMax();
    float viewProjection[16];
    sr3render::buildOrbitViewProjection(mn, mx, yaw, pitch, 1.9f,
                                        static_cast<float>(width) / static_cast<float>(height),
                                        viewProjection);

    auto renderOne = [&](const std::string& path, std::vector<uint8_t>& outPixels) -> bool {
        device.bindRenderTargetWithDepth(renderer.depthView());
        device.clear({28, 30, 38, 255});
        renderer.clearDepth(device.context());
        renderer.draw(device.context(), viewProjection, mode, nullptr);
        if (!device.readBack(outPixels, error)) {
            std::fprintf(stderr, "readback failed: %s\n", error.c_str());
            return false;
        }
        if (!sr3render::writePng(path, width, height, outPixels, error)) {
            std::fprintf(stderr, "png write failed: %s\n", error.c_str());
            return false;
        }
        std::printf("wrote    : %s\n", path.c_str());
        return true;
    };

    std::vector<uint8_t> bindPixels;
    const std::string bindPath = outPrefix + "_bind.png";
    if (!renderOne(bindPath, bindPixels)) return 1;

    double overallMaxDeviation = 0.0;
    size_t overallMaxDeviationBoneIdx = static_cast<size_t>(-1);
    size_t overallMaxDeviationTimeIdx = static_cast<size_t>(-1);
    size_t bonePairCount = 0;
    for (size_t i = 0; i < bones.size(); ++i)
        if (!bones[i].isRoot()) ++bonePairCount;

    for (size_t ti = 0; ti < fractions.size(); ++ti) {
        const float frac = fractions[ti];
        const float t = frac * duration;
        std::vector<sr3anim::BoneSample> samples =
            sr3anim::sampleClipAtTime(clipBytes, anim, payload, bones.size(), t);

        std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
        std::vector<std::array<float, 3>> deltasRig(bones.size(),
                                                     std::array<float, 3>{0.0f, 0.0f, 0.0f});
        size_t rotTracks = 0, transTracks = 0;
        for (size_t b = 0; b < bones.size(); ++b) {
            if (!isolateBoneSubstr.empty()) {
                std::string nm = bones[b].name;
                for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (nm.find(isolateBoneSubstr) == std::string::npos) continue; // stays identity/zero
            }
            if (excludeCameraBones) {
                std::string nm = bones[b].name;
                for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (nm == "camera" || nm == "camtarget" || nm == "camfov" || nm == "camdof") continue;
            }
            const auto& s = samples[b];
            const float sign = invertRotation ? -1.0f : 1.0f;
            rotsRig[b] = sr3rig::quatToRotation3(sign * s.rotation.x, sign * s.rotation.y,
                                                 sign * s.rotation.z, s.rotation.w);
            deltasRig[b] = s.translationDelta;
            if (s.hasRotationTrack) ++rotTracks;
            if (s.hasTranslationTrack) ++transTracks;
        }

        std::vector<sr3rig::Mat3x4> skinRig =
            sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);

        // Bone-length-preservation check, done in RIG SPACE - a linear
        // orthogonal mesh-space conjugation cannot change any distance, so
        // checking here is equivalent to checking after conversion.
        double frameMaxDeviation = 0.0;
        size_t frameMaxDeviationBone = static_cast<size_t>(-1);
        for (size_t b = 0; b < bones.size(); ++b) {
            if (bones[b].isRoot()) continue;
            const uint32_t p = bones[b].parentIndex;
            std::array<float, 3> worldB = skinRig[b].transformPoint(restRig[b]);
            std::array<float, 3> worldP = skinRig[p].transformPoint(restRig[p]);
            double dx = worldB[0] - worldP[0], dy = worldB[1] - worldP[1], dz = worldB[2] - worldP[2];
            double animLen = std::sqrt(dx * dx + dy * dy + dz * dz);
            double rdx = restRig[b][0] - restRig[p][0], rdy = restRig[b][1] - restRig[p][1],
                   rdz = restRig[b][2] - restRig[p][2];
            double restLen = std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
            double dev = std::fabs(animLen - restLen);
            if (dev > frameMaxDeviation) {
                frameMaxDeviation = dev;
                frameMaxDeviationBone = b;
            }
            if (dev > overallMaxDeviation) {
                overallMaxDeviation = dev;
                overallMaxDeviationBoneIdx = b;
                overallMaxDeviationTimeIdx = ti;
            }
        }

        std::vector<sr3rig::Mat3x4> skinMesh(bones.size());
        for (size_t b = 0; b < bones.size(); ++b)
            skinMesh[b] = useBonePalette ? sr3rig::conjugateToMeshSpaceInverted(skinRig[b])
                                         : sr3rig::conjugateToMeshSpace(skinRig[b]);

        sr3rig::SkinningStats stats;
        std::vector<std::array<float, 3>> posedPositions = sr3rig::skinVertices(verts, skinMesh, &stats);

        char frameLabel[64];
        std::snprintf(frameLabel, sizeof frameLabel, "_t%zu_frac%.2f", ti,
                     static_cast<double>(frac));
        const std::string framePath = outPrefix + frameLabel + ".png";

        if (!renderer.upload(device.device(), mesh, channelIndex, 0, error, drawGroup,
                             &posedPositions, dupPtr)) {
            std::fprintf(stderr, "posed upload failed at t=%.3f: %s\n", static_cast<double>(t),
                         error.c_str());
            return 1;
        }
        std::vector<uint8_t> framePixels;
        if (!renderOne(framePath, framePixels)) return 1;

        std::printf(
            "frame %zu: t=%.3f (frac=%.2f)  bones-with-rotation=%zu/%zu  "
            "bones-with-translation=%zu/%zu  bone-length max deviation=%.6e at bone %zu (\"%s\") "
            "over %zu bone-pairs  lanesDropped=%lld\n",
            ti, static_cast<double>(t), static_cast<double>(frac), rotTracks, bones.size(),
            transTracks, bones.size(), frameMaxDeviation,
            frameMaxDeviationBone == static_cast<size_t>(-1) ? 0 : frameMaxDeviationBone,
            frameMaxDeviationBone == static_cast<size_t>(-1)
                ? ""
                : bones[frameMaxDeviationBone].name.c_str(),
            bonePairCount, stats.lanesDropped);
    }

    std::printf(
        "\nbone-length-preservation summary: max deviation = %.6e at bone %zu (\"%s\"), "
        "time-sample %zu (t=%.3f), over %zu bone-pairs x %zu sample times = %zu checks\n",
        overallMaxDeviation, overallMaxDeviationBoneIdx == static_cast<size_t>(-1)
                                 ? 0
                                 : overallMaxDeviationBoneIdx,
        overallMaxDeviationBoneIdx == static_cast<size_t>(-1)
            ? ""
            : bones[overallMaxDeviationBoneIdx].name.c_str(),
        overallMaxDeviationTimeIdx == static_cast<size_t>(-1) ? 0 : overallMaxDeviationTimeIdx,
        overallMaxDeviationTimeIdx == static_cast<size_t>(-1)
            ? 0.0
            : static_cast<double>(fractions[overallMaxDeviationTimeIdx]) * duration,
        bonePairCount, fractions.size(), bonePairCount * fractions.size());
    std::printf(
        "NOTE: this check catches arithmetic bugs (composition order, interpolation, a units\n"
        "error) - it CANNOT confirm the animation is not mirrored/inverted/rotating the wrong\n"
        "way, since both a correct and an incorrectly-handed rotation convention preserve\n"
        "rigidity equally (both are orthogonal transforms). A non-zero deviation at a bone whose\n"
        "track carries translation keys is EXPECTED (that bone's pair distance is deliberately\n"
        "animated there), not a bug by itself - see which bone name is reported above.\n");

    return 0;
}

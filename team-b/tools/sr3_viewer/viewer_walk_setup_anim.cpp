// runWalkDemo() split: misc_tables node graph, animation bridge and real locomotion clips.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupAnimRuntime(WalkSession& sess) {
    auto& charactersPath = sess.charactersPath;
    auto& miscTablesPath = sess.miscTablesPath;
    auto& v2 = sess.v2;
    auto& animArchiveOverride = sess.animArchiveOverride;
    auto& miscBytes = sess.miscBytes;
    auto& miscRead = sess.miscRead;
    auto& docOf = sess.docOf;
    auto& states = sess.states;
    auto& actions = sess.actions;
    auto& triggerNames = sess.triggerNames;
    auto& loadIn = sess.loadIn;
    auto& gi = sess.gi;
    auto& pool = sess.pool;
    auto& svc = sess.svc;
    auto& motion = sess.motion;
    auto& clipRegistry = sess.clipRegistry;
    auto& runRecord = sess.runRecord;
    auto& jogRecord = sess.jogRecord;
    auto& wlkRecord = sess.wlkRecord;
    auto& animArchiveBytes = sess.animArchiveBytes;
    auto& realPoseEnabled = sess.realPoseEnabled;
    // ---- misc_tables.vpp_pc: real control-parameter/graph tables -------------
    miscBytes = readFileLargeC(miscTablesPath);
    if (miscBytes.empty()) {
        std::fprintf(stderr, "could not read %s\n", miscTablesPath.c_str());
        return 1;
    }
    sess.miscTop_.emplace(vpp::ByteView(miscBytes.data(), miscBytes.size()));
    auto& miscTop = *sess.miscTop_;
    miscRead = [&](const std::string& name) -> std::optional<std::string> {
        std::vector<uint8_t> out, sib;
        if (!findEntry(miscTop, name, out, sib, std::string()) || out.empty()) return std::nullopt;
        return std::string(reinterpret_cast<const char*>(out.data()), out.size());
    };
    docOf = [&](const std::string& name) -> sr3xtbl::Document {
        auto s = miscRead(name);
        if (!s) throw std::runtime_error("could not find '" + name + "' in " + miscTablesPath);
        return sr3xtbl::ParseDocument(std::string_view(*s));
    };
    sess.paramsDoc_.emplace(docOf("control_parameters.xtbl"));
    const auto& paramsDoc = *sess.paramsDoc_;
    sess.registry_.emplace(sr3animruntime::ControlParameterRegistry::load(paramsDoc));
    const auto& registry = *sess.registry_;
    sess.filtersDoc_.emplace(docOf("control_filters.xtbl"));
    const auto& filtersDoc = *sess.filtersDoc_;
    sess.filters_.emplace(sr3animruntime::ControlFilterTable::load(filtersDoc, registry));
    const auto& filters = *sess.filters_;
    {
        const sr3xtbl::Document d1 = docOf("anim_states.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimStatesTable(d1)) if (s.name) states.push_back(*s.name);
        const sr3xtbl::Document d2 = docOf("anim_actions.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimActionsTable(d2)) if (s.name) actions.push_back(*s.name);
        const sr3xtbl::Document d3 = docOf("anim_triggers.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimTriggersTable(d3)) if (s.name) triggerNames.push_back(*s.name);
    }
    sess.stateActions_.emplace(sr3animruntime::StateActionRegistry::build({}, states, actions));
    const auto& stateActions = *sess.stateActions_;
    sess.triggers_.emplace(sr3animruntime::TriggerRegistry::build(triggerNames));
    const auto& triggers = *sess.triggers_;

    sess.listDoc_.emplace(docOf("node_graph_files.xtbl"));
    const auto& listDoc = *sess.listDoc_;
    loadIn.registry = &registry;
    loadIn.filters = &filters;
    loadIn.stateActions = &stateActions;
    loadIn.triggers = &triggers;
    loadIn.openFile = miscRead;
    sess.lib_.emplace(sr3animruntime::LoadNodeGraphs(listDoc, loadIn));
    const auto& lib = *sess.lib_;
    if (lib.aborted) {
        std::fprintf(stderr, "node graph load aborted at pass %d, file '%s'\n", lib.abortedPass, lib.abortedFile.c_str());
        return 1;
    }
    // Throwaway verification (do not just trust the task brief's own citation
    // of the real name): print the population size, then look the graph up
    // by its real name - which HAS a space ("player state machine", not
    // player_state_machine; confirmed by direct archive inspection in the
    // merged sr3playeranim unit's own verify doc, results/verify_player_
    // anim_bridge_20261007.md - re-confirmed here by actually resolving it).
    std::printf("graphs   : %zu loaded from node_graph_files.xtbl\n", lib.graphs.size());
    gi = lib.findGraph("player state machine");
    if (gi < 0) {
        std::fprintf(stderr, "could not find graph 'player state machine' among %zu loaded graphs\n",
                     lib.graphs.size());
        return 1;
    }
    std::printf("graph    : 'player state machine' found (index %d of %zu loaded)\n", gi, lib.graphs.size());

    // ---- the bridge ------------------------------------------------------------
    // ARBITRARY bookkeeping ids - no real record/duration resolution
    // pipeline exists in this project either (same FakeServices precedent
    // tests/real_anim_node_graph_test.cpp already establishes against the
    // real 249-graph population).
    svc.tagToRecord = [](int32_t tagIndex) { return tagIndex + 100; };
    svc.duration = [](int32_t) { return 1.0f; };
    sess.bridge_.emplace(lib, gi, registry, pool, svc, motion, filters);

    // ---- FIX 4 (--v2 only, real U4 pose compose, 2026-10-07 follow-up task):
    // load a handful of REAL locomotion clips (RealClipRegistry above) from
    // preload_anim.vpp_pc - the SAME directory characters.vpp_pc/
    // misc_tables.vpp_pc/sr3_city_0.vpp_pc already live in (dataRoot, per
    // golden_scene_check.cpp's own convention), derived here rather than
    // taking a new required CLI argument so every pre-existing invocation
    // (including the frozen v1 golden's own command line) keeps working
    // unchanged - this whole block is skipped entirely when --v2 is not
    // given. --anim-archive <path> overrides the derived path.
    if (v2) {
        std::string animArchivePath = animArchiveOverride;
        if (animArchivePath.empty()) {
            const size_t slash = charactersPath.find_last_of("/\\");
            const std::string dir = (slash == std::string::npos) ? std::string() : charactersPath.substr(0, slash + 1);
            animArchivePath = dir + "preload_anim.vpp_pc";
        }
        // NORMALISE before this path is echoed anywhere below: it is
        // DERIVED from charactersPath, an argv-supplied path whose own
        // separator style (backslash/forward-slash) is caller-chosen and
        // varies between a manually-typed invocation and a CMake-
        // registered ctest one - the SAME lesson this command's own FIX 1
        // material-texture logging already learned (see that block's own
        // comment, above). Reading the file works with either style on
        // Windows, so this only affects what gets printed.
        for (auto& ch : animArchivePath)
            if (ch == '\\') ch = '/';
        animArchiveBytes = std::make_unique<std::vector<uint8_t>>(readFileLargeC(animArchivePath));
        if (animArchiveBytes->empty()) {
            std::printf("anim     : could not read %s - real U4 pose compose stays OPEN, falling back to the "
                        "rigid-body render (same as --v2 without this fix)\n",
                        animArchivePath.c_str());
        } else {
            try {
                vpp::Container animContainer(vpp::ByteView(animArchiveBytes->data(), animArchiveBytes->size()));
                runRecord = clipRegistry.load(animContainer, "plym_ps_run.anim_pc");
                jogRecord = clipRegistry.load(animContainer, "plym_ps_jog.anim_pc");
                wlkRecord = clipRegistry.load(animContainer, "plym_ps_wlk.anim_pc");
            } catch (const std::exception& ex) {
                std::printf("anim     : %s open failed: %s - real U4 pose compose stays OPEN\n",
                            animArchivePath.c_str(), ex.what());
            }
            std::printf("anim     : real locomotion clips from %s: run=%s jog=%s wlk=%s\n", animArchivePath.c_str(),
                        runRecord >= 0 ? "loaded" : "MISSING", jogRecord >= 0 ? "loaded" : "MISSING",
                        wlkRecord >= 0 ? "loaded" : "MISSING");
        }
        if (runRecord >= 0 || jogRecord >= 0 || wlkRecord >= 0) {
            // Real tag -> real record, resolved through the SAME
            // StateActionRegistry the diagnostic log already uses (built
            // from real anim_states.xtbl/anim_actions.xtbl rows) - any tag
            // this registry does not recognise keeps the OLD arbitrary
            // fallback unchanged, so it is safely skipped downstream
            // (ClipSource::lookup returns nullptr for it, never fabricated).
            svc.tagToRecord = [&stateActions, runRecord, jogRecord, wlkRecord](int32_t tagIndex) -> int32_t {
                std::string name;
                if (tagIndex >= 0 && static_cast<size_t>(tagIndex) < stateActions.names().size())
                    name = stateActions.names()[static_cast<size_t>(tagIndex)];
                for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (name == "run" && runRecord >= 0) return runRecord;
                if (name == "jog" && jogRecord >= 0) return jogRecord;
                if (name == "walk" && wlkRecord >= 0) return wlkRecord;
                return tagIndex + 100;  // UNCHANGED fallback - no real clip, safely skipped by the U4 bridge
            };
            svc.duration = [&clipRegistry](int32_t record) -> float {
                const sr3animpose::ResolvedClip* rc = clipRegistry.lookup(record);
                // spec-anim-runtime.md Sec6.1: duration in seconds = clip
                // header word +0x06 / 30 - the SAME conversion
                // sr3animpose::ClipTimeUnitsFromSeconds() inverts.
                return rc != nullptr ? static_cast<float>(rc->animation->durationTotal()) / 30.0f : 1.0f;
            };
        }
    }
    realPoseEnabled = v2 && (runRecord >= 0 || jogRecord >= 0 || wlkRecord >= 0);
    return -1;
}

}  // namespace sr3viewer

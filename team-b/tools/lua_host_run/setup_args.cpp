// lua_host_run main() split: usage text, option parsing and the positionals (parseArgs).
// Code moved verbatim out of the former single-function main() (see lua_host_run_session.h).
#include "lua_host_run_session.h"

namespace luahostrun {

int parseArgs(LuaHostRunSession& sess, int argc, char** argv) {
    auto& preloadStatesSpec164 = sess.preloadStatesSpec164;
    auto& hostRng = sess.hostRng;
    auto& hostRngSeed = sess.hostRngSeed;
    auto& displayInit = sess.displayInit;
    auto& displayWidth = sess.displayWidth;
    auto& displayHeight = sess.displayHeight;
    auto& missionTickBudget = sess.missionTickBudget;
    auto& probeUnresolvedNames = sess.probeUnresolvedNames;
    auto& vintDocs = sess.vintDocs;
    auto& probeAutopilot = sess.probeAutopilot;
    auto& autoAnswerHudPrompts = sess.autoAnswerHudPrompts;
    auto& hudPromptAutoAnswerDelayTicks = sess.hudPromptAutoAnswerDelayTicks;
    auto& probeAutopilotAutoRaisedRingSize = sess.probeAutopilotAutoRaisedRingSize;
    auto& cacheDir = sess.cacheDir;
    auto& registrationListPath = sess.registrationListPath;
    auto& outDir = sess.outDir;
    auto& toolsDir = sess.toolsDir;
    auto& maxScriptsToRun = sess.maxScriptsToRun;
    auto& t0 = sess.t0;

    if (argc < 4) {
        std::cerr << "usage: lua_host_run <cache_dir> <registered_tagged.txt> <out_dir> [max_scripts_to_run]\n"
                     "                    [--preload-states=spec16.4|tag]\n"
                     "  --preload-states=spec16.4  (default) run the spec-lua-bindings.md Sec16.4 preloads only in\n"
                     "      the state Sec16.4 names (vint_lib/game_ui_globals/vdo_base_object/vdo_anim_object/\n"
                     "      vdo_input_tracker: ui only; game_lib: gameplay only; system_lib: both). CONFIRMED and\n"
                     "      cleared for implementation 2026-10-01 (Sec16.1/Sec16.4). spec16.4-highconf is accepted\n"
                     "      as an alias.\n"
                     "  --preload-states=tag  the old behaviour: these files follow the per-script state tag like\n"
                     "      any other script (both states when the tag is OPEN).\n"
                     "  --host-rng[=SEED]  HYPOTHESIS / HOST SUBSTITUTE, off by default: rand_int/rand_float draw\n"
                     "      from a ring filled once by a deterministic host generator (default seed 1) instead of\n"
                     "      the CONFIRMED file-image ring, where every draw returns lo until a fill (the engine\n"
                     "      generator and fill are OPEN, spec-lua-api-behaviour.md Sec26.27). Values are not the\n"
                     "      game's. Recorded as host_rng_option= in verdict_summary.txt.\n"
                     "  --display=WxH | --display=none  the display resolution handed to the UI subsystem init\n"
                     "      0x00e23910 at front-end bring-up (spec-lua-api-behaviour.md Sec26.26): the width/height\n"
                     "      globals and the per-thread record vint_is_std_res reads, the display mode and the layout\n"
                     "      index. A HOST INPUT, not an engine value: default 1280x720 (CHOSEN, the spec's first\n"
                     "      worked example); none = the init does not run and those values stay OPEN. Recorded as\n"
                     "      display_option= in verdict_summary.txt.\n"
                     "  --tick-budget=N  CHOSEN override of the mission-drive loop's per-mission tick budget\n"
                     "      (default 20). One tick models kFadeHostMsPerTick (333 ms, CHOSEN) of this host's own\n"
                     "      simulated time (see that constant's own doc comment for the full cadence reasoning).\n"
                     "      Raise this to distinguish a 'still suspended' mission that is a real blocker from one\n"
                     "      that is just mid-way through a long, legitimate wait; mission_simulated_seconds_budget=\n"
                     "      in verdict_summary.txt reports what this host modelled, and start_still_suspended_at=\n"
                     "      (per-mission output / TSV) reports where it ended up parked.\n"
                     "  --auto-answer-hud-prompts[=N]  THIS TASK (hud-prompt-autoanswer-harness): each tick,\n"
                     "      any live hud_prompt(target, promptId, callbackName) request (Sec13.3) that has been\n"
                     "      showing for at least N ticks (default 15) is answered by spawning a coroutine for\n"
                     "      its own callbackName - the SAME BareThreadTable allocate()-then-let-the-scheduler-\n"
                     "      resume-it mechanism --probe-autopilot already uses for on_trigger/on_death/etc,\n"
                     "      reused here for hud_prompt. Generic over every currently showing prompt, not\n"
                     "      hardcoded to M05_JUMP. Fires LIVE during the normal tick loop (not a post-hoc probe\n"
                     "      like --probe-autopilot) so a mission blocked only on a HUD-prompt wait can keep\n"
                     "      advancing within its own --tick-budget= instead of tick-budget-stalling on it\n"
                     "      forever and masking whatever its own NEXT real stall point is.\n"
                     "  --probe-unresolved-names  DIAGNOSTIC ONLY, off by default, NEVER a real measurement: a\n"
                     "      named-object resolution miss (the OPEN gate behind most 'Killbane'-style blockers)\n"
                     "      returns a synthetic \"resolves\" placeholder instead of refusing, so a mission can\n"
                     "      keep running past it to show what blocks it NEXT. Every probed (synthetic) name is\n"
                     "      listed in verdict_summary.txt as probed_unresolved_names=; every other OPEN item a\n"
                     "      mission still hits past that point (another named-object check, a per-character/\n"
                     "      per-vehicle field, an unimplemented stub) is still genuinely OPEN and reported as\n"
                     "      such - this flag widens exactly one gate, nothing else.\n"
                     "  --no-vint-docs  turn OFF real .vint_doc loading (on by default since 2026-10-03): every\n"
                     "      .vint_doc found is parsed (layout CONFIRMED, spec-vint-doc-format.md) and, the first\n"
                     "      time a chunk named by a document's lua_script_file runs (or a hook it defined fires),\n"
                     "      loaded into the Vint object registry, with that document as the call's current\n"
                     "      document (CHOSEN association). Recorded as vint_docs= in verdict_summary.txt.\n"
                     "  --trace-host-calls-before-error=N  DIAGNOSTIC ONLY, off by default (N=0): on any\n"
                     "      mission-thread Lua error (the initial _start call or a later scheduler-pass resume),\n"
                     "      print to stderr, prefixed [trace-host-calls], the last N global calls (name only,\n"
                     "      via LUA_HOOKCALL) made during that specific resume before it errored. The ring resets\n"
                     "      fresh before every individual resume and is discarded unless that resume's own call\n"
                     "      ends in a Lua error, so it always reflects what led to one specific error, never a\n"
                     "      stale trace from an unrelated resume. A mission classified still_suspended at budget\n"
                     "      exhaustion instead prints TWO rings (2026-10-07): its own LAST resume (what its wait\n"
                     "      loop polls right now) and its own FIRST resume (its one-time _start-call setup, e.g.\n"
                     "      a trigger_enable/on_trigger/on_vehicle_enter callback registration, before it ever\n"
                     "      reached that wait loop) - the first-resume ring is captured once, right when\n"
                     "      callMissionStart's single resume of that coroutine returns, and is never overwritten\n"
                     "      afterward, unlike the last-resume ring. Never alters real engine behavior.\n"
                     "  --probe-autopilot  DIAGNOSTIC ONLY, off by default, NEVER on by default, NEVER changes\n"
                     "      baseline drive behavior: for EVERY mission classified still_suspended at budget\n"
                     "      exhaustion, checks that mission's own first-resume trace (g_firstResumeTraceRings,\n"
                     "      same data --trace-host-calls-before-error=N already prints). If it shows a CONFIRMED\n"
                     "      callback-registration native (on_trigger/on_trigger_exit/on_death/\n"
                     "      on_vehicle_destroyed/on_attack_performed/on_take_damage/on_revived/\n"
                     "      on_qte_animation_trigger - see isCallbackRegistrationNative's own doc comment for the\n"
                     "      full citation list, and which of this flag's own candidate natives were checked and\n"
                     "      EXCLUDED, e.g. trigger_enable has no callback argument at all) with its callback-name\n"
                     "      argument recovered, this flag spawns a new coroutine for that one Lua global, zero\n"
                     "      arguments (no spec section establishes what arguments a FIRED, as opposed to\n"
                     "      registered, callback receives - a CHOSEN, flagged judgment call, not invented state),\n"
                     "      matching the real dispatcher's own confirmed mechanism (spec-lua-api-behaviour.md\n"
                     "      Sec1.14, 2026-10-09: spawn via BareThreadTable::allocate(), never a synchronous\n"
                     "      lua_call/lua_pcall) - it is NOT resumed here; only the follow-up window's scheduler\n"
                     "      pass(es), below, resume it. Tagged SYNTHETIC in [probe-autopilot] stderr output and in\n"
                     "      verdict_mission_drive.tsv/summary, then runs a\n"
                     "      bounded follow-up window to see what breaks NEXT. If no such native is recoverable,\n"
                     "      records \"nothing to synthesize\" explicitly rather than a silent empty result - itself\n"
                     "      a real finding (the wait is most likely satisfied by polled world state, not a\n"
                     "      callback). Auto-raises --trace-host-calls-before-error to 200 if not already set (so\n"
                     "      this flag is self-sufficient), printed to stdout so that is never silent either.\n";
        return 1;
    }
    // Optional flags anywhere after the 3 positionals; the remaining
    // positional (if any) is max_scripts_to_run.
    std::vector<std::string> extraPositional;
    for (int a = 4; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--preload-states=spec16.4" || arg == "--preload-states=spec16.4-highconf") preloadStatesSpec164 = true;
        else if (arg == "--preload-states=tag") preloadStatesSpec164 = false;
        else if (arg.rfind("--tick-budget=", 0) == 0) {
            std::string v = arg.substr(14);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos || std::stol(v) <= 0) {
                std::cerr << "bad --tick-budget value (want a positive integer): " << v << "\n";
                return 1;
            }
            missionTickBudget = std::stoi(v);
        }
        else if (arg == "--probe-unresolved-names") probeUnresolvedNames = true;
        else if (arg == "--probe-autopilot") probeAutopilot = true;
        else if (arg == "--auto-answer-hud-prompts") autoAnswerHudPrompts = true;
        else if (arg.rfind("--auto-answer-hud-prompts=", 0) == 0) {
            std::string v = arg.substr(std::string("--auto-answer-hud-prompts=").size());
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << "bad --auto-answer-hud-prompts value (want a non-negative integer): " << v << "\n";
                return 1;
            }
            autoAnswerHudPrompts = true;
            hudPromptAutoAnswerDelayTicks = std::stoi(v);
        }
        else if (arg == "--no-vint-docs") vintDocs = false;
        else if (arg.rfind("--trace-host-calls-before-error=", 0) == 0) {
            std::string v = arg.substr(32);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << "bad --trace-host-calls-before-error value (want a non-negative integer): " << v << "\n";
                return 1;
            }
            g_traceRingSize = std::stoul(v);
        }
        else if (arg == "--host-rng") hostRng = true;
        else if (arg.rfind("--host-rng=", 0) == 0) {
            std::string v = arg.substr(11);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << "bad --host-rng seed: " << v << "\n";
                return 1;
            }
            hostRng = true;
            hostRngSeed = std::stoull(v);
        } else if (arg == "--display=none") {
            displayInit = false;
        } else if (arg.rfind("--display=", 0) == 0) {
            const std::string v = arg.substr(10);
            const size_t x = v.find('x');
            const std::string ws = x == std::string::npos ? std::string() : v.substr(0, x);
            const std::string hs = x == std::string::npos ? std::string() : v.substr(x + 1);
            auto digits = [](const std::string& s) {
                return !s.empty() && s.size() <= 6 && s.find_first_not_of("0123456789") == std::string::npos;
            };
            if (!digits(ws) || !digits(hs) || std::stoi(ws) == 0 || std::stoi(hs) == 0) {
                std::cerr << "bad --display value (want WxH, e.g. 1920x1080, or none): " << v << "\n";
                return 1;
            }
            displayInit = true;
            displayWidth = std::stoi(ws);
            displayHeight = std::stoi(hs);
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        } else extraPositional.push_back(arg);
    }
    // --probe-autopilot setup (2026-10-07): enable the capture gate, and
    // auto-raise the trace ring to a size this project's own earlier pass
    // (results/trace_host_calls_firstresume_20261007.md) already measured
    // as large enough to avoid truncating a mission's real first-resume
    // sequence (that pass's own =15 run hit the cap for 3 of 6 missions;
    // =200 did not, for any of them) - not re-chosen here, reused. Only
    // when the caller didn't already pass their own
    // --trace-host-calls-before-error=N, so an explicit N is never
    // silently overridden; printed to stdout (below, with the rest of
    // this run's own option echo) so the auto-raise is never silent.
    g_probeAutopilotEnabled = probeAutopilot;
    if (probeAutopilot && g_traceRingSize == 0) {
        g_traceRingSize = kProbeAutopilotAutoRingSize;
        probeAutopilotAutoRaisedRingSize = true;
    }
    cacheDir = argv[1];
    registrationListPath = argv[2];
    outDir = argv[3];
    // The two further tools/ inputs below are read from the same directory as
    // the registration list (cloud phase, 2026-09-30: the PC bridge runs tools
    // with cwd = the job's output dir, so a bare "tools/..." would not
    // resolve). Unchanged for the usual `tools/lua_all_registered_*.txt` run
    // from team-b/.
    toolsDir = fs::path(registrationListPath).parent_path();
    maxScriptsToRun = extraPositional.empty() ? UINT64_MAX : std::stoull(extraPositional[0]);
    fs::create_directories(outDir);

    t0 = std::chrono::steady_clock::now();
    return -1;
}

} // namespace luahostrun

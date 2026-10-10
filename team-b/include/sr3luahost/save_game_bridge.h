// Bridges EngineState::saveGame()/playerCash() (the live, mutable world
// state this project models) to the real sr3save_NN.sr3s_pc writer/reader
// (sr3save::SaveSnapshot, include/sr3save/save_snapshot.h) - the implement-
// ation side of spec-save-format.md Sec15's three confirmed save triggers:
//
//   - mission_autosave / game_autosave (Sec15.2): both real Lua natives
//     funnel through the SAME real save entry (0x00B94A10, Sec6.3) as the
//     menu "Save Game" action (Sec15.1, out of this project's scope - no
//     UI) - writeAutosaveSnapshot() below is that entry's "file write"
//     branch (Sec6.3: hash, then an async platform write).
//   - mission_set_checkpoint (Sec15.3/Sec11.2): the SAME save entry's OTHER
//     branch - "a pointer to caller-owned memory... no hash, no file
//     write" - buildCheckpointSnapshotBytes()/applyCheckpointSnapshot()
//     below.
//
// Both branches share the one real layout (sr3save::SaveSnapshot::build()),
// which is why this file is a thin bridge, not a second writer: see
// save_snapshot.h's own SaveSnapshotFields for exactly which spans are
// populated from live state and which stay at the format's own zero-fill
// placeholder.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sr3save/save_snapshot.h"

namespace sr3luahost {

class EngineState;

// A clearly-labelled scratch location under the OS temp directory,
// qualified by this process's own id so two test binaries (or two real
// game-process runs) started around the same time never race on writing
// the same slot file (spec-save-format.md Sec8.2's own practical note:
// autosave is always slot 0, "1 in slot 0 of every real directory" -
// writeAutosaveSnapshot() below always targets that one slot, matching the
// real game's own convention). Checked before adding this: EngineState had
// no prior save-path concept (save_browser.cpp instead takes a directory as
// its own CLI argument) - this is this project's own choice, not a spec
// fact, and every write through it still passes sr3io::isOutputPathSafe()
// (output_path_guard.h), which independently refuses the real game-install
// roots regardless of this default.
std::string defaultSaveGameDirectory();

// Gathers EngineState::saveGame()/playerCash() into the writer's own input
// struct (save_snapshot.h's SaveSnapshotFields) - the live-state half of
// the bridge. `saveTime`/`saveDate` are filled from the real OS wall clock
// at call time (the same source spec-save-format.md Sec6.3 says the real
// header serializer uses - the C runtime's time/date calls), matching the
// real format's own convention rather than inventing a fixed placeholder;
// no other field depends on wall-clock time, so this does not affect the
// round-trip of any world-state field a test cares about.
//
// Takes a non-const reference: EngineState follows this project's own
// established convention (engine_state.h, e.g. playerCash()/saveGame()
// themselves) of exposing every field through a single mutable accessor
// rather than a separate const/non-const pair, so a read-only helper still
// needs non-const access to call them.
sr3save::SaveSnapshotFields buildSaveSnapshotFields(EngineState& st);

// Applies a parsed snapshot's modeled fields back into live EngineState
// state (the load-side mirror of buildSaveSnapshotFields() above) - cash
// via EngineState::playerCash() (dollar units, matching cashDiv100()),
// everything else via EngineState::saveGame(). Deliberately does NOT touch
// anything this project has no live counterpart for (position, inventory
// beyond collectibles, vehicle/garage state - see save_game_state.h's own
// top note for why) and does NOT use activeMissionLabelIndex to resume a
// mission (Sec15's own task brief: display-only on load).
void applySaveSnapshotFields(const sr3save::SaveSnapshot& snapshot, EngineState& st);

// mission_autosave/game_autosave's real file-write branch (Sec6.3's "hash,
// then queue an async platform write," modeled here as a synchronous write
// since this project has no async I/O layer to model). Always writes slot 0
// (the real directory's own autosave-slot convention, Sec8.2) under
// st.saveGameDirectory(), through sr3io::isOutputPathSafe() (never throws -
// a refusal or an I/O failure is reported via `error` and
// EngineState::lastAutosaveError(), exactly like every other "no
// affordability check, but still can't corrupt a file outside its own
// lane" writer in this project, src/wav_writer.cpp/src/png_writer.cpp).
// Returns true and sets `outPath` to the written file's real path on
// success; on failure, `outPath` is left untouched and `error` explains
// why. Also bumps EngineState::autosaveFileWriteCount()/
// lastAutosaveFilePath() on success - test/observability counters, not a
// spec fact.
bool writeAutosaveSnapshot(EngineState& st, std::string& outPath, std::string& error);

// mission_set_checkpoint's real in-memory branch (Sec11.2/Sec15.3): builds
// the SAME layout with computeHash=false (the real "no hash" buffer) and
// returns it - never writes a file. The caller (the stub) is expected to
// store the result in EngineState::lastCheckpointSnapshot() rather than
// discard it (this task's own step 4) - done here as a free function
// instead of writing EngineState directly so a caller can inspect the
// bytes before deciding whether to keep them.
std::vector<uint8_t> buildCheckpointSnapshotBytes(EngineState& st);

// The checkpoint restore path's mirror (Sec11.2: "the restore... feeds that
// pointer to the load gate"): parses EngineState::lastCheckpointSnapshot()
// (if present) and applies it via applySaveSnapshotFields(). Returns false
// (and leaves `st` untouched) if no checkpoint has been set yet, or if the
// stored bytes somehow fail to parse (defensive only - buildCheckpoint
// SnapshotBytes() always produces a parseable buffer, so this should never
// actually happen outside a test deliberately corrupting the stored bytes).
bool applyCheckpointSnapshot(EngineState& st);

// The file-load path's mirror of writeAutosaveSnapshot() above (Sec11.1's
// own load chain, "request... read... apply": this project models the
// read+apply steps directly, since there is no slot-descriptor/menu layer
// in scope). Reads `path` from disk, parses it as a real snapshot (per
// SaveSnapshot::parse()'s own rules - a hash mismatch or an off-by-version
// value is reported via the parsed snapshot's own hashValid()/
// versionSupported(), never rejected outright, exactly matching Sec11.1's
// "the game never verifies this on load" finding), and applies it via
// applySaveSnapshotFields(). Returns false (and leaves `st` untouched) on
// any I/O or format failure, with `error` explaining why - never throws.
bool loadSaveSnapshotFile(const std::string& path, EngineState& st, std::string& error);

} // namespace sr3luahost

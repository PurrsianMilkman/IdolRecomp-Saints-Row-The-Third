# Real .vint_doc document-model loading merge verification (2026-10-03)

Verified independently against a fresh build (`build_verify_vintdoc_merge`), separate from the implementing
agent's own dev build (`build_verify_vintdoc_final`). Clean merge into `main` (521cf90), no conflicts
(the 3-way auto-merge touched CMakeLists.txt/engine_state.h/lua_engine_state.cpp/lua_spec_confirmed_stubs.cpp
but resolved without any manual intervention).

`ctest -C Release`: **58/58 passed** (up from 56 — the new `synthetic_vintdoc_document_test` and
`real_vintdoc_document_test` both registered and ran, the latter not skipped, confirming the real archive
was found and exercised, not just compiled).

Real mission drive (`results/verify_vintdoc_merge_mission_drive/`, docs-on by default): aggregate numbers
are **identical** to the pre-merge baseline: `missions_with_start_call_ok=48/49`,
`missions_with_start_suspended=47/49`, `still_suspended:34 finished:8 errored:5`. Consistent with the
implementing agent's own finding — real `.vint_doc` loading closes 1,217 "current default vint document is
OPEN" hook refusals outright, but those hooks now simply progress further and park at their own next real
blocker (a document time-index OPEN item, a pre-existing test-harness gap unrelated to the document model,
or an already-catalogued nil-value error) rather than changing any mission's own start/suspend/finish
outcome — not a regression, the document model genuinely has nothing further to unblock at the
mission-drive level with this project's current scope.

Core parser (`sr3vintdoc::parseDocument`, `src/vint_doc.cpp`) reviewed directly: bounds-checked cursor
reads throughout (no unchecked pointer arithmetic), explicit `FormatError` on any malformed structure
rather than reading past it, depth/record-count guards against a hostile child count. Header field offsets
cross-checked against the 67455c3 spec commit (secondaryOffsetRaw read at header `+0x16`, matches).
`EngineState::loadVintDocument()`'s property-tag-to-Lua-value mapping (signed/unsigned 32-bit, float,
string, bool, vec3, vec2) matches the CONFIRMED tag widths from 67455c3. `Host::setDocumentContextResolver`'s
RAII `DocumentContextScope` correctly saves/restores the previous `currentDefaultDocHandle_` state
regardless of what happens inside the scope, including the nested-hook-firing case.

Verdict: merge `521cf90` is sound.

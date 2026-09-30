# Read before using the Lua roster files in this folder

Applies to `lua_all_registered_1430_tagged.txt`, `…_1435_tagged.txt`, `…_1490_tagged.txt`,
`lua_reconciliation_called_and_registered_1181.tsv`, `lua_reconciliation_called_not_registered_101.tsv`
and `lua_reconciliation_registered_not_called_249.tsv`. The files themselves are left unchanged (other tools
parse them); these notes correct what they cannot express. Added 2026-09-30 (cloud phase) from
`spec-lua-bindings.md` and its desk review.

## 1. One cluster per name is not the whole truth: 8 names are in BOTH states (§13.5)

The tag files record one registrar call site, one cluster, per name. `spec-lua-bindings.md` §13.5 confirms 8
names registered into **both** Lua states (same native function from two registrars), all tagged `gameplay`
here: `audio_object_post_event`, `game_is_active_input_gamepad`, `hud_display_set_element`, `audio_stop`,
`hud_display_create_state`, `hud_display_commit_state`, `hud_display_remove_state`, `coop_is_active`.
`sr3luahost` registers all 8 into both states regardless of the tag (`src/lua_host.cpp`; pinned by
`tests/synthetic_luahost_roster_test.cpp`). Any ranking or census built from these files alone (e.g. the
`1181` tsv) under-counts their UI-state side.

## 2. The 1,430 roster predates the 5th registrar (§13.7)

`lua_reconciliation_called_not_registered_101.tsv` was built against the 1,430-name roster. §13.7 later found a
5th registrar of 55 `vint_*` names (UI state), included in the 1,490 file. So `vint_*` rows in the `101` tsv
(`vint_set_property`, `vint_object_find`, `vint_is_std_res`, …) **are registered** natively, in the UI state.
Use the 1,490 file.

## 3. Not every Lua-visible engine global is in the 1,490 list: the 24 bare globals (§13.2/§16.3) — PENDING

A registrar at `0x00e0f900` installs 24 bare globals. They are **not** all Lua math, and they are **not** stock
Lua 5.1 (stock Lua has `math.floor`, not bare `floor`): §16.3 puts a non-math engine function among them, and
scripts call bare `max`, `floor`, `abs`, `rand_int`, `rand_float`, `round`, `debug_print`. Those rows in the
`101` tsv are therefore **engine globals with an unknown exact roster, not "Lua stdlib, ignore"**. The roster is
queued as a Team A job against the executable; `sr3luahost` deliberately registers none of them until then
(calls fail as nil globals, visibly), pinned by the roster test.

## 4. Hook names (§4) — evidence labels

Not a roster file, but the same kind of caveat: the host's Group 1 hook list mixes names re-confirmed by §8 with
names from §4's automated scan only, which the 2026-09-30 desk review marks OPEN. Each hook now carries
`HookEvidence` (`include/sr3luahost/hook_registry.h`): 42 `sec4_scan_pending_recheck`, 5
`refuted_sec14_3_still_fired`, the rest `sec8_confirmed`. `lua_host_run` writes it as the last column of
`verdict_hook_fires_by_hook.tsv`. Nothing was removed.

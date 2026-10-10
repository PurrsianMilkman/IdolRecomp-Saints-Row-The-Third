# Saints Row: The Third — HUD Runtime Data Flow (`SaintsRowTheThird.exe`)

**Scope.** Traces the live-data-feed half of the HUD: for health, ammo, money, notoriety, and the
minimap, where the live value lives in memory, what writes it, what reads it to push it onto the HUD
(getter / per-frame poll / event-callback), and the real update cadence. The STATIC rendering/document
side (how a widget is laid out and drawn) is covered by `spec-vint-render.md`/`spec-vint-doc-format.md`
and is not re-derived here; individual Lua-native bodies cited below are documented in full in
`spec-lua-api-behaviour.md` and are cross-referenced, not duplicated.

**Method.** Desk-review of already-published disassembly in `spec-lua-api-behaviour.md` (re-examined,
not re-derived where a prior pass already did the work), plus fresh queries against this project's
Ghidra server for the xref/caller-census work no prior pass had done. No leaked Volition/Havok/
Audiokinetic/RAD material. All addresses plain hex; `FUN_`/`DAT_` are bare evidence-anchor names, never
treated as real identifiers.

**Confidence key:** **CONFIRMED** (disassembly/decompile read directly), **HIGH** (inferred from
structure, not independently proven by tracing a render-side consumer), **HYPOTHESIS** (plausible,
materially uncertain), **OPEN** (not resolved — reported honestly rather than guessed).

## 1. The two generic HUD-widget pools — ruled out as the core-stat path

**Neither of this engine's two generic, numbered-slot HUD-widget pools is a dedicated channel for the
always-on core stats (health/ammo/money/notoriety/minimap) — both are shared, reusable scratch space
borrowed by whichever transient minigame/QTE/diversion overlay is currently active.** A renderer should
NOT wire any core stat to either system.

**`hud_display_*` family** (4 distinct native bodies behind 8 dual-registered names, per this project's
already-closed "8 overlaps" finding): `hud_display_set_element` (`0x00a4ed30`) takes 3 plain numbers
(`slotId, subIndex, value`) — no string argument at all — and calls `0x007efd80`, which, for
`slotId < 20` with that slot's active flag set, writes into a 20-slot value array (`0x022b7058`, stride
`0x10`, 12 sub-fields) gated by an active-flag array (`0x022b7095`, stride `0x40`). `hud_display_create_state`
(`0x00a4f0c0`) allocates only from indices 15-19 (`0x007efae0`); indices 0-14 are reserved for direct
C-engine use. `hud_display_commit_state`/`hud_display_remove_state` manage a shared 5-deep
pending-transition queue (`0x007efb40`/`0x007efbd0`) over the same pool.

A full xref census of the direct writer `0x007efd80` found **28 call sites in 15 distinct functions**
beyond the native itself (plus a 5th, previously-unidentified thin Lua entry point, `0x00a26bc0`,
carrying the same `"Lua script HUD"` debug label `hud_display_commit_state` uses — not chased further).
Every one of the other 14 callers carries a debug-label string identifying it as a transient overlay,
never a core stat:

| Caller | Debug label / identity |
|---|---|
| `0x004f1440` | `"Player_Ai"` — state machine on `[obj+0x500]` |
| `0x0060c150` / `0x0060d000` / `0x0060dd60` | QTE (shared slot global `0x012ed1a0`) |
| `0x00644df0` | `"trail_blazing"` diversion |
| `0x006726f0` | `"Zombie"` |
| `0x0067ae40` | `"Mashing Hud"` button-mashing minigame |
| `0x00689bf0` | `"escort_minigame"` |
| `0x006dd3d0` | `"Mission Action Sequence"` |
| `0x006ee130` | unlabeled, not fully read — OPEN, doesn't change the verdict |
| `0x00985760` | `"Melee Active Attack"` finisher prompt |
| `0x00b70720` | `"Satellite Drone"` controller singleton `0x0130eee8` |

**`hud_bar_*` family** (`hud_bar_off`/`hud_bar_on`/`hud_bar_set_value`, `0x00a47010`/`0x00a4d9e0`/
`0x00a4dc20`) indexes a separate 4-slot array (`0x022b6828`). A full xref census of the slot resolver
`0x007eb210` found exactly 2 real C-engine callers: `0x0061fce0` (a generic bootstrap setting up all 4
slots, no stat-specific binding) and `0x0068a560` (the Escort diversion's own dual mood-meter,
hardcoding slots 0/1, literal strings `"ACT_ESCORT_PLEASURE"`/`"ACT_ESCORT_COURAGE"`/`"ACT_ESCORT_RAGE"`/
`"ACT_ESCORT_FOOTAGE"` — not a core stat).

**[CONFIRMED — disassembly/decompile for the architecture and all 15 real callers' identities except
`0x006ee130` (unread) and `0x00a26bc0`'s own registered Lua name (not chased) — neither changes the
verdict.]**

## 2. Health

**Live storage, CONFIRMED.** Character-record fields (shared record family for player/followers/NPCs):
`+0x1cb8` (`float`, current HP), `+0x1cac` (`int`, current max cap), `+0x1cb0` (`int`, base max — cap
changes add a delta onto this, not the cap directly). Vehicle equivalent (distinct record):
`+0x1560`/`+0x155c`/`+0x1564` = min/max/current.

**Writer.** The damage-application engine subtracts the final (post-multiplier-chain) damage amount
directly from `+0x1cb8`. When the result drops to or below a fixed threshold constant (`0x0125e270`,
plausibly `0.0`), it triggers a ragdoll/react call (`0x009bc0d0`) then a further call (`0x00963530`) —
plausibly the death trigger. A DIFFERENT address, `0x0096f8c0`, is separately on record elsewhere as
"very plausibly the death-trigger path" for `set_current_hit_points`'s own callers — **not reconciled;
either two genuine death-trigger entry points exist, or one wraps the other — OPEN.** Lua mutators:
`set_current_hit_points` (`0x00a5f200`, 3 dispatch paths: player/follower/character, vehicle, and an
unidentified 4th resolver kind, clamped against `+0x1cac`) and `set_max_hit_points` (`0x00a5f340`, the
Lua argument is added as a delta onto `+0x1cb0`, preserving the current/max ratio unless an override
bool is passed).

**Reader / HUD push, HIGH CONFIDENCE, not independently traced to a widget.** `get_current_hit_points`
(`0x00a4bb00`, 20 real Lua call sites) / `get_max_hit_points` (`0x00a4afe0`) are pure-query natives with
a 5-path resolver (generic `#PLAYER#`, `#FOLLOWER#`, current-mount character via two vtable `+0x70`
hops, vehicle via `0x00a77e10`/`0x00a77e20`, an unidentified named-placed-object resolver) — the SAME
getter answers "health" for whatever the resolver succeeds on. **This is the only concrete Lua-reachable
bridge found between the live `+0x1cb8` field and anything a HUD script could call.** Neither
`hud_display_*` nor `hud_bar_*` (§1) is wired to this field by any C-side code. Most plausibly a Lua UI
script polls these getters on some cadence and writes into a VINT element's own property — the specific
script, element, and exact cadence (every frame vs. a UI tick vs. a damage-event callback) were **not
traced — OPEN.** 20 static call sites is consistent with "one or two central HUD-update functions," an
inference from call-site count, not a traced cadence.

## 3. Ammo (current weapon's clip/reserve)

**Live storage, CONFIRMED existence/location, OPEN field mapping.** `ammo.xtbl`'s loader (`0x00B6F170`
via `0x00B6E3B0`) builds a runtime ammo-state circular list, `0x028CDC44`, 16-byte entries
(`{pointer to static ammo record, 0, 0, 1.0}`) — HIGH CONFIDENCE per-player reserve/clip state, but
which of the three trailing dwords is clip vs. reserve vs. something else was never traced, in this
pass or a prior one. A separate, densely bit-packed per-character/per-weapon inventory-slot record
(located via `0x008dc820`, hidden `this = weapon_def+0x1b78`) is confirmed as what
`inv_weapon_add_temporary`/`inv_weapon_remove_temporary` write an ammo count and flag bits into — also
not field-mapped at the bit level.

**Writer.** `inv_weapon_add_temporary` (`0x00a510c0`, 88 calls/23 scripts) and
`inv_weapon_remove_temporary` (`0x00a50d00`, 78 calls/19 scripts) converge on the inventory-slot
machinery above (weapon-definition table `0x028dc4dc`, stride `0x7ec`; gate `0x008addb0` selects a
network-record branch vs. direct-apply). Firing, reloading, and `inv_item_add_ammo` are the obvious
additional real-world writers by name/subsystem, not independently traced to the same fields.

**Reader / HUD push, CONFIRMED mechanism (event-driven, not per-frame), OPEN internals.**
`game_hud_update_inventory` (`0x00841dc0`) is the dedicated entry point: if a local player exists, it
calls exactly one further function, `0x007f2650(player)` — nothing else. **259 real Lua call sites**
place this among the most pervasively-called natives in the whole corpus, invoked from the overwhelming
majority of scripted actions that could change the displayed loadout (pickup, fire, reload, switch,
purchase), not a per-frame loop. `0x007f2650` walks an indexed inventory-slot table and does a
current-vs-previous comparison (`0x00941f40`) **before any mutation** — a genuine conditional, on-change
rebuild. **Verdict: ammo's HUD cadence is CONFIRMED event-driven/on-change, not per-frame.** The exact
field reads inside `0x007f2650` (which trailing dword maps to clip vs. reserve) were not decompiled —
**OPEN**, not guessed.

## 4. Money (cash on hand)

**Live storage, HIGH CONFIDENCE, explicit caveat.** The local-player object pointer lives at global
`0x0262EDFC` (accessor `0x009DA4E0`, ~2,240 call sites project-wide — the same accessor every other
player-scalar query uses). `spec-save-format.md`'s entry-1 writer documents a SAVE-SNAPSHOT offset
`+0x4A70` as "cash on hand ×100" — **this report does NOT treat `+0x4A70` as the confirmed LIVE runtime
offset**: the same save-format document shows, for a neighboring field (`+0x4A60`), that the snapshot
offset and the live runtime offset are demonstrably different numbers (`+0x1B70` live) at least once on
this exact record. Whether `+0x4A70` happens to coincide with the live cash offset was not independently
checked. **OPEN for the exact live byte offset.**

**Writer.** `cash_add` (`0x00a42ef0`, 10 calls/3 scripts) and its sibling `cash_remove`: resolve the
target, scale the raw Lua amount ×100, saturate to `±2,000,000,000` via a clamp helper (cited at two
different addresses in two different passes of this project, `0x009601d0` vs. `0x00960160` — **not
reconciled, flagged as a possible single-function citation mismatch, not asserted identical**), then
hand off to `0x0094d920` (mode flag `13`). That delegate branches on the standard authority gate
(`0x008addb0`): local-authoritative → immediate local apply plus a reward-UI chain; otherwise → a
network-replicated record (opcode `0x46`, debug tags `"human"`/`"cash_adjust"`). The engine-side
crib/store-purchase writer (`0x005F1E50`) subtracts a `float×100` price from "one player-record field"
the same way.

**Reader / HUD push, CONFIRMED mechanism, generic toast, event-driven.** The local-apply branch's UI
chain (`0x0091f5e0` → `0x0071b7b0` → two calls to `0x00710CB0`, the same stat-update entry point
`spec-tables-progression.md` documents for "HUD progress notice... when the count crosses a multiple of
`HudUpdateFrequency`") ends in a commit call, `0x0094a800`. **New this pass: `0x0094a800` is
independently reached from the unrelated vehicle-delivery notification path too** — confirming it is a
generic "post a HUD reward/toast" primitive, not cash-specific. **Verdict: money's toast/popup push is
CONFIRMED event-driven.** Whether the ALWAYS-VISIBLE cash counter (as opposed to a "+$500" popup) is
driven by this same commit or separately polls the live wallet field, the way health's getter-based path
is suspected to work, was **not determined — OPEN**, not assumed identical to either §2's or §3's
pattern.

## 5. Notoriety (wanted level)

**Live storage, CONFIRMED record family, OPEN exact score field.** A fixed family of per-track records
(5 hardcoded track bases selected by `0x008ed1d0`, plus a name-resolved 24-group table `0x008ed350`,
both sharing a record type with a `+0x455` "force no spawn" flag byte and `+0x3e0`/`+0x3e4`/`+0x3e8`
tier-range fields). The track-name resolver `0x00605160` is shared by `notoriety_set` and
`hud_set_fake_notoriety` (below). A SEPARATE save-snapshot offset `0x4A24`-`0x4A33` is the unlockable
notoriety-multiplier array (a bonus-percentage modifier) — not the live wanted-level score, must not be
confused with it. **The live numeric score's own byte offset was not pinned down this pass** —
`notoriety_get`/`notoriety_get_decimal` exist as real registered Lua natives but only a prior tranche's
crash-shape note (a null-argument `__stricmp` hazard) was on record, not their own full bodies/addresses
— **OPEN**.

*(Cross-reference, 2026-10-08: `spec-ai-runtime.md` §19 independently traces the live notoriety RECORD
itself — a 4-slot array at `0x014A4F70`, {level, fraction} pairs — from the opposite direction (the
add/decay/dispatch chain). That section's own record is almost certainly the same "live numeric score"
this section leaves OPEN; the two were not explicitly cross-checked field-for-field across these two
reports, but a future pass should treat `0x014A4F70+0x00` as the strong first candidate for the field
this section was looking for.)*

**Writer.** `notoriety_set` (`0x00a550b0`) resolves the track via `0x00605160` and calls
`0x006079c0` (sub-type tag 5). `notoriety_force_no_spawn`/`notoriety_force_no_spawn_group`/
`disable_specific_notoriety_group` mutate the adjacent `+0x455`/tier-range fields, not the score itself.

**Reader / HUD push — the clearest event/callback evidence of all five elements, not closed.**
`on_notoriety_event` (`0x00a57160`) is a real native that **registers a hook** (`0x005e4660`, slot 2, at
the target's `+0x1f00` sub-record) firing on notoriety-related events — the strongest literal
event/callback mechanism found for any of the 5 elements. **What was NOT traced:** which Lua HUD script
registers against this hook and which widget-write call it makes in response — the hook's EXISTENCE is
CONFIRMED, its consumer is OPEN. A second, separate mechanism, `hud_set_fake_notoriety` (`0x00a4d0e0`)/
`hud_get_fake_notoriety`, resolves a track name against three parallel 4-entry cross-title alias tables
(`0x012ed130`/`0x012ed140`/`0x012ed150` — this series' own SR3/SR2/SR1 rival-gang name sets) and sets a
scripted "displayed" override value, independent of the real record — HIGH CONFIDENCE this lets a
mission script show a different wanted level than the player's real state (e.g. a scripted chase)
without mutating real notoriety.

## 6. Minimap — live data feed traced (2026-10-08); base map + district overlay CONFIRMED, player blip still OPEN

The minimap is a first-class VINT element type, **`sr2_map`** (type id 11 of 13 in the fixed
registration order: `element, tween, animation, bitmap, gradient, text, group, document, point, clip,
bitmap_circle, sr2_map, video`), with its own one-time type-registration function (`0x00e34640` —
CONFIRMED to NOT itself instantiate an object or write a vtable pointer, 2026-10-08) and exactly 2 real
properties: `zoom` (float) and `map_mode` (`pause_map`=0, `minimap`=1 — the SAME element type backs both
the full-screen pause map and the corner minimap). `0x0132cf68` is `sr2_map`'s **property-descriptor
table** (matching `spec-lua-bindings.md` §9.2's own per-type table exactly, row 0 = zoom, row 8 =
map_mode) — **correcting an earlier framing that called it the vtable.** **The real per-instance vtable's
own base address is now CONFIRMED (2026-10-08): `0x01259cb4`**, found via the construction chain
(type-registration `0x00e34640` → pool constructor `0x00e34530`, capacity 3, object size `0xa8`=168
bytes → the MSVC `vector_constructor_iterator` → the real per-object constructor, `0x00e34290`, which
writes `*this = 0x01259cb4` as its first act). **The entire vtable was read in full (54 slots,
`+0x00`-`+0xd4`) and terminates exactly there** — `+0xd8` onward is simply the `"map_mode"` string
literal in `.rdata`, not more vtable; there is no construct/update/draw slot past the 2 known
properties, because none exists. ~~its 2 property-pair slot offsets are pinned by disassembling all 4
get/set trampolines directly: zoom get=`+0xcc`/set=`+0xc8`, map_mode get=`+0xd4`/set=`+0xd0` — note the
get/set order is the REVERSE of the convention `spec-lua-bindings.md` §9.3 established for `element`'s
own inherited base-class properties (there, getter sits at the lower offset); whether this reversal is
specific to subclass-added properties or incidental was not determined, flagged OPEN.~~ **CORRECTED
2026-10-08: the get/set order is NOT reversed — this was a labeling artifact in the earlier pass, not a
real discrepancy.** Reading the property-descriptor table directly (the authoritative source,
`+0x08`=setter/`+0x0c`=getter) plus both real implementation bodies gives: **zoom get=`+0xc8`
(`0x00e34420`, reads `this+0xa0` and outputs it)/set=`+0xcc` (`0x00e34460`, validates and writes
`this+0xa0`); map_mode get=`+0xd0` (`0x00e344a0`, reads `this+0xa4`)/set=`+0xd4` (`0x00e344c0`, a
`switch` writing `this+0xa4`)** — the same "lower slot = getter" convention `element`'s own base
properties use. Instance data: `zoom` is a float at `this+0xa0`; `map_mode` is an int/enum at `this+0xa4`
(the last dword of the 168-byte object — a clean internal consistency check). **The draw (`+0xbc`,
`0x00e34340`) and post-draw (`+0xc0`, `0x00e34320`) slots were also read in full**: draw branches on
`map_mode` and calls a generic 2D immediate-draw singleton (`0x02a5a008`) with a single mode-gated
primitive — a filled circle/ellipse sized by the live `zoom` value in minimap mode (most plausibly the
corner minimap's own round viewport mask), a two-point line/rect in pause-map mode (most plausibly the
pause map's own border); post-draw is a conditional batch-flush. **Neither slot contains an icon blit, a
texture bind, or any per-object loop** — confirming structurally that `sr2_map`'s own draw code is not
where the map content, icon pool, or any player marker gets painted (see §6.4 for where the player's own
marker is actually produced). One pre-existing finding stands unchanged: the minimap's own draw
callbacks write the UI renderer's current-colour cache directly, bypassing the redundancy filter every
other element type goes through — a genuine, already-flagged stale-cache hazard for whatever draws
immediately after a minimap. Full detail:
`scratchpad/weapons_fire_damage_and_player_blip_2026-10-08.md`.

### 6.1 The shared blip pool — 3 confirmed producers

A single, shared, generic "live map-marker" pool exists at the engine level: add (`0x00808c50`, 9 real
callers), remove (`0x00806e60`, 12 real callers), set-position (`0x00809270`) are generic primitives over
one record list. Three independently-confirmed producers feed it: (1) the pause map's own bookmark/GPS
system, `pause_map_add_bookmark`/`pause_map_set_gps` (already documented, `spec-lua-api-behaviour.md`
§31/§42.1); (2) **newly traced to the instruction**, the Lua-scripted minimap-icon system
(`minimap_icon_add_do`, `0x00a53f90`, §10.5/§10.6 below) — its own internal add call (`0x0093d3a0`) first
removes any stale entry, then calls a GENERIC per-object-instance "this object carries a map icon"
component, `0x0093b7a0`, which reads icon id/radius/flags/visibility-distance straight off a small
sub-record on the resolved object itself (fields `+0x8`/`+0xc`/`+0xc0`/`+0xc4`/`+0xcc`/`+0xd0`/`+0x7b` bit
0) before reaching the shared add primitive — the first C-level proof that the Lua minimap-icon system
and the pause map's own blip system are ONE underlying mechanism, not two; (3) a previously-undocumented
**kind-bucketed named-entity blip tracker** (`0x005ea320` and its sibling family), managing up to 4 kinds
× 20 live entries per kind in a flat array (`0x0149fdd0`, stride 0x148), icon ids from a 4-entry table
(`0x01126000`) — what the 4 kinds represent is HYPOTHESIS only (plausibly tracked ally/enemy/objective/
vehicle markers), not confirmed. `0x0093b7a0` has 3 further real callers (`0x0093d320`/`0x0093e520`/
`0x00941450`) and the add primitive has 8 further real call sites, none individually opened — concrete,
addressed next steps, not guessed at. A `spec-tables-vehicle-world.md` vehicle-definition `Icon` field
(resolving into a 108-entry blip-name table at `0x012fff78`) is a plausible, unconfirmed HYPOTHESIS-level
4th producer into this same pool.

### 6.2 The pause map's per-frame live-update chain — fully traced; district overlay is live VINT, not a texture

The full-screen pause map's per-frame tick (`0x007df660`, reached via an unresolved tail-jump, caller
OPEN) runs once per real rendered frame (proven by its use of the already-documented per-frame-delta
global `0x0132a0ac` to drive a continuous UI fade). It: recomputes input-driven pan/zoom state from 3
continuous axis reads (`0x007dafc0`, HIGH CONFIDENCE stick/mouse-wheel axes) — **CONFIRMING the pause
map's pan/zoom is driven by live player INPUT, not the player character's world position**; resolves the
mouse/gamepad cursor's own world-space position (`0x005bc5d0`/`0x00dcfc90`); broadcasts that cursor
position into a per-category hover-test cache (`0x007db5b0`, up to 8 categories, fires a
`"SYS_MENU_MAP_HOVER"` audio cue on change and drives a continuous 1.0↔1.25 blip-pulse/blink oscillator
every frame); and resolves which world zone the cursor sits over (the already-documented `0x0084ba60`
lookup, now confirmed from inside its own caller `0x007dd700`).

**New finding, not previously documented anywhere in this project:** that same `0x007dd700` then walks up
to `[0x0229a2b0]` live per-district VINT sub-elements EVERY TICK, pushing 5 properties each — a
world→screen-mapped position, a per-district ownership fraction (the already-documented stag-mode-gated
pair `0x0084ce80`/`0x0084ce30`, `spec-lua-api-behaviour.md` §31.5), two further derived floats (not
individually decoded), and a hover/selected flag. **The pause map's district-ownership overlay is a live,
per-frame-refreshed set of data-bound VINT elements, not a static or load-baked texture.** A separate,
single cached marker element is also refreshed every tick from the CURSOR's own world position
(`0x007d9bc0`, confirmed by full trace — not the player, see §6.4) when a bookmark/blip selection is
active.

### 6.3 The base map texture — data/asset side CONFIRMED, runtime bind OPEN

Three already-published findings, newly cross-referenced here, answer the base-imagery question:
`spec-world-streaming.md` §5's per-tile minimap texture asset (`minimap_CCRR.cvbm_pc`/`.gvbm_pc`, same
`CCRR` coordinate grid as the rest of the open world); `spec-zone-data-format.md`'s per-zone dependency
loader `0x00859110`, which already issues the load request for "`.cvbm_pc` minimap" alongside every other
asset a zone references — i.e. the minimap tile streams in/out through the EXACT SAME zone-dependency
mechanism as the rest of that tile's content, not a separate system; `spec-texture-format.md` §12's pixel
format `403`=`D3DFMT_R5G6B5`, whose own population breakdown specifically names "minimap tiles (640×512)"
as one of its three real-world uses. **CONFIRMED at the data/asset level:** the minimap's base imagery is
per-tile, 640×512 R5G6B5 texture data, streamed by the same world-streaming system as every other tile
asset — "which tiles are available" is a direct structural consequence of that system, not a separate
mechanism needing its own tracing. **OPEN, narrowly scoped:** which code reads the player's live position,
decides which loaded `minimap_CCRR` tile(s) are currently relevant, and binds that texture onto
`sr2_map`'s own drawable state — no texture/image-handle property write was seen anywhere in the
per-frame chain traced above (every write found there is a position/fraction/flag), consistent with (not
proof of) the base map art being a separate draw layer, bound independently.

### 6.4 The player-position blip — hypothesis (b)'s mechanism now CONFIRMED; the literal glyph draw is the one remaining gap

An exhaustive search (the full per-frame pause-map chain above, plus all 21 real call sites across the 3
shared blip-pool primitives) found **no** read of the standard local-player accessor (`0x009da4e0`) or the
generic object-position field (`+0x40`, `spec-player-runtime.md`'s own convention) feeding either the
blip pool or the cursor/hover chain — a genuine, confirmed negative over a concretely-enumerated search
space, not an absence-of-evidence shrug. The one single-marker element that looked like a candidate
(§6.2's cached marker) was traced to its own producer, `0x007d9bc0`, and is unambiguously the mouse/
virtual-cursor's own screen→world unprojection, not the player.

**Hypothesis (a) — resolved, 2026-10-08: a confirmed structural negative at the C++ level.** The player
character's own icon sub-record fields (`+0xc0`/`+0xc4`/`+0xcc`/`+0xd0`, `0x0093b7a0`'s own read shape,
§6.1) are written by a **shared base-object constructor, `0x0091f8d0`, with 9 real callers project-wide**
(including the human/player constructor `0x00954340`, which calls it as its very first act and adds
nothing icon-specific afterward) — generic, type-agnostic defaults ("no icon yet") identical for every
object type that shares this base, not anything player-specific. `0x0093b7a0`'s own 4 real callers were
re-checked for a local-player-accessor call that would gate the player in specially — none found; all 4
are generic, Lua-handle-driven. **The mechanism is real and available to any object, but nothing in the
C++ construction path hands the player to it specially** — whether the player ever receives a pooled
blip this way is a runtime/script-content fact (which handle a Lua `minimap_icon_add` call names), not
resolved one way or the other by the executable's own code.

**Hypothesis (b) — CONFIRMED-mechanism, 2026-10-08 (upgraded from HIGH CONFIDENCE).** `spec-save-
format.md` §7.5's "static minimap" option byte is applied directly (no setter call) to a live global,
`0x022c03e0`, by the options-apply routine `0x00841320`. That global is read by a previously-
undocumented, **indirectly-dispatched per-frame function, `0x00809910`** (reached only through a
function-pointer slot `0x02317a6c`, registered by `0x00848c07` inside a routine that also initializes
something literally named `"Vint"` — structurally separate from, and not sharing, the pause-map chain
§6.2 traces). `0x00809910`: resolves the local player; reads the player's own world position
(`player+0x40`/`+0x48` — **exactly** the field this section's own earlier search reported zero reads of,
found here because `0x00809910` sits outside both call-graphs that search covered) into a live
"map-centre" pair, `0x022cb2c8`/`0x022cb2d0` (falling back to a linked vehicle's position if the
player is seated); reads the static-minimap flag **three times** to gate a live-heading read
(`0x004bc550`) vs. a fixed/zeroed orientation, and whether a computed rotation (`0x0078b6f0`) is applied
to the on-screen draw-position math; then draws directly through low-level immediate-mode primitives.
**This is the genuinely separate corner-minimap update entry point** this section's own confidence
summary previously flagged as "HIGH CONFIDENCE it does not [share the pause-map chain], not confirmed by
locating a distinct entry point" — now located and named. The minimap's background/rotation is driven
every frame by the player's own live position and heading; because the on-screen transform centres the
view on the player by construction, **the player's own marker sits at a fixed screen point regardless of
world position** — exactly why no pooled blip-add call for the player was ever going to be found by
searching the blip pool. Hypothesis (a) and (b) are therefore not competing possibilities needing
further adjudication: (a) is a structural non-starter at the C++ level, (b) is the real, confirmed
mechanism.

**One honest, narrowly-scoped gap remains**, not traced within budget: a literal "draw the player's own
fixed glyph at the centre point" instruction, most plausibly inside `0x00809910`'s own later tail or one
of its uncalled `0x0080xxxx` helper callees (`0x00805d40`/`0x00805f90`/`0x00807970`/`0x00806350`/
`0x00808ad0`, none individually opened) — the geometry above makes one highly likely to exist nearby, but
it was not directly read.

**[CONFIRMED — the property-table/vtable-slot correction; `sr2_map`'s real vtable base address and
complete 54-slot contents (§6 above); the corrected zoom/map_mode get/set slot assignment; the shared
blip pool's 3 producers and their mechanisms; the pause map's full per-frame dispatcher and every traced
callee; the district-ownership overlay as live VINT, not a texture; the cursor (not player) identity of
`0x007d9bc0`; the base-map-texture naming/format/streaming tie; the icon sub-record's universal
(non-player-specific) defaults (hypothesis (a)); the full static-minimap-option chain from save-format
byte to `0x00809910`'s three gated uses (hypothesis (b), now CONFIRMED-mechanism). HIGH CONFIDENCE: the
3-axis input read; the hover category ids matching a filter/legend system; `0x00809910`'s identity as
specifically the corner-minimap's own update function. OPEN: a literal player-glyph draw instruction
inside `0x00809910`'s own tail or uncalled helpers; which code binds a loaded tile texture onto
`sr2_map`; 8+3 further unopened blip-pool/icon-component callers.]** Full detail:
`scratchpad/minimap_live_feed_2026-10-08.md`,
`scratchpad/weapons_fire_damage_and_player_blip_2026-10-08.md`.

### 6.5 Mission objective text and the waypoint/GPS route target — setters fully traced, HUD-side readers OPEN (2026-10-09)

**Objective text — a 4-slot global array, CONFIRMED.** The Lua native `objective_text` (`0x00a56920`,
424 real call sites across 41 scripts — the busiest entry point in this cluster) resolves its
`textKey` argument through a 3-stage localization fallback (`0x00849df0`, then a second
localization-table pair gated by a runtime singleton at `0x026e7e68`, then a raw-string
last resort) and writes into a fixed **4-slot, `0x130`-byte-stride record array at base
`0x022b6988`** via the real slot-writer `0x007ee110` (bound-checked `slotIndex < 4`). So the engine
genuinely supports up to 4 simultaneously-live objective-text lines, not one — a main objective plus
up to 3 others could in principle display at once, each independently settable/clearable
(`objective_text_clear` → `0x007eda00`). A flag bit on the write additionally touches a session
singleton (`0x0087ba20`), consistent with a co-op-replicated text broadcast.

**Waypoint/GPS route target — a SINGLE per-player global record, not a slot array, CONFIRMED.**
`waypoint_add` (`0x00a68cf0`) resolves its target name against a priority chain of object resolvers,
then hands the resolved position to `0x007dd420(x, y, z, mode)`. `mode` gates two independent paths:
`2`/`3` build a network-replicated record (opcode `0x40`, the same opcode `waypoint_remove` already
uses); `1`/`3` call the local-apply writer **`0x007d6bc0`**, which stores (or clears) a **fixed-size
single record per player** — `0x012fdff0` for the local player, `0x012feb20` for a co-op partner,
selected by a 2-way selector, not an array index. The record is a 1-byte "active" flag at `+0xae0`
plus a 32-byte position block at `+0xac0`/`+0xad0`; a null source buffer clears only the active flag,
leaving the position untouched.

**New unification finding: a mission waypoint and the player's own manual GPS pin are the same
record.** `0x007d6bc0` has (at least) two real callers that reach it independently: the
`waypoint_add` chain above, and **`pause_map_set_gps`**'s own dispatch target `0x007dba10`
(`pause_map_set_gps` already documented, `spec-lua-api-behaviour.md` §31/§42.1, now confirmed to
converge here). **There is no merge or priority logic — whichever source writes last simply
overwrites the other.** A mission-driven waypoint and a player-chosen pause-map GPS click cannot
coexist; this is the same kind of single-underlying-mechanism finding §6.1 already established for
the Lua minimap-icon system and the pause map's own blip system, one layer further down, specific to
the GPS/waypoint target. Two further real callers of `0x007d6bc0` (`0x00664320`, `0x007d8300`) were
not independently traced.

**One fully-wired worked example: the mission-start/checkpoint marker.** `0x006d5720`/`0x006d86f0`
(the mission-phase-2 marker-arm function already cited by `spec-lua-api-behaviour.md` §21.27a at the
summary level, fully opened this pass) fires, from one call site, in sequence: `objective_text` slot
`0` for the mission's own `+0x1e4` text key; a world-space indicator via the same internal call
`object_indicator_add_do` (§10.6) uses; a minimap blip via the same internal call `minimap_icon_add_do`
(§10.5) uses (icon kind `5`, a new data point for §6.1's "4+ kinds" census); a particle effect named
literally `"vfx_missioncheckPointicon"`; an armed walk-into trigger via the same tail-call
`trigger_enable` uses; and finally `0x007dd420(x, y, z, mode=3)` — the identical waypoint-position
setter above — targeting the marker's own position. This is a single, disassembly-confirmed
demonstration of every piece this section documents firing together at one mission-phase transition.
**This exact multi-system push is specific to the mission-start/checkpoint marker** (a C++-side,
phase-machine-driven special case) — there is no generic engine-side "advance to objective N"
mechanism (`spec-lua-api-behaviour.md` §21.27b already established this independently); for an
ordinary mid-mission objective change, the HIGH-CONFIDENCE (not independently script-verified)
picture is that the mission's own Lua script calls `objective_text`/`waypoint_add`/
`object_indicator_add_do` directly, stage by stage, using the same natives — not confirmed by reading
a real mid-mission script, since mission `.lua` content stays inside the parked mode-a containers
project-wide.

**Render-side consumers CLOSED for objective text, NARROWED (not closed) for the waypoint/GPS record
(2026-10-09).**

**Objective text — CONFIRMED consumer found.** The 4-slot array is one of 6 parallel, same-lifecycle
pools implementing a generic "Gameplay State Indicator" (GSI) widget cluster (shared destructor
`0x007eb0e0`, init `0x007eb880`, per-slot resolver `0x007eb220`, dirty-mark dispatcher `0x007eea80`).
The real per-frame consumer is **`0x007ebae0`**, reached every rendered frame via `0x00702a50` (the
master per-frame tick) → `0x009ea630` → `0x009e91c0` → `0x007ebae0` (its sole call site
project-wide). It is a per-frame poll with a change-detection short-circuit (gated per-slot on the
forced-enabled byte `+0xd` plus a global dirty timestamp `0x022b6634`), pushing each live slot's
fields (text id `+8`, flags, aux-text pair) into a VINT element literally named
**`"information_indicator"`** via the generic set/commit pair `0x00e26040`/`0x00e24930`. A fresh xref
census confirms the only functions anywhere touching `0x022b6988` are this consumer plus the
already-known writer/clear/init/destructor set — a closed, fully-accounted-for surface.

**Waypoint/GPS — a real per-frame consumer found.** The record is far larger and more active than
previously catalogued: 49 real uses across 22 functions per player base, spanning a
previously-undocumented module (`0x007d1450`-`0x007d8be0`). Confirmed every-frame chain:
`0x00702a50` → `0x007d8be0` → `0x007d89a0` (a budget-capped async state-machine pump, 1000μs/frame)
→ `0x007d7650`, which performs **real navmesh corridor-funnel pathfinding** from the player's
position to the waypoint target (portal/polygon walking, string-pulled smoothing), caching the
routed path's corner points at `0x02297e80`/`0x02297e34`, and drives **3D positional audio**
proximity/approach cues tied to those corners (`0x00ad5460` and friends) — a real,
continuously-recomputed route, not a straight line.

**The minimap route-draw question is CLOSED as a doubly-confirmed negative — the minimap never
shows the route, in any form (2026-10-09).** All 4 functions that reference the cached corner-point
globals (`0x02297e80`/`0x02297e34`) have now been individually read in full, not just xref-counted:
none of them touches the minimap's own 2D draw singleton (`0x02a5a008`), `sr2_map`'s vtable, or
either documented minimap chain (§6.2's pause-map, §6.4's corner-minimap). A dedicated string search
for a bearing/arrow-widget or VINT-element name (`route`, `nav_arrow`, `gps_arrow`, `compass`,
`minimap_arrow`, `waypoint_arrow`, `route_line`, `nav_marker`, `gps_line`) came back with zero hits
for every term, anywhere in the executable. So the minimap genuinely never shows the GPS route — the
minimap only ever shows the static player-centred rotation §6.4 already documents.

**But the broader question — is the route ever visualized at all — has a positive answer: yes, via a
world-space particle-effect arrow trail, CONFIRMED.** The other 3 consumers of the corner array
(`0x007d2590`, `0x007d2610`, `0x007d2650`) are the init/cull/teardown halves of exactly this system.
`0x007d2590` (the waypoint-record reset) resolves a particle-effect template by its literal name,
**`"vfx_gps_arrows"`**, via `0x005c50b0` — the same generic effect-name resolver the already-
documented mission-checkpoint marker (`0x006d5720`) uses for its own effect (`"vfx_missioncheckPointicon"`)
— caching the template handle at `0x012fdfd8`, then zeroing a **100-slot, `0x50`-byte-stride
corner-record array** (base `0x02297e40`: position at `+0x00`, live effect-instance handle at
`+0x40`/`+0x44` — the exact pair the original xref census surfaced — a kind discriminant at `+0x48`).
Every rendered frame, `0x007d7650`'s own per-corner loop (`0x007d7f1d`-`0x007d82f5`, read further
this pass than the pathfinding/audio portion above) culls any corner outside a cached reference
distance (`0x013c8740`) via the kill primitive `0x00854410`, and for any visible corner with no live
effect yet, spawns one at that corner's position (template from `0x012fdfd8`, placement via
`0x004f00f0`, instantiated via the generic, project-wide "build a positioned-effect descriptor then
spawn it" pair `0x0052a940`/`0x005cf190` — the same pair this document's own audio proximity cue
above reuses, now confirmed generic rather than audio-specific) — storing the resulting handle back
into the corner record. `0x007d2610`/`0x007d2650` tear down every spawned instance (via the same
`0x00854410`) on cull or on a full route clear; `0x007d2650` is also called directly from inside
`0x007d7650` whenever the per-frame pump decides there is no active route this frame. **Net effect:**
a trail of `"vfx_gps_arrows"` particle instances laid along the player's real, continuously-recomputed
navmesh route through the world — an in-world 3D guide, not a 2D minimap overlay — each segment
appearing/disappearing by distance-cull and being consumed as the player walks past it (a separate
sub-loop decrements the live corner count once the player's own position closes within a cached
radius pair, `0x012fdfdc`/`0x012fdfe0`). **[CONFIRMED — disassembly, all 4 corner-array consumers
read in full; the minimap negative and the particle-trail positive are two separate confirmed facts,
not inferred from each other. OPEN: `0x013c8740`'s own identity (plausibly a draw/LOD-distance
cull reference, not independently confirmed); no live capture confirms the effect is actually
visible under real play, though the mechanism's shape — a literal "arrows" name spawned per route
corner — matches the real game's known GPS-trail presentation.]** Full detail:
`scratchpad/gps_route_minimap_draw_2026-10-09.md`.

Still open: whether shipped missions actually use objective-text slots 1-3 for real simultaneous side
objectives, or whether real content only ever uses slot 0 (not checked against real script content —
note the mission-script "parked container" limitation this was originally attributed to has since
been resolved, `spec-vpp-container.md` §7.6; checking real mission scripts for this is now a cheap,
concrete follow-up). Full detail: `scratchpad/mission_hud_objectives_2026-10-09.md`,
`scratchpad/hud_objective_waypoint_render_consumers_2026-10-09.md`.

**`waypoint_is_placed` — CONFIRMED, closed 2026-10-09 (WAVE 10), and reconciles a real sub-field
distinction within this same per-player record.** The Lua native (`0x00a68680`, `spec-lua-api-
behaviour.md` was REGISTERED-ONLY for it) takes no real argument — it reads the Lua stack depth via
`lua_gettop` but discards the result and always passes a hardcoded literal `0` to its helper, so
**only the local player's record (`0x012fdff0`) is ever consulted**; a co-op partner's own waypoint
can never make this native return true. It returns true iff helper `0x007d2210(0)` finds dword
`record+0xa70 != 0` OR byte `record+0xa91 != 0`.

This is a **different sub-field cluster** from the `+0xae0`/`+0xac0`/`+0xad0` fields this section's
own prose above highlights (also written by `0x007d6bc0`) — not a contradiction, both clusters exist
in the same record. `0x007d6bc0` writes `+0xae0`(flag)/`+0xac0`/`+0xad0`(position) directly in its own
body, unconditionally based on whether its caller passed a non-null position struct, then
**unconditionally also calls `0x007d6970`** — the function that actually owns the
`+0xa70`/`+0xa91`/`+0xa84`/`+0xa8c` cluster `waypoint_is_placed` queries. On success `0x007d6970`
writes `+0xa60`=1, `+0xa91`=1 (the flag `waypoint_is_placed` ORs in), `+0xa74`=0, `+0xa90`=0, copies
the target position into `+0xa84`/`+0xa8c`, a type/kind byte into `+0xa92`/`+0xa5e`, marks the
record's own state dword (`+0x0`)=`0xb`, and resolves a live object/handle into `+0xa70` through a
type-descriptor-gated lookup (`0x00458230` against descriptor table `0x02cc9900` bit `0x80`, plus
liveness checks `0x00ad3160`/`0x00ad31a0`) — so `+0xa70` is a **resolved live-object-handle slot for
the route target**, not a boolean. `0x007d6970` has its own gating beyond `0x007d6bc0`'s: it can
decline to update (an early return when the new target is within 1.0 distance unit of the already-
cached one) or fall through to a different fallback writer (`0x007d1450`) when its own resolve/
liveness checks fail — so `+0xa70`/`+0xa91` track a gated, validity-checked routing target, not
simply "a GPS pin was last written."

**Net effect:** `waypoint_is_placed` reports whether the local player's record has an active,
*validated* routing target — the same target this section's own per-frame consumer chain
(`0x007d8be0` → `0x007d89a0` → `0x007d7650`) feeds into real navmesh pathfinding and 3D positional
audio — rather than literally mirroring the simpler `+0xae0` "was a raw position struct non-null"
flag. In every real call path found so far (`waypoint_add`, `pause_map_set_gps`) both clusters are
written together from the same top-level call, so in practice the two flags should track each other
for ordinary mission-script `waypoint_add`/`waypoint_remove` usage — the distinction only matters for
the 1.0-unit-distance no-op case and the two other, not-yet-traced direct callers of `0x007d6bc0`
(`0x00664320`, `0x007d8300`) already flagged above as unexplored. **[CONFIRMED for `waypoint_is_placed`'s
own body and the `+0xa70`/`+0xa91`/`+0xa84` field identification; HIGH CONFIDENCE, not independently
re-verified against a real mission-script call sequence, that the two sub-field clusters always move
together in practice.]** Full detail: `scratchpad/wave10_natives_batch1_2026-10-09.md`.

## 7. Confidence summary and open items

**CONFIRMED:** the `hud_display_*`/`hud_bar_*` architecture and full ruling-out as the core-stat path
(§1); health's field offsets and all 4 hit-points natives' full bodies (§2); `game_hud_update_inventory`'s
full body and `0x007f2650`'s external shape (§3); `cash_add`'s full body and the `0x0094a800`
generic-toast finding (§4); `notoriety_set`/`on_notoriety_event`/`hud_set_fake_notoriety`'s full bodies
(§5); `sr2_map`'s registration/type-id/property facts, the property-table/vtable-slot correction, the
real vtable base address and complete contents, the shared blip-pool mechanism, the pause map's
per-frame chain, the base-map-texture data/asset facts, and the player-blip mechanism (hypothesis (b)
CONFIRMED, hypothesis (a) a confirmed structural negative) (§6).

**HIGH CONFIDENCE, not independently proven by tracing a render-side consumer:** health's HUD readout
goes through the getter pair from a Lua UI script (§2); money's live wallet field sits on the same
player object every other scalar query reaches (§4); `0x009601d0`/`0x00960160` are the same clamp
function cited twice (§4, not reconciled).

**OPEN, flagged honestly rather than guessed:** cash's exact live byte offset (§4); which of
`0x009bc0d0`+`0x00963530` vs. `0x0096f8c0` is the real HP death trigger (§2); the clip-vs-reserve field
mapping inside `0x028CDC44`'s entries and inside `0x007f2650` (§3); the live notoriety score's own byte
offset and `notoriety_get`/`notoriety_get_decimal`'s own bodies (§5, but see the `spec-ai-runtime.md`
§19 cross-reference candidate); which Lua code answers `on_notoriety_event`'s hook (§5); a literal player-glyph draw instruction inside
`0x00809910`'s own tail (the player-blip mechanism itself is now CONFIRMED, §6.4 — this is only the
final draw-call pinpoint), and which code binds a loaded tile texture onto `sr2_map`'s drawable state
(§6, narrowed from a previously wide-open question down to these 2 specific gaps — the vtable's own
base address is now found, §6); `0x006ee130`'s identity and `0x00a26bc0`'s registered Lua name (§1,
neither changes the verdict).

## 8. Clean-room check

Plain hex addresses throughout. No Ghidra auto-names, no decompiler pseudocode, no `FUN_`/`DAT_`/`iVar`
-style tokens used as if real identifiers — every such name appears only as a bare evidence-anchor
address citation. No leaked Volition/Havok/Audiokinetic/RAD material.

## Changelog

- **2026-10-09 — §6.5's own render-side consumer gap: CLOSED for objective text, NARROWED for the
  waypoint/GPS record** (SPEC TEAM, agent `a67ae4c189aa4aa2b`, WAVE 9 item 2). Objective text's real
  per-frame consumer is `0x007ebae0` (reached every frame from the master tick `0x00702a50`),
  feeding a VINT element literally named `"information_indicator"` — part of a 6-pool generic
  "Gameplay State Indicator" cluster, not a standalone system. Waypoint/GPS: a real per-frame
  consumer (`0x007d89a0`/`0x007d7650`) performs genuine navmesh corridor-funnel pathfinding from the
  player to the target and drives 3D positional audio proximity cues — but the literal minimap
  route-line draw was checked for and confirmed NOT to exist anywhere in this cluster (never
  touches the pause-map/corner-minimap chains or the 2D draw singleton `0x02a5a008`) — a real
  structural negative, narrowing but not closing the original gap. Full detail:
  `scratchpad/hud_objective_waypoint_render_consumers_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — new §6.5: mission objective text and the waypoint/GPS route target, setters fully
  traced** (SPEC TEAM, agent `a407fca2b227df337`, WAVE 8 item 4). Objective text: a 4-slot global
  array (`0x022b6988`, stride `0x130`, bound-checked `< 4`), written by the Lua native
  `objective_text` (`0x00a56920`) via `0x007ee110`, text resolved through a 3-stage localization
  fallback. Waypoint/GPS: newly traced to a SINGLE per-player global record (`0x012fdff0`/
  `0x012feb20`, not a slot array), written by `0x007d6bc0` — **headline new finding: a
  mission-driven waypoint (`waypoint_add`→`0x007dd420`) and the player's own manual pause-map GPS
  pin (`pause_map_set_gps`→`0x007dba10`) converge on this exact same record and overwrite each
  other**, no merge/priority logic. One fully-wired worked example traced end to end: the
  mission-start/checkpoint marker (`0x006d5720`/`0x006d86f0`) fires objective text, a world
  indicator, a minimap blip, a particle effect, an armed trigger, and the waypoint setter all from
  one call site. HUD-side render consumers for both remain OPEN, matching this file's existing gap
  for health/money/notoriety. Full detail: `scratchpad/mission_hud_objectives_2026-10-09.md`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- 2026-10-08: §6 follow-up — found `sr2_map`'s real vtable base address (`0x01259cb4`), read the full
  54-slot vtable (confirming no construct/update/draw slot exists past the 2 known properties), corrected
  the prior pass's own zoom/map_mode get/set labeling (not actually reversed — a labeling artifact), read
  the draw/post-draw slot bodies (a simple mode-gated shape primitive, not an icon blit), confirmed the
  player's icon sub-record fields are universal/non-player-specific via the shared base-object
  constructor (hypothesis (a) now a confirmed structural negative), and traced the "static minimap" save
  option end-to-end to a previously-undocumented indirect-dispatch corner-minimap update function
  (`0x00809910`) that reads the player's own live position+heading — **upgrading hypothesis (b) from HIGH
  CONFIDENCE to CONFIRMED-mechanism** and closing this section's own "does the corner minimap share the
  pause-map chain" open item. One narrow gap remains: the literal player-glyph draw instruction itself.
  Full detail: `scratchpad/weapons_fire_damage_and_player_blip_2026-10-08.md`. Cleanroom self-check: 0
  new hits. Synced to Team B.

- 2026-10-08: §6 (minimap) replaced in full — the live data feed left OPEN by the original dispatch is
  now substantially traced. Corrected the "vtable `0x0132cf68`" framing to "property-descriptor table,"
  pinned the real vtable's 2 property-pair slot offsets by disassembling all 4 get/set trampolines
  (reversed get/set order vs. the established base-class convention, flagged OPEN). Found and confirmed a
  shared, generic 3-primitive blip pool with 3 real producers — the pause map's own bookmark/GPS system
  (already known), a generic per-object-instance map-icon component newly traced to be
  `minimap_icon_add_do`'s own real implementation (one underlying mechanism, not two), and a
  previously-undocumented kind-bucketed named-entity blip tracker. Fully traced the pause map's own
  per-frame live-update chain, newly finding its district-ownership overlay is a live, per-frame-refreshed
  set of VINT sub-elements, not a texture, and that its pan/zoom is driven by player INPUT, not world
  position. Closed the base-map-texture question at the data/asset level by connecting 3
  previously-unconnected already-published findings across `spec-world-streaming.md`/
  `spec-zone-data-format.md`/`spec-texture-format.md`. Investigated the player-position blip exhaustively
  and found it genuinely absent from every traced path, narrowing (not answering) it to 2 candidate
  readings, with a new supporting cross-reference (`spec-save-format.md`'s "static minimap" option).
  Full detail: `scratchpad/minimap_live_feed_2026-10-08.md`. Cleanroom self-check: 0 hits. Synced to
  Team B.

- 2026-10-08: file created (SPEC TEAM, HUD runtime data flow dispatch). Started from the project's own
  `hud_display_*` lead, traced its 15 real C-engine callers, and used that evidence to REFUTE the
  hypothesis that it's the core-stat path (every caller is a transient minigame/QTE/diversion overlay);
  independently ruled out the parallel `hud_bar_*` pool the same way. Closed health and ammo to CONFIRMED
  producer/consumer/cadence; closed money's writer/toast chain to CONFIRMED with an explicit OPEN caveat
  on the live byte offset; found the clearest event/callback mechanism of all five for notoriety
  (`on_notoriety_event`); surfaced the already-published but never-followed-up `sr2_map` structural
  anchor for the minimap, left OPEN per task priority. Full detail:
  `scratchpad/hud_runtime_data_flow_2026-10-08.md`. Cleanroom self-check: 0 hits. Synced to Team B.

- **2026-10-09 — §6.5: `waypoint_is_placed` closed (WAVE 10), reconciling a real sub-field
  distinction within the waypoint/GPS record** (SPEC TEAM, agent `a7134278632f2231f`). The native
  (`0x00a68680`) always queries only the local player's record, discarding any Lua argument; it
  reads a different sub-field cluster (`+0xa70`/`+0xa91`/`+0xa84`, owned by `0x007d6970`) than the
  `+0xae0`/`+0xac0`/`+0xad0` cluster this section's prose foregrounds — both written together by
  `0x007d6bc0` in practice, but gated differently (a 1.0-distance-unit no-op skip, and a resolve/
  liveness-gated fallback writer `0x007d1450` the raw flag doesn't share). Full detail:
  `scratchpad/wave10_natives_batch1_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits.

- **2026-10-09 — §6.5: the GPS route minimap-draw OPEN item CLOSED (WAVE 11)** (SPEC TEAM, agent
  `a1aeb9fcba94da977`). The minimap never shows the route in any form (doubly confirmed negative —
  all 4 corner-array consumers now read in full). The route IS visualized, via a world-space particle
  trail `"vfx_gps_arrows"` spawned/culled/consumed per navmesh-corridor corner by `0x007d7650`, reset
  by `0x007d2590`, torn down by `0x007d2610`/`0x007d2650`. Full detail:
  `scratchpad/gps_route_minimap_draw_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits.

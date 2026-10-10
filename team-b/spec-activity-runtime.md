# Side-activity/diversion C++ runtime: per-type class family, tick/score/notify (2026-10-09)

**Scope.** `scratchpad/side_activity_native_coverage_2026-10-09.md` §1 found that 57 of the 59 `_a_*`
location/activity markers in `sr3_city_0.vpp_pc` carry zero `.lua` script — these activities
(Insurance Fraud, Mayhem, Trafficking, Escort, the `stunt_*` family, Barnstorming, base jumping,
etc.) are driven entirely by C++ engine code reading `.xtbl` config directly, with no Lua layer at
all. `spec-tables-diversions.md` already maps the DATA side (the `.xtbl` loaders and runtime record
layouts) for this family. This file maps the RUNTIME CODE side: the per-type class pattern, the
tick/score/completion chain, and how it relates (or doesn't) to `activity_types.xtbl`.

**Honesty note up front:** this is a first map, not a complete one. Exactly one diversion type —
**Barnstorming** — was traced end to end, because it was the one with a findable, literal-string-
tagged entry point. The same mechanism is HIGH CONFIDENCE (strong structural evidence, not
individually re-derived) to generalize to the other 8 `stunt_*` tables and `base_jumping`. The
single biggest open question — how an `_a_*` marker's own placement data selects and constructs the
right diversion-type instance in the first place — is NOT answered here; this pass worked backward
from a known string literal, not forward from a marker. Full source: `scratchpad/activity_framework_2026-10-09.md`.

## 1. The per-type class pattern

Each diversion type's gameplay logic lives in a small, dedicated C++ class in a tight address
cluster immediately adjacent to its own `.xtbl` loader — e.g. Barnstorming's constructor
`0x00693ae0` sits next to its loader `0x00691f10` (`spec-tables-diversions.md` §3). This is the same
"one dedicated function cluster per table" shape already established on the data side, now shown to
extend to the gameplay functions too.

**Constructor, CONFIRMED (Barnstorming, `0x00693ae0`):** zero-initializes a `0x360`+-byte instance
(fields `+0x24` through at least `+0x394`, including a 50-entry sub-array at `+0x24`), resets
`+0x360`/`+0x364` to "no current trick" sentinels, copies two default-position dwords from
`0x029cdb98`/`0x029cdba0`, then **calls its own `.xtbl` loader (`0x00691f10`) directly, in-line** —
not through any generic "load my config" indirection. It then calls a secondary per-type setup
function (`0x006938e0`, not opened) and, if a local player exists, registers a 50-slot record array
(`0x0093ce20`/`0x0093d320`, capacity matching the `+0x24` array) into a per-player field at `+0x20b4`
(HIGH CONFIDENCE a personal "trick history" ring buffer, not confirmed).

**Shared base-class `Init`, CONFIRMED to exist as a fan-in, internals OPEN:** the constructor's
first call, `0x006a4010`, is shared unconditionally by **22 other constructor-shaped functions**
across the address range `0x006a1f70`-`0x006cb4a0` — the whole `stunt_*`/diversion cluster, by
address proximity to the already-named loaders (`stunt_drifting` `0x006bc970`, `stunt_hijacking`
`0x006bd3c0`, `stunt_near_miss` `0x006bf170`, `stunt_back_seat_driver` `0x006b95a0`, `stunt_oncoming`
`0x006c0070`, `stunt_peel_out` `0x006c0cf0`, `stunt_low_flying` `0x006bd920`, `base_jumping`
`0x00693cd0`). Confirmed by direct `xref`, not inferred from proximity alone: every one of the 22
callers is a distinct constructor-shaped function calling `0x006a4010` as its own first real action
— strong evidence every diversion type shares this constructor pattern (shared base `Init` → zero
own fields → own `.xtbl` loader → own secondary setup). **OPEN:** `0x006a4010`'s own body (what the
shared base actually initializes); which of the 22 other constructors maps to which specific
`stunt_*` table (only inferred from address clustering, not individually matched).

**A class-descriptor/vtable-style table, CONFIRMED to exist, semantics partially open:**
Barnstorming's constructor and tick function (`0x00692e20`, §2) are each referenced from **exactly
one place in the whole binary** — a slot in a dense function-pointer-sized table at
`0x01134a70`-`0x01134b54`+ (`0x01134b14` → constructor, `0x01134b24` → tick; confirmed by `xref`
both directions: neither function has any other caller, and the table's own base address is never
taken as an immediate anywhere, so it isn't reached through a plain computed-index `switch` either).
A string run `"DIVERSION_BARNSTORMING..."` sits immediately before the table at
`0x01134a40`-`0x01134a6c`. The table mixes real per-type functions (Barnstorming's own ctor/tick,
plus ~15 other distinct addresses consistent with other types' own slots) with identical-code-folded
generic stubs reused by hundreds of unrelated call sites elsewhere (`0x006948c0` = `return false`,
150+ unrelated callers including the master startup loader `0x005d25f0`; `0x00c0dea0` = empty
`return`; `0x00db60a0` = `return true`; `0x0066ac20`/`0x006b89a0` = unrelated Wwise stubs sharing the
same folded body — all confirmed NOT diversion-specific despite appearing in this table). **HIGH
CONFIDENCE, not proven:** a per-diversion-type C++ class vtable (or hand-rolled struct-of-function-
pointers "type descriptor"), most virtual slots left at the base class's default (hence the folded-
stub noise), only a few overridden per type (construct, tick, get-display-name). **OPEN, the honest
gap:** the table's own stride/field order — only Barnstorming's two known slots anchor it, no second
type's slots were independently matched, no RTTI/type-name string is attached to it.

**Marker→instance dispatch, SUBSTANTIALLY RESOLVED (2026-10-09 follow-up) — the answer is "there is
no per-marker construction," not a dispatch table.** `0x00693ab0` (sitting next to Barnstorming's
own constructor) registers `triggers.xtbl` row `0xe` ("Barnstorming") into the generic Lua
trigger-volume native-handler array (`spec-lua-api-behaviour.md` §1.14's `0x0093b3c0`, runtime array
`0x026232a8` — zero-fill BSS, not static data, via the real per-row writer `0x0093afe0`), with
handler `0x006938c0`→`0x00693380`. `0x00693ab0` is called from `0x006a34c0`, a flat 28-call startup
routine (itself called once from the master startup loader `0x005d25f0`) that registers **every**
diversion type's own trigger-enter handler, once, at startup — this is the top-level driver §1.14
already flagged as "not located." **The handler `0x006938c0` always operates on the same fixed
global address, `0x14bbef0`, never a per-marker pointer.** That address is a static-BSS singleton,
constructed exactly once at CRT-init time (`0x006925c0`, reached via an MSVC-CRT-style
static-initializer table), whose constructor write includes `[0x14bbef0+0x00] = 0x01134b14` — the
mega-table's own "Barnstorming constructor" slot address, used here purely as a stamped-in type-tag
data value, **never called as code.** `0x00693ae0` (the mega-table-referenced constructor)
re-confirmed to still have zero callers anywhere — it was never the real construction path at all.
**Conclusion:** each diversion type is one pre-existing static singleton, tagged with its own
mega-table slot address, registered once into `triggers.xtbl`'s native-handler array under its own
named row. "Marker → instance selection" reduces to a pure data-side name match — whichever trigger
volume carries row `0xe` fires Barnstorming — there is no runtime construct-the-right-class branch
at all. **[CONFIRMED by disassembly for this exact chain; HIGH CONFIDENCE this generalizes to the
~27 structural siblings (not individually re-matched, by the same evidence §1's own constructor
fan-in already establishes).]** Honest remaining gap: which on-disk record sets a real trigger
volume's `+0xa8` field to row `0xe` (ties back to §1.14's own unlocated `+0xa8`-writer); the other
13 `0x0093afe0` callers (other types' own rows) weren't individually opened; the mega-table
tick-slot's outer per-frame walker is still unlocated. **Lead (b), Guardian Angel, checked and
ruled out as a route to this mechanism:** its own `.xtbl` placement record has no activity-type-id
field — it's a Lua path-node mission-chain record (`Start_function`/`Cleanup_function` by literal
name), structurally the same framework as numbered missions, not a diversion-type registry entry;
its own natives are all bespoke gameplay, no generic activate-by-type dispatcher found. Full
detail: `scratchpad/wave13_activity_marker_dispatch_2026-10-09.md`.

**Name/display-string getters, CONFIRMED:** `0x00691e80` returns `"DIVERSION_BARNSTORMING"` →
`"Barnstorming"` (a trivial getter, likely another vtable slot). `0x00692210` is the same pattern
specialized by enum: `1` → `"DIVERSION_BARNSTORMING_INVERTED"`, `2` →
`"DIVERSION_BARNSTORMING_KNIFE_EDGE"` — the two named sub-stunts `barnstorming.xtbl` itself documents
(`Knife_Edge_Tolerance`/`Inverted_Tolerance`) are also a small in-code enum, not just parallel xtbl
fields.

**Not traced at all, honestly:** whether the activity's trigger mechanism is the generic Lua
trigger-volume system (`spec-lua-api-behaviour.md` §1.14, native-handler-dispatch via `0x0093b3c0`'s
`triggers.xtbl`-row-indexed function pointers) reused with a native-only handler row, or a completely
separate `_a_*`-marker-specific native dispatch reading the marker's own placement data. Neither
`0x0093b3c0`'s 20 real `triggers.xtbl` rows' function-pointer targets nor any `_a_*` marker's own
on-disk placement record were opened to check for a match against the constructor address or the
mega-table above.

## 2. Per-frame tick / scoring — CONFIRMED in detail for Barnstorming

**Tick function, CONFIRMED, fully disassembled:** `0x00692e20` (`this` = the Barnstorming instance;
called once per frame by whatever walks the mega-table from §1 — that outer driving loop was not
located). Per-frame logic, in order:

1. Resolves the local player object (`0x009da4e0`) and two stored object-handle pairs at instance
   offsets `+0x340`/`+0x344` and `+0x348`/`+0x34c` (via the project's standard by-id-pair object
   resolver `0x00458230` against singleton `0x024433a8`, with the usual liveness/class-bit gating
   `0x00853b10` — the same resolver chain already documented for the generic trigger per-frame poll,
   `spec-lua-api-behaviour.md` §1.14). These read as "candidate vehicle" and "driver/pilot" handles.
2. **Containment test, CONFIRMED to reuse the Lua trigger system's own geometric primitive:**
   `0x0093bfc0` — the shared box/sphere/cylinder containment test `spec-lua-api-behaviour.md` §1.14
   already named as having 10 call sites project-wide, including "a side-activity's own containment
   check." This is the direct confirmation of that lead: a side-activity with zero Lua reuses the
   Lua trigger subsystem's own geometric test as a plain C++ library call, with no `Trigger_Volume`/
   `triggers.xtbl` object involved — a different, lower-level sharing than the native-`triggers.xtbl`
   dispatch `0x0093b3c0` implements. Barnstorming tests the resolved candidate against a volume
   anchored at `+0x40` of the first resolved handle.
3. **Maneuver classification, CONFIRMED:** `0x00692460` reads live pitch/roll (`+0x398`/`+0x39c`),
   normalizes an orientation angle, returns `0` (neither)/`1` (inverted)/`2` (knife edge) by which of
   two angular windows the orientation falls in — directly implementing the two named sub-stunts. If
   the classification changed since the last tick (`+0x354`), the sustained-duration timer (`+0x358`)
   is reset via `0x00692210` (dual use — also the name getter from §1, same address).
4. **Duration accumulation and threshold check, CONFIRMED:** accumulates elapsed time in the current
   maneuver (`+0x374`/`+0x378`/`+0x37c`, `+0x380` = accumulated value); once `+0x380` exceeds a
   stored threshold at `+0x388`, calls `0x00692270` — the "trick scored" path.
5. **Exit path, CONFIRMED:** once the containment test fails (left the volume), the tick also calls
   `0x00692270` — the same function handles both "duration threshold reached" and "left the area,"
   folding scoring and completion into one call.

**Score/notification function, CONFIRMED:** `0x006926c0`. Re-resolves the candidate object; if
valid: computes a score/style value via `0x00692340` (not opened — OPEN, best next lookup for the
reward question), calls two bookkeeping functions (`0x00bda0e0`, `0x00bd2de0`), **increments a
per-player lifetime counter at player-object offset `+0x20b0`** (the exact field
`spec-tables-progression.md` §5.4 independently names as the stat-system record id `0x44`'s target —
HIGH CONFIDENCE, not proven here, that this is the stats/achievement pipeline rather than a bespoke
counter), fires the shared **210-message HUD hint dispatcher `0x00710e40`** with hint id `0xa3` (the
same mechanism `spec-tables-progression.md` §5.4 documents for respect level-ups — CONFIRMED this is
a generally-reused hint-posting path, not level-up-specific), and — gated on a global flag
(`0x014c2d19`, not traced) — posts a typed HUD notification via `0x007f1ad0(6,
"DIVERSION_BARNSTORMING", "DIVERSION_BARNSTORMING_SUBTITLE", counterValue, computedScoreValue)`,
then replicates the event to network peers via `0x00bdba30`. This is the concrete scoring/
notification chain, confirmed down to the exact HUD-post call and its arguments.

## 3. `activity_types.xtbl` — a separate system, not the per-tick dispatch mechanism

`spec-tables-progression.md` §10.12 already fully maps `activity_types.xtbl` (loader
`0x00617e10`→`0x006174c0`; 19 fixed `0x94`-byte records at `0x014B2DE8`, matched by `Name`). Its
fields (completion-image pointer, gameplay-modifier/disable flag bytes, soundbank name, unlockables
array, a per-type instance counter at `+0x4C`) are all metadata/flags, not function pointers —
nothing in that layout points at code. **No cross-link was found between this 19-entry registry and
the per-diversion-type class cluster above** — they read as two separate systems:
`activity_types.xtbl` governs coarse-grained modifiers (spawn suppression, HUD-off, stat-counting-
off) for whichever activity is "current," while the tick/score/complete logic for the
`stunt_*`/`barnstorming`/`base_jumping` family lives entirely in the dedicated per-type classes
above, each reading its own `.xtbl` table with no indirection through `activity_types.xtbl` at all.
This matches `spec-tables-diversions.md` §13's own existing finding ("this cross-reference does not
exist as data, only as a plausible design relationship") and adds the code-side confirmation: no
call from the Barnstorming class cluster reads `0x014B2DE8` or calls `0x00617e10`/`0x006174c0`, and
no reverse call was found either. **OPEN:** whether `activity_types.xtbl`'s registry is consulted by
whatever outer system decides which diversion class to construct in the first place.

## 4. Completion/reward — partially answered, the reward-grant path is the open piece

- **Completion detection, CONFIRMED** (§2): a trick completes either by sustaining the maneuver past
  a stored duration threshold or by leaving the geometric volume; both converge on `0x00692270`.
- **HUD notification on completion, CONFIRMED** (§2): `0x007f1ad0` with the activity's name/subtitle
  keys plus the live counter and computed score.
- **Stat-pipeline counter increment, CONFIRMED, consumer not independently re-verified:** player-
  object `+0x20b0` increments every successful trick — the same offset `spec-tables-progression.md`
  §5.4 names for the achievement/stat system's stat id `0x44` record.
- **Actual cash/respect grant: still OPEN, hypothesis (a) now RULED OUT (2026-10-09 follow-up).**
  No call to `game_award_respect`/`0x00843bf0`/`0x009e2440` (`spec-tables-progression.md` §5.4) or
  any sibling award routine was found anywhere in the traced Barnstorming constructor, tick,
  classifier, or score/notify functions. `0x00692340` (the score/style computer) and every direct
  callee of `0x006926c0` were opened in full: `0x00692340` is pure arithmetic (duration/threshold
  ratios × per-sub-stunt coefficients) producing a trick-count int and a style-score float, not
  currency; `0x00bda0e0`/`0x00bd2de0` turned out to be telemetry/checkpoint-state bookkeeping, not
  reward-related. **Hypothesis (a) is RULED OUT — the grant does not happen through `0x00692340`.**
  Narrowed to (b) a batch-grant elsewhere (the still-unlocated destructor/teardown counterpart to
  the shared `Init`, `0x006a4010`), or (c) a wholly separate consumer of the flat `Max_Respect`/
  `Max_Cash` globals, with (c) now favored since nothing in the fully-checked per-trick chain reads
  anything resembling those globals. Fastest next lookup: find the direct reader(s) of the flat
  `Max_Respect`/`Max_Cash` globals themselves (`spec-tables-diversions.md` §1.2 item 2 — loaded into
  flat globals, consumer never found on the data side either), rather than continuing from the
  tick-chain side. Not guessed further — the single largest OPEN item for completion/reward. Full
  detail: `scratchpad/wave12_activity_dispatch_reward_2026-10-09.md`.

  **Direct reader found (2026-10-09, WAVE 13 follow-up) — still OPEN, but the search space is now
  narrowed, not widened.** For `stunt_jumping_diversion` specifically (globals `0x014c74c4`
  `Max_Respect`/`0x014c74c8` `Max_Lifetime_Respect`/`0x014c74cc` `Max_Cash`, `spec-tables-
  diversions.md` §2.9): a direct `xref` on all three CONFIRMS exactly one non-loader reader for all
  three, `0x006bbc10`, called from the tick function `0x006bc4b0` on a completion condition — the
  **same structural pattern as Barnstorming's own tick→score/notify chain** (HIGH CONFIDENCE,
  supports hypothesis (b) — a completion-triggered call out of the tick, not a disconnected batch
  consumer). `0x006bbc10` (fully disassembled) reads all three globals, feeds them into a pure
  clamp/scale helper (`0x006a4d80`, 16 callers, no grant), calls the same two bookkeeping functions
  already ruled out as telemetry (`0x00bda0e0`/`0x00bd2de0`), then posts a HUD notification keyed
  `"DIVERSION_BEST_JUMP_DISTANCE"` — the exact same shape as Barnstorming's own inert score/notify
  function. **No call to `game_award_respect`/`0x00843bf0`/`0x009e2440` anywhere in this chain
  either** (exhaustive grep of the full recursive disassembly). So the per-type flat-global readers
  are consistently score/notify functions of the same already-proven-inert kind, across two
  independent diversion types now (Barnstorming's instance field, `stunt_jumping_diversion`'s flat
  globals) — the actual grant mechanism, if it exists as a per-trick call, is not reachable from
  either type's own tick chain. Strengthens the case that the real grant is a batch/session-end
  mechanism entirely outside any individual diversion type's own code (hypothesis (b), now the
  leading candidate). Full detail: `scratchpad/wave13_activity_reward_globals_2026-10-09.md`.

  **Actual cash/respect grant — RESOLVED for `streaking` (CONFIRMED, 2026-10-10 follow-up),
  OVERTURNING hypothesis (b) as the universal answer.** An xref scan of the respect-apply routine
  `0x009e2440` (20 call sites, 17 functions, project-wide) found one caller, `0x006b91a0`, sitting
  directly in the diversion address cluster next to `streaking`'s own loader `0x006b89d0` — this
  is `streaking`'s own score/notify function. It grants the reward **inline, per level
  completion, with no batching and no Lua involvement**: a per-level data-driven flag
  (`0x006a4790(0xca, 0x014c73b4)`, almost certainly reflecting `streaking.xtbl`'s own per-level
  `Cash`/`Respect` choice) selects one of two branches. The respect branch multiplies the level's
  respect value by the same global respect-scale selector helper (`0x007bdb70`) the Lua-callable
  `game_award_respect` native itself uses, rounds it, and calls `0x009e2440` directly — the exact
  native-side routine `game_award_respect` bottoms out in. The cash branch rounds the level's cash
  value and calls `0x0094d920` directly — the exact routine the Lua-callable `game_award_cash`
  native (and, independently, `0x009e2440` itself) also calls into, confirming `0x0094d920` is a
  shared low-level "apply an amount to a player object and network-replicate it" primitive one
  layer below both currency and respect grants. Either branch posts a per-level HUD notification
  keyed `"DIVERSION_STREAKING_LEVEL_COMPLETE_RESPECT"`/`"DIVERSION_STREAKING_LEVEL_COMPLETE_CASH"`
  using the actually-applied amount, and — once the level index reaches the table's level count
  (`0x014c73b0`) — an additional `"DIV_STREAKING_COMPLETE"` notification plus end-of-activity
  cleanup (`0x007169e0`, `0x0101bac0`). **This result, not the batch-mechanism lead, is now the
  best-supported general answer: individual diversion types can and do grant their own reward
  inline from their own tick/score chain, by calling straight through to the same primitives the
  Lua reward natives use.** Barnstorming and `stunt_jumping_diversion` are therefore better read as
  the exceptions needing their own explanation (most plausibly: genuinely reward-less/dead reward
  fields in the shipped build) rather than as evidence for a hidden universal batch consumer — that
  hypothesis is now the WEAKER of the two, not the leading one. Hypothesis (c) (a Lua-side
  consumer) is independently confirmed dead for this whole family:
  `scratchpad/side_activity_native_coverage_2026-10-09.md` shows 57 of 59 `_a_*` activity markers
  carry zero `.lua` at all, and neither `sr3_city.lua` nor `game_lib.lua` (both read in
  full/grepped) contain any diversion-reward logic. **Residual, not re-investigated this pass:**
  whether `mugging_diversion` and the other untraced types follow Streaking's direct-grant pattern
  or Barnstorming's reward-less pattern; why Barnstorming/`stunt_jumping_diversion` specifically
  don't grant inline.

- **`streaking` traced end to end as a second worked example (CONFIRMED, 2026-10-10), upgrading it
  from "HIGH CONFIDENCE, not individually re-derived" (§5) to a concrete second worked example
  alongside Barnstorming.** Tick function `0x006b9390` (sole reference: diversion mega-table slot
  `0x01138444`, the same vtable-style dispatch pattern Barnstorming's own slots at `0x01134a70`+
  already establish) accumulates elapsed time (`+0x24`), gates on two state checks
  (`0x009634f0`/`0x006b8d70`), fails out with a "time's up" notification if the level's time
  budget (`0x014c73bc+0x28`) is exhausted, otherwise scans nearby pedestrians for ones within a
  3-unit radius not already reacted-to (`+0x2bd` bit `0x1`), triggers a react/startle call
  (`0x0097e280`/`0x004e53a0`) on qualifying ones, and increments a naughty-count (instance `+0x20`);
  once that count reaches the current level's threshold (array `0x014c73e0`), calls the score/notify
  function `0x006b91a0` directly, which performs the reward grant described above.

- **Reward-grant classification extended to the remaining diversion types (2026-10-10, second
  follow-up) — one more confirmed direct-grant type found, most others remain genuinely open.** A
  full project-wide xref scan of both reward primitives (`0x009e2440`: 20 calls in 17 functions;
  `0x0094d920`: 41 calls in 32 functions) checked against every named diversion type's own address
  cluster found a second confirmed direct-grant type: `cat_and_mouse`'s own cluster (loader
  `0x006c6c30` through the shared-`Init` fan-in's own upper bound `0x006cb4a0`) contains a function
  at `0x006c9400`, called from `0x006c9df0` inside the same cluster, that rounds a percent-style
  score and calls `0x0094d920` directly against the resolved local player object — the identical
  no-Lua, no-batching shape `streaking`'s cash branch already established, though only the cash
  side was found here, not a respect branch. A second candidate hit, near `base_jumping`'s loader,
  did not hold up on closer reading: the containing function (`0x006a4220`-`0x006a443b`) calls both
  primitives back-to-back, but operates through a shared "current diversion" singleton
  (`0x014c2d10`, read/written by at least nine other functions spanning `0x006a33b0` to
  `0x006cbe2d`) rather than through anything `base_jumping`-specific, and is itself referenced from
  exactly one project-wide data slot (`0x012f465c`, a vtable-style assignment, not a call site) —
  consistent with being the diversion framework's own shared/default completion-and-reward handler
  for whichever type doesn't override it, not a per-type routine. **This raises a real possibility
  that some of the remaining types grant reward through this shared default path** rather than
  through either an own-cluster call (which this scan would catch) or no grant at all — a case the
  proximity method cannot distinguish from "reward-less" without opening each type's own
  constructor to check whether it overrides this vtable slot, not done this pass. For the 9
  remaining `stunt_*` tables and `exploration_diversion`/`mugging_diversion`/`photo_op`, no caller
  of either primitive was found anywhere in or near their clusters, but — per the honesty standard
  this file already set for `base_jumping`'s own near-miss above — none of their own tick/score
  functions were individually opened to confirm a Barnstorming-style HUD-only reward field, so
  these are reported as **ambiguous, not independently confirmed either way**, rather than
  reward-less. Two further reward-primitive call sites (`0x0069f8e0`, calling both primitives;
  `0x006cc630`, calling the cash primitive) sit just outside the diversion cluster's established
  address bounds and were not matched to any named type.

  | Diversion type | Loader address | Classification | Evidence |
  |---|---|---|---|
  | `streaking` | `0x006b89d0` | direct-grant, CONFIRMED | `0x006b91a0` (own score/notify) calls `0x009e2440` and `0x0094d920` directly, per-level, no batching |
  | `cat_and_mouse` | `0x006c6c30` | direct-grant, CONFIRMED | `0x006c9400` (inside own cluster, called from `0x006c9df0`) calls `0x0094d920` directly with a rounded percent-based amount; cash branch only, no `0x009e2440` call found in this function |
  | `barnstorming` | `0x00691f10` | reward-less, CONFIRMED | own constructor/tick/classifier/score-notify fully traced; no call to either primitive anywhere in the chain |
  | `stunt_jumping_diversion` | `0x006ba4e0` | reward-less, CONFIRMED | sole non-loader reader of its `Max_Respect`/`Max_Lifetime_Respect`/`Max_Cash` triad (`0x006bbc10`, called from tick `0x006bc4b0`) is pure clamp/scale + HUD notify, no call to either primitive |
  | `base_jumping` | `0x00693cd0` | ambiguous | nearest caller pair (`0x006a43b4`/`0x006a43bd`) turned out to belong to the shared/generic base-class handler above, not `base_jumping`-specific — cannot be attributed to this type with confidence |
  | `stunt_drifting` | `0x006bc970` | ambiguous | no caller of either primitive found in or near this cluster; own tick/score function not independently opened |
  | `stunt_hijacking` | `0x006bd3c0` | ambiguous | same as above |
  | `stunt_near_miss` | `0x006bf170` | ambiguous | same as above |
  | `stunt_back_seat_driver` | `0x006b95a0` | ambiguous | same as above |
  | `stunt_oncoming` | `0x006c0070` | ambiguous | same as above |
  | `stunt_peel_out` | `0x006c0cf0` | ambiguous | same as above |
  | `stunt_wheels` | `0x006c16a0` | ambiguous | same as above |
  | `stunt_low_flying` | `0x006bd920` | ambiguous | same as above |
  | `exploration_diversion` | `0x006a6250` | ambiguous | same as above |
  | `mugging_diversion` | `0x006b7900` | ambiguous | same as above |
  | `photo_op` | `0x0091d610` | ambiguous | no caller of either primitive found anywhere near this (distant, isolated) address region either |

- **`0x006a4220` opened in full (2026-10-10, third follow-up) — confirms conditional dual-grant,
  narrows the amount source, and surfaces a new reachability problem. The "most likely also
  default" inference for the remaining untraced types is NOT supported by this pass and is not
  being added.** The function is a 3-way completion dispatcher on the "current diversion" singleton
  (`0x014c2d10`): one status value posts a "you died"-style notification, one posts a
  cancel/escalation notification and resets state, and only the third calls `0x0094d920` then
  `0x009e2440` back-to-back — the dual-grant is real but conditional, not unconditional. The amount
  is not reached by dereferencing the singleton to a per-instance field — it comes from two flat
  globals adjacent to the singleton pointer in memory but structurally separate from it
  (`0x014c2d14`, a respect-scale multiplier consumed via the same global scale-selector
  `0x007bdb70` `streaking`'s own grant and the Lua respect native use; `0x014c2d40`, a cash amount
  passed by address with currency code `5`). Both globals have exactly one writer, `0x006a40c0` —
  which only RESETS them to zero — and no writer anywhere sets a non-zero value into either, so the
  real per-type amount source remains unlocated. `0x006a40c0` is itself called from three functions
  inside the documented diversion address range (`0x006b6e40`, `0x006c8c90` — inside
  `cat_and_mouse`'s own cluster — `0x006cdc70`), which does tie this mechanism into real diversion
  code, cutting against a "wholly dead" reading. However, `0x006a4220` itself has zero CALL-type
  cross-references anywhere in the binary (only the one DATA reference at `0x012f465c` already
  known), and an xref check on that data slot's containing 32-dword block's own base address
  (`0x012f4600`) also returned zero uses — no instruction or static field anywhere points at this
  table. Testing the hypothesis that this table shares the stride of the already-confirmed per-type
  descriptor tables (Barnstorming's `0x01134a70`+, streaking's analogous table) by checking the
  same relative offset in both disproved it — that slot holds an unrelated, already-flagged folded
  stub in both known cases. **Net effect: the actual virtual-dispatch call site that would invoke
  `0x006a4220` could not be located by static address tracing, raising a genuine, unresolved
  possibility that this handler is unreachable in the shipped build rather than a working shared
  default** — reported as an open dead end, not forced to either conclusion. A follow-up override
  check for `mugging_diversion` (constructor `0x006b7b40`), `base_jumping` (constructor
  `0x00694630`), and `photo_op` (nearest candidate `0x0091d700`) found that none of them — nor
  Barnstorming's own already-confirmed constructor, used as calibration — contains any explicit
  vtable-pointer-write instruction at all, consistent with every diversion type being a
  pre-existing static singleton rather than a placement-constructed object; checking the override
  question properly would require reading each type's own static singleton's own data bytes
  directly, which was only ever done for Barnstorming and was not attempted for these three.
  `photo_op`'s nearest candidate (`0x0091d700`) additionally looks structurally different (no call
  to the shared base Init, reached by an ordinary call rather than a lone data-tag reference) and
  is flagged as likely not the real constructor at all. **Conclusion: the "shared base-class
  default" reward path is now itself of uncertain reachability, and the override-vs-inherit
  question for `mugging_diversion`/`base_jumping`/`photo_op` was not resolved either way — the
  table entries for all three checked types and the eleven others remain `ambiguous`, not
  reclassified.**

- **Unlock-on-completion:** not checked. `activity_types.xtbl`'s own `Unlockables` array (§3) is the
  natural candidate mechanism, but no call site linking a completed trick to that array was found or
  searched for in depth.

## 5. Confidence summary and open items

**CONFIRMED, full trace:** Barnstorming's constructor, tick, maneuver classification, and
score/notify chain (reward-less); `streaking`'s tick and score/notify chain, including its reward
grant (both cash and respect branches calling straight through to the same primitives the Lua
reward natives use — §4); `cat_and_mouse`'s own direct cash-grant call site (`0x006c9400`); the
shared base-class default completion/reward handler `0x006a4220`-`0x006a443b`, keyed on a
framework-wide "current diversion" singleton (`0x014c2d10`), installed as a vtable slot
(`0x012f465c`) for whichever type doesn't override completion handling — a newly-discovered THIRD
reward-path alongside per-type direct-grant and reward-less; the shared containment-test reuse
from the Lua trigger system; the shared base-class `Init` fan-in across 22 sibling constructors;
the mega-table's existence as the sole reference point for the traced functions;
`activity_types.xtbl`'s non-relationship to this class cluster (code-side confirmation of an
existing data-side finding).

**HIGH CONFIDENCE, not individually re-derived:** the same class pattern generalizes to the other
13 untraced types (strong structural evidence: shared `0x006a4010` call, same address clustering
around their already-named loaders); the mega-table is a vtable-or-equivalent type-descriptor
structure.

**OPEN, flagged honestly rather than guessed:** how an `_a_*` marker's placement data selects and
constructs the right diversion-type instance in the first place (the framework's own core open
question — not answered); the mega-table's exact field stride/order; ~~the actual respect/cash
reward-grant consumer for the `Max_Respect`/`Max_Cash` triad~~ (RESOLVED for `streaking` and
`cat_and_mouse`, §4 — inline per-completion grants, not a batch consumer; a THIRD path, the shared
base-class default handler, was also found but not yet matched to any specific type); why
Barnstorming/`stunt_jumping_diversion` appear reward-less by contrast; **which of the 11 remaining
untraced diversion types (the 9 `stunt_*` tables minus `stunt_jumping_diversion`, plus
`base_jumping`, `exploration_diversion`, `mugging_diversion`, `photo_op`) use direct-grant,
reward-less, or the shared base-class default path — a full project-wide xref scan found no
per-type caller for any of them, but none of their own tick/score functions was individually
opened to confirm a reward-less reading, so all 11 are reported as ambiguous, not classified
either way** (see the table above); any relationship between `activity_types.xtbl`'s registry and
this class cluster beyond "none found so far"; most of the other 13 diversion types' own tick
functions in full (Barnstorming, `streaking` and `cat_and_mouse`'s cash-grant site are now
individually traced, to varying depth).

## 6. Clean-room check

Plain hex addresses throughout. No Ghidra auto-names, no decompiler pseudocode, no
`FUN_`/`DAT_`/`iVar`-style tokens used as if real identifiers. No leaked Volition/Havok/
Audiokinetic/RAD material.

## Changelog

- **2026-10-10 — shared default handler `0x006a4220` opened in full; vtable-override check on 3
  types — the "most likely also default" inference is NOT supported, third follow-up** (SPEC TEAM,
  agent `a49f8a06539d406bb`). Confirms the handler's dual-grant is real but conditional (one of 3
  completion-status branches), and that its reward amount comes from two flat globals adjacent to
  (not fields of) the singleton — both globals have exactly one writer, which only RESETS them to
  zero, so the real per-type amount source stays unlocated. More importantly: `0x006a4220` has
  ZERO call-type cross-references anywhere in the binary, and its own vtable-shaped data table's
  base address also has zero references — raising a genuine possibility the handler is unreachable
  in the shipped build. The requested vtable-override check for `mugging_diversion`/`base_jumping`/
  `photo_op` found none of the 3 (nor Barnstorming, used as calibration) write an explicit
  vtable-pointer at construction — consistent with every diversion type being a pre-existing static
  singleton, meaning the override question needs each type's own singleton's raw data bytes, not
  attempted this pass. **Net result: the orchestrator's proposed inference ("if the default grants
  and 3 types don't override, label the other 8 as likely-default") is explicitly NOT applied — all
  14 ambiguous types stay ambiguous**, reported honestly rather than forced. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — reward-grant classification extended project-wide, second follow-up** (SPEC TEAM,
  agent `ad1456a8e67d130bb`). Full xref scan of both reward primitives against every named
  diversion type's address cluster found a second confirmed direct-grant type, `cat_and_mouse`
  (cash branch, `0x006c9400`), and a significant new structural finding: a shared base-class
  default completion/reward handler (`0x006a4220`, keyed on a framework-wide singleton
  `0x014c2d10`, installed via a vtable slot `0x012f465c`) that some types may use instead of either
  overriding with a direct grant or being genuinely reward-less — a third possible path this
  proximity method can't distinguish from "reward-less" without opening each type's own
  constructor. The remaining 11 diversion types (9 `stunt_*` tables minus
  `stunt_jumping_diversion`, plus `base_jumping`/`exploration_diversion`/`mugging_diversion`/
  `photo_op`) have no per-type caller of either primitive in or near their own clusters, but were
  NOT independently confirmed reward-less (their own tick/score functions weren't opened) — all
  reported honestly as ambiguous, not classified, in the new table. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — reward-grant consumer FOUND for `streaking`, overturning the leading "batch
  mechanism" hypothesis** (SPEC TEAM, agent `afd08ed5c390d9156`). An xref scan of the respect-apply
  routine `0x009e2440` found `streaking`'s own score/notify function (`0x006b91a0`) calling it
  directly for respect, and the shared cash-apply primitive `0x0094d920` directly for cash — both
  are the exact routines the Lua-callable `game_award_respect`/`game_award_cash` natives themselves
  bottom out in, with no Lua round-trip and no batching. This means Barnstorming and
  `stunt_jumping_diversion` (both previously shown reward-less in their own chains) are the
  exceptions, not evidence of a hidden universal batch consumer — that hypothesis is now the
  weaker of the two. Also traced `streaking`'s own tick function (`0x006b9390`) end to end as a
  second full worked example alongside Barnstorming. Independently confirmed hypothesis (c), a
  Lua-side grant mechanism, is dead for this whole family (57/59 activity markers carry zero Lua;
  neither `sr3_city.lua` nor `game_lib.lua` contain diversion-reward logic). Residual: whether
  `mugging_diversion` and the other untraced types follow the direct-grant or reward-less pattern.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — new file, first map of the C-side activity/diversion runtime framework** (SPEC
  TEAM, agent `a17a5b112e3768f45`, WAVE 11 item 2). Barnstorming traced end to end as the one worked
  example: per-type C++ class pattern (own constructor calling its own `.xtbl` loader in-line, a
  shared base-class `Init` fan-in across 22 sibling constructors, a class-descriptor/vtable-style
  mega-table at `0x01134a70`+), full tick/classify/score/notify chain, and the confirmed non-
  relationship to `activity_types.xtbl`. Largest open items: how a marker selects/constructs its
  activity instance, and the respect/cash reward-grant consumer. Full detail:
  `scratchpad/activity_framework_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — WAVE 12: reward-grant hypothesis (a) RULED OUT; marker-dispatch search narrowed
  but not closed** (SPEC TEAM, agent `aa7c5d5d6e39a9fc8`). `0x00692340` and every direct callee of
  `0x006926c0` opened in full — none calls `game_award_respect` or grants currency; `0x00692340` is
  pure score arithmetic. Reward-grant question narrowed to a batch-grant elsewhere or a separate
  `Max_Respect`/`Max_Cash` global consumer, now favoring the latter. Marker→instance dispatch: the
  mega-table's base address re-confirmed to have zero uses project-wide (independently
  reconfirming the existing finding) — interior-slot computed-index readers remain unattempted.
  Full detail: `scratchpad/wave12_activity_dispatch_reward_2026-10-09.md`. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — WAVE 13: direct reader of `Max_Respect`/`Max_Cash` found for
  `stunt_jumping_diversion`, reward-grant question narrowed further, not closed** (SPEC TEAM, agent
  `aed66519d614e3030`). `0x006bbc10` is the one and only non-loader reader of all three reward
  globals, called from the tick function `0x006bc4b0` on completion — same structural pattern as
  Barnstorming's own tick→score/notify chain, and, like that chain, provably inert (pure score
  arithmetic + HUD notification, no `game_award_respect` call anywhere). Two independent diversion
  types now both show the same result: the per-type tick chain never grants currency. Strengthens
  the case for a batch/session-end grant mechanism outside any individual type's own code. Full
  detail: `scratchpad/wave13_activity_reward_globals_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — WAVE 13: marker→instance dispatch SUBSTANTIALLY RESOLVED — there is no
  per-marker construction** (SPEC TEAM, agent `a8225d1deae9e8e4c`, re-dispatched on Sonnet after
  an initial Opus dispatch was stopped before producing results, per the user's Sonnet-first rule).
  Each diversion type is one pre-existing static singleton (e.g. Barnstorming's own `0x14bbef0`,
  constructed once at CRT-init time, stamped with its mega-table slot address as a type tag, never
  called as code), registered once at startup into the generic Lua trigger-volume native-handler
  array under its own named `triggers.xtbl` row — "marker → instance" is a pure data-side name
  match, not a runtime construct branch. Generalizes with HIGH CONFIDENCE to the other diversion
  types. Guardian Angel's own placement data checked and ruled out as a route to this mechanism —
  it's an ordinary Lua mission-chain record, not a type registry. Full detail:
  `scratchpad/wave13_activity_marker_dispatch_2026-10-09.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

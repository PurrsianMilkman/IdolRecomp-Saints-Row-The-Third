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

**[2026-10-14 CORRECTION, FINAL: the diversion respect/cash grant IS located. It is paid when the on-screen reward message closes (`0x007FA320` calls the award routine). Per stunt: respect = trunc(score fraction x `Max_Respect`), switching to round(fraction x `Max_Cash`) cash once the diversion's lifetime statistic reaches `Max_Lifetime_Respect`. Full detail and test vectors: spec-tables-progression.md "## Free-roam respect grants and level-up flow (2026-10-14)".]**

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

**User-observed ground truth, 2026-10-11 (not re-derived from the executable this pass, but a
real behavioral observation that should steer the next search): the cash reward for a diversion
visibly arrives only when the player loads back into free-roam after the activity, not during the
activity itself.** This favors hypothesis (b) over (c) above — a batch-grant on activity
teardown/exit-to-freeroam, not a tick-time or score-computation call, and plausibly explains why
every in-activity search (constructor, tick, classifier, score/notify) came up empty for all 10
still-OPEN diversion types, not just Barnstorming. Next step for whoever picks this up: search the
activity-exit/teardown path (the "still-unlocated destructor/teardown counterpart to the shared
`Init`, `0x006a4010`" already named above) specifically for a reward-grant call, rather than
continuing to check more in-activity tick/score functions.

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

  **RESOLVED for 10 of these 11, 2026-10-11 follow-up (SPEC TEAM, agent `aa0589748b08aa6dc`,
  independently re-verified by direct query before splicing) — 10 confirmed to have no call to
  either reward primitive in their own score/notify or tick chain; CORRECTION, same day
  (orchestrator): this does NOT mean "reward-less" — the shipped game visibly pays these rewards
  (mugging pays cash, stunts/exploration show real respect pops), so some OTHER path grants them
  (a different setter, a generic diversion-complete handler, or Lua) — labeled "no direct
  primitive call; reward path elsewhere, OPEN" below, not "reward-less, CONFIRMED," the same class
  of overclaim the footstep-audio item made.** `photo_op` remains ambiguous (`base_jumping`'s own
  tier consumer was subsequently found, see below — no longer an exception). A
  real-object-constructor pattern ("Shape B": calls shared `Init` `0x006a4010`, zeroes instance
  fields, tail-calls its own loader, reached from exactly one slot — index 4 — in a private
  16-dword per-type mega-table in `.rdata`) was confirmed for `stunt_wheels`/`base_jumping`/
  `exploration_diversion`/`mugging_diversion`, alongside Barnstorming's own already-known
  instance; the mega-table's layout is now resolved further: index 8 = the type's own **tick**
  function, index 14 = a **name/title getter**, several indices are cross-type folded generic
  stubs. The other 7 (the remaining `stunt_*` tables) use a simpler "Shape A" tag-write wrapper
  with no `Init` call, reached from the master 28-call startup driver `0x006a34c0` — a previously
  undistinguished second constructor shape. For each of the 7 Shape-A types plus `stunt_wheels`/
  `exploration_diversion`/`mugging_diversion`, the real score/notify (or tick) function was found
  and fully opened: all read their own private `Max_Respect`/`Max_Lifetime_Respect`/`Max_Cash`
  triad (or, for `stunt_wheels`, a per-record array of the same triad), route it through the
  already-proven-inert clamp/scale helper `0x006a4d80` plus telemetry/HUD calls, and call NEITHER
  `0x009e2440` nor `0x0094d920` anywhere in the fully-opened chain — reclassified "no direct
  primitive call; reward path elsewhere, OPEN" (not reward-less — see correction above).
  `base_jumping`'s own tier consumer FOUND, 2026-10-11 follow-up (SPEC TEAM, agent
  `ad0cd4823a21ead90`, independently re-verified by direct query before splicing): **not missing
  after all — it lives outside the mega-table entirely.** `0x00695d50`, reached from a separate
  per-attempt state machine `0x00696310` (state==2, "jump complete") rather than the mega-table's
  own tick slot, selects the highest `Reward_Tier` entry whose `Max_Distance` the attempt's own
  measured distance clears, scales `Respect`/`Cash` through the same inert clamp helper
  `0x006a4d80`, logs telemetry via `0x00bda0e0`, and notifies HUD via `0x007fc2d0`/`0x007fc190` —
  re-verified exactly: its own callee list contains neither `0x009e2440` nor `0x0094d920`. A new
  field was found in the process: `this+0x54` (just before the tier array) is the real tier
  COUNT, confirmed by the reset function's own zeroing span and by this consumer's own
  loop bound. **Reclassified into the SAME "no direct primitive call; reward path elsewhere,
  OPEN" bucket as the other 9** — not a different kind of ambiguous after all. `photo_op` is the
  one REMAINING case, ambiguous for a THIRD, sharper reason: its own `.xtbl` loader (read in
  full) has no `Max_Respect`/`Max_Cash`-shaped fields at all — only `Saints_Loved`/`Saints_Hated`/
  `Trigger_Effect` — so the whole triad-based classification method this file uses simply does
  not apply to this type; whatever it grants (if anything) is a structurally different mechanism
  not yet investigated. **Net count, project-wide: 10 types with no direct primitive call in
  their own chain, real reward path still OPEN (Barnstorming, `stunt_jumping_diversion`,
  `base_jumping`, and 7 more), 2 confirmed direct-grant (`streaking`, `cat_and_mouse`), 1
  genuinely unresolved (`photo_op`), plus the shared-default handler
  `0x006a4220` whose own reachability is still unconfirmed.**

  **Convergence trace, same day, follow-up** (SPEC TEAM, agent `a6d56e7c68abe3502`, independently
  re-verified by direct query before splicing — including `0x00695d50`'s full raw listing,
  `0x007fc2d0`'s raw listing, and `0x00710cb0`'s caller list). Followed `0x00695d50`'s scaled
  `Respect`/`Cash` values (written by `0x006a4d80` at `0x00695e33` into caller-local stack
  variables only, never into any instance field) downstream, and did the same for
  `stunt_drifting`'s `0x006bcdb0`. **Both chains converge exactly, call-for-call, past the scaling
  step:** both feed the same two scaled locals into the identical telemetry-event pusher
  `0x00bda0e0` (gated behind three enable-flag globals, bottoming out in a generic event-submit
  call, `0x00423270`) and into the identical shared HUD notification-parameter system
  (`0x007fc2d0`/`0x007fc190`, both opened in full) — a 15-slot array at `0x022b7ac0` (stride
  `0x58`, confirmed by raw listing: `IMUL EAX,EAX,0x58; ADD EAX,0x22b7ac0`), each slot holding a
  message-id tag and up to two typed "format argument" pairs, used to fill in localized HUD toast
  text (e.g. "+%d Respect"). **This convergence point is confirmed NOT itself a reward-grant
  mechanism** — re-verified: neither `0x00bda0e0` nor the HUD-array field-copy worker it shares
  with `0x007fc190` ever calls `0x009e2440` or `0x0094d920`, and the scaled values live only as
  transient stack locals / HUD-display arguments, never written to any object field, pending-queue,
  or save-data location found so far. Useful negative evidence, independently confirmed: the
  generic HUD-hint dispatcher `0x00710cb0` that several of these chains also call IS itself called
  by both real primitives (`0x0094d920` at `0x0094da39`/`0x0094da79`, `0x009e2440` at `0x009e2603`)
  when they genuinely grant something — so a `0x00710cb0`/`0x00710e40` call in a chain is NOT by
  itself a signal of a reward grant; every HUD toast uses it, reward or not. **Net conclusion: the
  real grant path for all 10 "OPEN" types remains genuinely unlocated** — it dead-ends in shared
  UI/telemetry plumbing, not a hidden reward mechanism. Two honest possibilities remained open at
  the time, not mutually exclusive: (1) these 10 types really are reward-less from this engine
  code's own perspective and the shipped-game rewards (mugging cash, stunt/exploration respect
  pops) come from a batch/save-stat system entirely outside any per-type code path, not yet
  located; or (2) a still-unfound second reader of the HUD-notification-arg array (`0x022b7ac0`+)
  triggers an actual grant as a side effect of the toast. **Possibility (2) CLOSED 2026-10-11, see
  below — a full xref scan of the array's readers found only lifecycle/pool-management plumbing,
  no grant.** Possibility (1) stands as the remaining live explanation.

  **`0x00bda0e0` opened in full, same day, follow-up** (SPEC TEAM, agent `a528bdcf295be8702`,
  independently re-verified by direct query before splicing — `0x00bda0e0`'s and `0x00423270`'s
  own raw listings both matched the report exactly). Orchestrator pushback: `0x00bda0e0` receives
  BOTH the scaled Respect and Cash together, and both are persistently tracked player stats, so it
  was worth fully opening rather than accepting the shallow "telemetry" label. Full recursive
  trace of every callee (`0x009da4e0` → trivial getter; `0x00bd8a60` → read-only state-snapshot
  filler, ~14 further callees, all reads; `0x00423270` → sampling-rate gate (`0x00403070`,
  classic `rand()%100` telemetry sampling against a per-event-type rate table) → `0x00426bd0`
  (GDS middleware event serialization, named `GDS_value_*` imports) → `0x00401ca0` (job-struct
  allocation, `GDS_value_encode_values`) → `0x00401b30` (critical-section-protected append to an
  async worker-thread job queue, bookkeeping globals only)) — **confirmed conclusively: no write
  anywhere in this entire tree touches a respect/cash/save field, and no call anywhere reaches
  `0x009e2440` or `0x0094d920`.** The three gate flags (`0x013123d1`/`0x029897a5`/`0x029897a6`)
  were confirmed to gate this same shape across ~78-80 sibling functions at `0x00bd80a0`-
  `0x00bd94f0`+ — i.e. a project-wide "is telemetry enabled" toggle for this whole event family,
  not a stats-tracking switch; one flag (`0x029897a6`) is toggled at runtime by two other
  functions (set 1 / cleared 0), the other two have no writer found in this pass — their actual
  in-session value is OPEN, requiring either their setters or live capture, but doesn't change
  the conclusion since the entire subsystem dead-ends in telemetry regardless of the flags' state.
  **This closes the `0x00bda0e0` half of the convergence lead: it is pure analytics plumbing, the
  orchestrator's stat-tracking hypothesis does not hold for this specific function.**

  **HUD-array reader angle CLOSED 2026-10-11 (orchestrator-routed, direct harness query, no agent
  dispatched).** A full xref census of the array base (`0x022b7ac0`) found exactly 7 functions
  touching it; 3 were already opened (`0x007fc190`/`0x007fc2d0`'s field-copy worker and notify
  entry point, `0x007fa090`'s pool constructor). The 4 remaining were opened in full this pass:
  `0x007f9dc0` — pure arithmetic, stamps a slot with a unique `(generation<<16)|index` tag on
  allocation, no calls, no field writes beyond that one tag; `0x007fa090` itself, on closer read,
  is the ONE-TIME pool initializer (builds the 15-slot array as an intrusive doubly-linked free
  list across 4 sub-structure offsets per slot, `+0x5c`/`+0xb4`/`+0x10c`/`+0x164`, self-referential
  head pointers — classic list-init idiom, no gameplay write); `0x007fc110` and `0x007fc200` are
  both handle-release/destructor variants (validate the caller's handle generation against the
  slot's own stored generation, release any owned typed-argument resources via
  `0x007f95d0`/`0x007f9650`/`0x007f96c0`/`0x007f9770`, then call the shared slot-release routine
  `0x007fb4f0` and zero the caller's handle) — `0x007fc200` alone has dozens of callers across the
  diversion/activity tick functions (including `stunt_drifting`'s own `0x006bcdb0`/`0x006bcd70`),
  confirming it is the generic "I'm done with this toast handle" cleanup every caller uses, not a
  type-specific hook. `0x007fb4f0` itself (opened as a callee) is reference-counted slot-pool
  bookkeeping — decrements a refcount at `+0xc`, frees via `0x007fa320` at zero, otherwise touches
  a lock/dialog-queue helper (`0x00e25890`/`0x00d9e4c0`) and a message dispatcher (`0x007f9880`)
  — all plumbing for the notification queue itself, no reward-primitive call, no save/stat field
  write anywhere in any of the 4 functions. **Possibility (2) above is now closed: there is no
  hidden second reader of this array that grants anything — every consumer beyond the known
  display path is slot-pool lifecycle management (allocate/stamp/release), full stop.** The real
  grant mechanism for all 10 "no direct primitive call" types stays genuinely unlocated; this
  closes the HUD-array angle specifically, and the standing live explanation is possibility (1)
  above — a batch/save-stat system entirely outside any per-type code path. This thread is now
  considered fully exhausted by static analysis short of a live capture.

  - **Evidence:** full xref census of `0x022b7ac0` (7 functions total, re-confirmed against the
    live Ghidra server); all 4 previously-unopened functions read in full
    (`0x007f9dc0`/`0x007fa090`/`0x007fc110`/`0x007fc200`), plus their own callees
    (`0x007fb4f0`, read to its own key branch points).
  - **Confidence:** CONFIRMED for all 4 functions' lifecycle-only behavior (no reward-primitive
    call, no field write beyond the slot's own generation tag, anywhere in any of them).
  - **Unknowns:** `0x007fb4f0`'s own full body past the refcount/lock/dispatch branches shown
    (not read line-by-line to its own `ret` — the branches read are sufficient to rule out a
    reward call, since none of them touch `0x0094d920`/`0x009e2440` or any player-record offset,
    but a full read was not performed); possibility (1)'s own batch/save-stat system remains
    entirely unlocated.

  | Diversion type | Loader address | Classification | Evidence |
  |---|---|---|---|
  | `streaking` | `0x006b89d0` | direct-grant, CONFIRMED | `0x006b91a0` (own score/notify) calls `0x009e2440` and `0x0094d920` directly, per-level, no batching |
  | `cat_and_mouse` | `0x006c6c30` | direct-grant, CONFIRMED | `0x006c9400` (inside own cluster, called from `0x006c9df0`) calls `0x0094d920` directly with a rounded percent-based amount; cash branch only, no `0x009e2440` call found in this function |
  | `barnstorming` | `0x00691f10` | no direct primitive call; reward path elsewhere (OPEN) | own constructor/tick/classifier/score-notify fully traced; no call to either primitive anywhere in the chain |
  | `stunt_jumping_diversion` | `0x006ba4e0` | no direct primitive call; reward path elsewhere (OPEN) | sole non-loader reader of its `Max_Respect`/`Max_Lifetime_Respect`/`Max_Cash` triad (`0x006bbc10`, called from tick `0x006bc4b0`) is pure clamp/scale + HUD notify, no call to either primitive |
  | `base_jumping` | `0x00693cd0` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | own reward structure is a 10-tier array (`this+0x58`, stride `0xc`: Max_Distance/Respect/Cash; count at `this+0x54`); real consumer found: `0x00695d50`, reached from a separate per-attempt state machine `0x00696310` (state==2), not the mega-table tick slot — selects the highest tier cleared, scales via `0x006a4d80`, no call to either primitive |
  | `stunt_drifting` | `0x006bc970` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | score/notify `0x006bcdb0` (called from tick `0x006bd180`), re-verified by direct query — callees: `0x006a4d80`/`0x00bda0e0`/`0x006a49b0`/HUD (`0x00710cb0`,`0x00710fa0`,`0x007fc190`,`0x007fc2d0`,`0x007fc200`)/`0x00618200`/`0x00691e50` — no call to `0x009e2440` or `0x0094d920` |
  | `stunt_hijacking` | `0x006bd3c0` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | `0x006bd740` (mega-table-dispatched), same inert callee set (resolver `0x00458230`/`0x00853b10`, `0x006a4d80`, `0x00691e50`), no primitive calls |
  | `stunt_near_miss` | `0x006bf170` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | `0x006bf6c0` (3 tick callers: `0x006bf8d0`/`0x006bf9b0`/`0x006bfe90`, plausibly success/timeout/crash), same inert pattern, no primitive calls |
  | `stunt_back_seat_driver` | `0x006b95a0` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | `0x006b9a90` (tick `0x006b9e20`), same inert pattern, no primitive calls |
  | `stunt_oncoming` | `0x006c0070` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | `0x006c0720` (tick `0x006c0a80`), same inert pattern, no primitive calls |
  | `stunt_peel_out` | `0x006c0cf0` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | `0x006c10b0` (tick `0x006c13c0`), same inert pattern, no primitive calls |
  | `stunt_wheels` | `0x006c16a0` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | structurally different (real Shape-B constructor `0x006c1a00`, per-record triad in an array, stride `0x1c`, up to the count at `0x01139500` — multiple wheel-stunt sub-variants, like Barnstorming's own two sub-stunts); tick `0x006c2290` (re-verified exact), dispatches to `0x006c1e60`/`0x006c1f50` on a state flag, both → `0x006c1980`/`0x006c15c0` + resolver/telemetry; no primitive calls anywhere in the depth-2 trace |
  | `stunt_low_flying` | `0x006bd920` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | `0x006be080` (2 tick callers: `0x006be2b0`/`0x006bed60`), same inert pattern, no primitive calls |
  | `exploration_diversion` | `0x006a6250` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | real Shape-B constructor `0x006a64d0` (mega-table slot `0x01136df4`); tick `0x006a6ed0`→`0x006a6c20` + the Barnstorming-shared HUD hint dispatcher `0x00710e40`, no primitive calls |
  | `mugging_diversion` | `0x006b7900` | **no direct primitive call; reward path elsewhere (OPEN, 2026-10-11)** | real Shape-B constructor `0x006b7b40` (mega-table slot `0x011381ac`); has NO `Max_Respect`/`Max_Cash` triad at all — its own instance fields are `Default_Min_Cash`/`Default_Max_Cash`, confirmed to feed a cosmetic/world-state value, not a reward grant; tick `0x006b8600`→`0x006b81a0`/`0x006b7cf0`, no primitive calls |
  | `photo_op` | `0x0091d610` | **CLOSED 2026-10-11 — CONFIRMED reward-less, same bucket as Barnstorming (not a diversion-class instance at all)** | see the dedicated paragraph below |

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
Barnstorming/`stunt_jumping_diversion` appear reward-less by contrast; ~~which of the 11 remaining
untraced diversion types... use direct-grant, reward-less, or the shared base-class default
path~~ **RESOLVED 2026-10-11 for 10 of 11 (§4's own follow-up, wording corrected same day per
orchestrator — see §4): 7 `stunt_*` types plus
`stunt_wheels`/`exploration_diversion`/`mugging_diversion` confirmed to have no call to either
primitive in their own score/notify or tick chain — NOT "reward-less" (the shipped game visibly
pays these: mugging cash, stunt/exploration respect pops), so the real grant path is still OPEN,
just ruled out as being this direct-call shape. `base_jumping`'s own 10-tier reward array
(`this+0x58`, stride `0xc`) now HAS a located consumer, `0x00695d50` (see above) — it joins this
same bucket. `photo_op` (has no `Max_Respect`/`Max_Cash`-shaped fields at all, so the triad method
doesn't apply) remains the one genuinely ambiguous case, not from lack of looking.** Project-wide
count now: 10 with no direct primitive call (real path OPEN), 2 confirmed direct-grant, 1
unresolved, plus the shared-default handler's own reachability still unconfirmed. Still OPEN: any
relationship between `activity_types.xtbl`'s registry and this class cluster beyond "none found so
far"; `photo_op`'s own reward mechanism, if any; the shared base-class default handler
`0x006a4220`'s own actual call site (virtual-dispatch, not found by static tracing).

## 6. Per-activity rules — port recipe (2026-10-12, FINAL)

**Scope and sources.** These eight activities are the "instanced" activity family. They are not the
§1-§2 diversion singletons. Each playable instance is its own `_a_<tt>_<hood>_<nn>` zone (str2)
carrying one `.xtbl` whose root element is `Activity`. The per-instance files were extracted to
`scratchpad/ai/xtbl_scan/sr3_city_0/*_a_*_modal.str2_pc___a_*.xtbl` (DLC Genki escort:
`scratchpad/ai/xtbl_scan/dlc1/dlc1.vpp_pc__dlc1_a_es_0N.xtbl`). Type codes: `if` Insurance Fraud,
`dt` Trafficking, `sn` Snatch, `ha` Heli Assault, `mh` Mayhem, `es` Escort, `tm` Tank Mayhem, `rm`
Professor Genki (`activity_types` row `Running Man`). **No `.lua` exists for any of the eight**: of
the activity zones, only the two Guardian Angel zones (`_a_ga_*`) ship Lua. Every rule below is
therefore either xtbl data (CONFIRMED, values read from the shipped files) or C++ (addresses given).

### 6.1 Shared frame: levels, rewards, and the reward-grant path (closes §4's open item for this family)

**Difficulty levels = instances, not `Level` rows.** Every instance file has exactly **one**
`Levels/Level`. Difficulty is the instance number `_01`/`_02`/`_03`. Each type ships 2 hoods × 3
instances = **6 instances**, except Tank Mayhem, which has 3 hoods × 3 = **9**. The DLC Genki
escort has 2 instances. The engine reader supports up to **6 `Level` rows per instance** (see
below), but the shipped data never uses more than 1. **[CONFIRMED, data]**

**Uniform reward ladder (data):** `Cash_Award`/`Props_Award` (Props means respect) are
`2000/200`, `4000/400`, `6000/600` for instances `_01/_02/_03` across `if`, `dt`, `sn`, `ha`,
`mh`, `tm` and base `es`. Genki (`rm`) pays `1000/200`, `2000/400`, `3000/600`. The DLC Genki escort
pays `4000/400` and `5000/500`. Only the `_01` instance of each hood carries `Reward` =
`Activity_Unlock_<XX>` (an `unlockables.xtbl` row, e.g. `Activity_Unlock_IF` → `UNL_FRAUD`, category
`Activities`, which unlocks the activity's replay/menu entry). The exceptions are `_A_SN_NW_01` →
`Activity_Unlock_SN-K` (Snatch Kinzie variant), `_A_ES_SW01` → `Activity_Unlock_ES-T` (Escort
Tiger variant), `_A_RM_DT_01` additionally `Reward2 = Homie_Pierce`, and `DLC1_A_ES_02` →
`DLC_story02_vehicle_genki`. The "complete all instances" unlock is
`activity_types.xtbl.Unlockables`: Escort → `Gang_NPC_Strippers`, Escort_Tiger →
`Gang_NPC_Wrestlers`, Snatch → `Gang_NPC_Hos`, Snatch_Kinzie → `Gang_NPC_Cops`, Tank Mayhem →
`Gang_NPC_NG`, Running Man → `Gang_NPC_Mascots`. Fraud's own `Unlockable` is `all_fraud`. Mayhem,
Trafficking and Heli have none. **[CONFIRMED, data. OPEN: the code that tests "all instances done"
before applying `Unlockables`.]**

**Common instance reader, `0x0061c6e0` (CONFIRMED, decompiled).** This is the base-class
`Activity`-record reader. It is reached from 12 per-type vtable data slots
(`0x01129e68`…`0x011337b8`) plus 2 direct callers. It reads `Name` (hashed, `+0x194`), the flags
`Explored_at_start` (bit0 of `+0x1d4`) and `End_Warp_Sidewalk` (bit2), `Start_Location`
(`Disabled`/`Enabled`; when enabled it resolves object `activity_start_<zone>_<name>` into
`+0x30`), the warp names (`+0x34`, `+0x74`, `+0xb4`, `+0xf4`, 64 bytes each), `Player_Outfit`
(`+0x134`), `Cutscene` (`+0x19c`), `Start_function`/`Cleanup_function` (`+0x154`/`+0x174`), and
`Disable_Trigger` (bit1). It then loops `Levels/Level`, **capped at 6**, with the count at `+0x1a0`.
Per level *i* it reads `Reward` into `+0x1d8+8i` and `Reward2` into `+0x1dc+8i` (both resolved
through the unlockables lookup `0x0071cc90`, sentinel = "none"), `Cash_Award` into `+0x1a4+4i`, and
`Props_Award` into `+0x1bc+4i`. The per-run "level completed" bitmask is `+0x1d5`.

**Reward grant, `0x0061bb50`. CONFIRMED (decompiled); this is the reachable answer to §4's open
item for this family.** It is called only from `0x005e97a0`. Given the activity, it looks up the
persistent per-activity save record via `0x006170c0` (keyed by the hashed name). For **every level
*i* < count whose bit *i* is not yet set in the save record's byte `+8`**, it:

1. sets bit *i* in the instance's `+0x1d5` and in the save record's byte `+9`, unless in a
   co-op-client state (`0x0087ba20` check), which takes `0x006188b0(i)` instead;
2. applies `Reward` and then `Reward2` (if not "none") via the unlock-apply routine `0x0071ccf0(id,1,0)`;
3. telemetry `0x00bd2f10`;
4. if a local player exists, grants **respect** through `0x009e2440(player, Props_Award, 0)`, then
   a second `0x009e2440` call with `round(Props_Award × (scale − k))`. Here `scale` comes from the
   same global respect-scale selector `0x007bdb70(0)` that `streaking` uses, and `k` is the constant
   at `0x012a2d70` (CONFIRMED 1.0, read as a double from the image, so the bonus is the difficulty/scale surplus). It then
   grants **cash** `Cash_Award` via `0x00960210` and the shared money primitive `0x0094d920`;
5. posts HUD hint args `0x5b` (cash) and `0x5c` (respect) through `0x00710cb0`. Unless the
   `activity_types` record's flag byte (`0x014b2e2a + type×0x94`, bit3) is set, it also posts
   hints `0x13`/`0x33` plus the record's own completion hint id (`0x014b2e0c + type×0x94`, if ≠ −1);
6. increments the per-type completion counter at `0x014b2e38 + type×0x94` (record offset `+0x50`
   of the `activity_types` registry, `spec-tables-progression.md` §10.12). The type index comes
   from the instance's vtable slot `+8`.

It then calls `0x0061af70` and `0x006d6d20` (teardown). **Port recipe:** on activity success, for
each level row not already marked done in the save, grant `Props_Award` respect (scaled) and
`Cash_Award` cash, apply `Reward`/`Reward2`, set the done bit, and bump the type's completion
counter. Granting is idempotent per level via the save bitmask, so a replay pays nothing. This is a
**teardown-time batch grant**, which matches the 2026-10-11 user observation in §4 (cash appears on
return to free-roam). **OPEN:** what `0x005e97a0` gates on (success vs fail, and whether the call
fires on the exit-to-free-roam transition); whether the §1-§2 diversion singletons ever reach this
function (they are not `Activity` records, so probably not). §4's "10 OPEN diversion types"
question is therefore unchanged.

**Shared activity_types flags (data):** all eight carry `auto advance level`, `reset noteriety
each level` and `fail on death`. All except Escort also carry `remove noteriety spawns`. Escort
instead has `do initial warp`. Extra `Disable_flags` are `disable all stores` for all eight; Fraud
adds `disable distant spawns` + `disable roadblocks`; Trafficking adds `disable crib`, `disable warp
triggers` and `disable attack helis`; Tank Mayhem adds `disable roadblocks`. **Universal lose
condition: player death.** Area/abandon rules differ by type and are
not a shared test (FINAL, 2026-10-12): Fraud never fails for leaving its hood (§6.2). Mayhem
uses a 20 s leash countdown when the instance has an `Activity_Center` (§6.6/§6.7). Escort fails after a 10 s
countdown out of the client's vehicle (§6.8).

### 6.2 Insurance Fraud (`_a_if_*`, per-type reader `0x00625630`)

- **Win:** accumulate `Goal` dollars of fraud payout (co-op: `Coop_Goal`, always 2×) before
  `Duration` seconds run out. **Lose:** timer expiry, or death.
- **Levels:** DT `Goal`/`Duration`/`Max_Spots`/`Crazy_Hit_Chance` = 80000/210/2/.2,
  200000/300/3/.3, 450000/420/4/.4. SW = 70000/225/2/.2, 200000/300/2/.3, 450000/420/3/.4.
  Constant across all instances: `Crazy_Chance .725`, `Crazy_Min/Max_Speed 20/40`,
  `Ring_Money_Percent 1.0`, `Bonus_Distance 9999`.
- **Class (§1 pattern), CONFIRMED:** vtable `0x0112aaa4` (40 slots). Slot 36 (`0x0112ab34`) is
  the per-instance reader `0x00625630`. Slot 11 (`0x0112aad0`) is the **per-frame tick
  `0x00625a30`**. The same slot indices hold for Mayhem (`0x0112de04`) and Escort (`0x0113380c`).
  Instance fields: `Goal` at `+0x58`, `Duration`×1000 (ms) at `+0x60`, `Max_Spots` at `+0x64`,
  `Crazy_Hit_Chance` at `+0x28`, and the running score (float $) at `+0x44`.
- **Scoring: port recipe (2026-10-12, FINAL; decompiled).** One "hit" is one player ragdoll
  episode. The game's ragdoll begin/end hooks (`0x009a48e0`/`0x009a5520`) call `0x00623a00`, which
  calls `0x00622aa0`. On ragdoll **begin** it clears the hit record and arms it. On ragdoll **end**,
  if the hit is armed, it banks the hit via `0x00621670` and starts a 3000 ms cooldown.
  1. **Per impact during the ragdoll** (`0x00623ad0`, called from the physics contact handler
     `0x00781650`; the local player only):
     - `t = 60·dt`, where dt is the frame-time global `0x0132a0ac` divided by the constant
       `1/60` at `0x0112ad50`.
     - `massMult = lerp(Mass_Min_Multiplier, Mass_Max_Multiplier, s)`, floored at 1.0. Here `s` is
       the other body's mass, inverse-lerped and clamped over `[Mass_Min_Mass, Mass_Max_Mass]`.
       It applies only when `s > 0` and every relative-velocity component exceeds `Mass_Min_Speed`.
       Otherwise `massMult = 1`.
     - `crush = |impact normal speed| × massMult × Crush_Damage_Per_Speed × t`.
     - `scrape = |relative velocity|² × Scrape_Damage_Per_Speed × t`.
     
     Then `0x00623120` runs. It counts only while scoring is allowed: the player has reached the
     active hood (latched by `0x00621e00`), or is inside a bonus spot, or Adrenaline mode is on.
     - `raw = (crush + scrape) × Money_Conversion`.
     - The impact is ignored unless `raw ≥ Damage_Multiplier × Min_Money`.
     - Otherwise `hit.base += raw`, then `hit.base += round(Body_Multipliers[part] × raw)`.
       Shipped values: Head 5, Chest 4, Groin 3, Arm 2, Leg 2. Unlisted parts use 1.0.
     - Each accepted impact also sets the hit's base multiplier slot (`+0x58`) to 1.0. Vehicles
       struck go into a per-hit list of at most 6 distinct vehicles (`0x00622360`), which feeds
       Vehicle, Crazy_Driver and luxury-car. The witness count within `Witness_Bonus_Radius` comes
       from `0x00620a80`.
     - While the player is on fire, `hit.base += On_Fire_Bonus × dt` per frame (50 $/s).
  2. **Category value** (`Multiplier_Data` grid, `0x0061dcc0`): `f(x)` is the maximum over up to 4
     tiers of `x < Minimum ? 0 : min(Multiplier_Min + (x − Minimum) × Per_Multiplier,
     Multiplier_Max)`. A non-positive `Multiplier_Max` means no cap. Only the `*_Element` blocks are
     read; the same-named top-level grids in the file are ignored. The shipped blocks are:
     - Airtime (seconds): `Cash_Instead`, tiers 1/3/5 s → $0-500 / 500-1500 / 1500-5000.
     - Linear_Dist: `Cash_Instead`, tiers 1/10/20 → $0-180 / 180-580 / 580-5000.
     - Windshield: `Cash_Instead`, $500 + 225 per extra, capped at 5000.
     - Vehicle: multiplier, from 2 vehicles: 1 + 0.5 per extra.
     - Surfing: multiplier 0 to 9, +0.15 per unit.
     - Crazy_Driver: multiplier 1 to 2.
     
     Witness, Cop_Witness, Civil_Vehicle, Pinball and Cliff_Diver have no `*_Element` block in the
     shipped `fraud_globals.xtbl`, so they contribute 0.
  3. **Hit value** (`0x00621450` × `0x00621230`). Both sums are **additive**:
     `hitValue = (base + Σ Cash_Instead categories) × (Σ multiplier categories + bonusSlots)`.
     `bonusSlots` is the base 1.0 plus 2.0 for each of these per-hit slots: a luxury car
     (`Luxury_Cars`) among the vehicles struck (`+0x6c`), the co-op chain (`+0x78`), the co-op team
     hit (`+0x7c`), and the co-op gang bonus (`+0x80`). The banked
     value is then clamped to the instance cap at `+0x5c` (its writer was not located). It is also
     multiplied by `Adrenaline_Dollar_Multiplier` (1.0) when the hit's adrenaline flag is set.
  4. **Bank** (`0x00621670`):
     - `score(+0x44) += hitValue`. A bonus spot containing the player also accumulates
       `spot.total += hitValue`, and once `spot.total > Goal × Ring_Money_Percent` the spot is
       consumed (`ACT_FRAUD_BONUS_DONE`).
     - HUD: `ACT_FRAUD_TOTAL_LAST_HIT`.
     - **Adrenaline meter** (`0x00620020`): the meter gains the `Adrenaline_Grid` bonus,
       interpolated by hit value (1000/10000/20000 → 0.05/0.075/0.10). The gain is multiplied by
       `Adrenaline_InArea_Mult` (3) when the hit landed in a bonus spot. The meter is capped at 1.0,
       and reaching 1.0 sets Adrenaline mode (`ACT_FRAUD_ADRENALINE_READY`).
     - The in-run $ are **score only**: the integer clamp `0x00960160` is a display clamp, not a
       grant. The reward is the §6.1 ladder.
- **Win / fail (tick `0x00625a30`), FINAL.** Each frame:
  - **Win:** if `score(+0x44) ≥ Goal(+0x58)`, the tick shows result HUD hint 9, waits a 1000 ms
    timer (state `+0x40` = 2), then ends with success.
  - **Fail:** once the `Duration` timer has expired, the tick fails ("fail time") unless the
    player (or the co-op partner) is mid-ragdoll or Adrenaline mode is active. Those cases defer
    the fail, so a hit in flight is always banked first.
  - Leaving the hood never fails the run. It only stops scoring and posts
    `ACT_FRAUD_GO_SCORING_AREA`. Death fails it via §6.1.
- **OPEN (minor):** the writer of the per-hit cap `+0x5c`; the meaning of bonus slots `+0x70`,
  `+0x74` and `+0x88`, which are summed into the multiplier but were never seen set.

### 6.3 Trafficking (`_a_dt_*`, `Deal_Use_Count` read at `0x0066ee10`, outside any defined function)

- **Win:** drive the dealer (top-level `Dealer_Type`, e.g. `npc_Pierce`) in the activity car
  (`Vehicle_Type`, `Vehicle_Health 25000`, speed override 60 mph) through `Deal_Use_Count` deals,
  chosen from `Deals/Deal` (`Deal_Nav`, `Buyer_Spawn`, `Buyer_Type`), then reach `Ending_Nav`.
  **Lose:** dealer or player death, or car destroyed (HIGH CONFIDENCE from `Vehicle_Health`; the
  code was not traced). **No timer.**
- **Levels:** `Police_Notoriety`/`Gang_Notoriety`/`Deal_Use_Count`/`Max_complications`. DT =
  0/2/2/2, 1/3/3/2, 1/5/4/3. NE = 1/2/3/2, 1/4/4/2, 1/5/5/4. Co-op variants
  `Coop_*_Notoriety`/`Coop_Notoriety_Vehicle_Count` are one step higher.
  `Level_Complications` = `{buyer_flee .3, buyer_flee_shooting .4}` (chance per deal, at most
  `Max_complications`). The loadout `Weapons` is Pistol, SMG (250 ammo), grenade ×4 and Shotgun.
- **Scoring:** none. This is a pass/fail activity, and the reward is the §6.1 ladder only. Note
  that the top-level `Cash_Award 2000`/`Props_Award 2000` siblings are **not read**: `0x0061c6e0`
  reads the awards only inside `Level`.

### 6.4 Snatch (`_a_sn_*`, `Ho_Win_Amount` read at `0x0065f6a3`, outside any defined function; text descriptor `0x00653fdd`)

- **Win:** deliver `Ho_Win_Amount` rescued NPCs to the drop-off before `Duration` runs out
  (co-op: `Coop_Duration`). Drop-off radii are `On_Foot_Dropoff_Dist 6`/`Vehicle_Dropoff_Dist 8`;
  the pickup becomes "in range" at 60. **Lose:** timer expiry, or death.
- **Levels (`Ho_Win_Amount`/`Duration`/`Coop_Duration`):** NE = 4/320/350, 5/400/440, 7/560/620.
  NW (Kinzie variant) = 3/270/300, 5/270/240, 6/480/530.
- **Complications:** `Recapture_Complication` (`Recapture_Chance`, `Cooldown 45`,
  `Damage_Threshold 35`, `Total_Recaptures`) plus per-pickup `Standard_Pickup` vs `Ho_Rescue`
  (pimps and thugs guard; `Reroute_Dist 30`). Notoriety bump is 1 for the gang
  (`Notoriety_Type`), clamped to police 0-1 and gang 0-3.
- **Scoring:** none beyond the count. Reward per §6.1.

### 6.5 Heli Assault (`_a_ha_*`)

- **Win:** complete `Objectives_Needed` (1 for every instance) objectives from the gunner seat.
  The objective kind for the instance's level index comes from top-level
  `Vehicles_destroy_objectives`, `Aircraft_destroy_objectives`, `Protect_target_objective`
  (escort a vehicle, `Vehicle_hit_points 20000`, enemy cars every `Time_between_spawns` 12 s, or 10 s
  in co-op, `Chance_of_rocket_launcher .15`) and `Scripted_gang_fight_objective`, keyed by
  `Level_number`. **Lose:** death, the player heli destroyed (`Heli_Health_Multiplier 5.0`) or the
  protected car destroyed. **Timer:** `Time_For_Level 300`, but `Use_Timer False` in all 6
  instances, so it is untimed.
- **Levels:** the rows are identical except for the award ladder. Difficulty lives in the
  objective blocks (vehicle types, occupant counts). `Friendly_Fire_Mult .05`, spawn ring 96-160.
- **Scoring:** none. Reward per §6.1. **OPEN:** no per-type reader located (`Objectives_Needed`
  has no static string reference).

### 6.6 Mayhem (`_a_mh_*`, `Destruction_Goal` read by `0x00667c40`, the only reference to that string, so HIGH CONFIDENCE shared with Tank Mayhem)

- **Win:** reach `Destruction_Goal` dollars of property damage (co-op: `Destruction_Goal_Coop`)
  within `Duration`. **Lose:** timer expiry, or death.
- **Levels (`Goal`/`Coop`/`Duration`):** NW = 90000/200000/300, 200000/320000/240,
  300000/500000/300. SW = 115000/225000/300, 200000/400000/300, 250000/550000/300.
  `Notoriety_Settings`: police 0-2, gang 2-3. Unlimited-ammo loadout of Pistol, grenade, SMG and
  Shotgun.
- **Scoring (`Combo_Info`, data):** each destroyed object adds its destruction $ to a running
  combo. The combo multiplier grows by `Human_Combo_Multiplier 0.01`, `Vehicle 0.04` and
  `Mover 0.02` per kill of that class, capped at `Combo_Max_Multiplier 2.0`. The combo window decays
  faster as the combo's $ grows: `Combo_Decay_Cutoffs` maps `Destruction_Value`
  4000/8000/16000/25000/35000/50000 to `Decay_Mult` 2-7 (with matching HUD `Start_Text_Intensity`).
  Warning beeps play at 50/20/0 % remaining (1500/1000/500 ms).
- **Class, CONFIRMED:** one class serves Mayhem, Tank Mayhem and DLC Ball Mayhem. Its vtable is
  `0x0112de04`: slot 36 is the reader `0x00667c40`, and slot 11 is the **tick `0x00664320`**. The
  type is stored at `+0x34`: 10 = Mayhem, 9 = Tank Mayhem, 0x10 = Ball Mayhem. Weapons are loaded
  only for 10; repair nodes and HVTs only for 9 and 0x10. A derived vtable `0x0112dea4` (reader
  `0x00669240`: personas, `Combo_Beep_Thresholds`, HVT sightings, tank spawn) adds audio and HUD
  extras only. Fields:
  - goal `+0x9c`, banked $ `+0x98`
  - level timer `+0x74`, combo timer `+0x80`, leash timer `+0x8c` (each `{deadline, paused-remaining,
    clock}`)
  - combo window `+0xa0`, combo kill count `+0xa4`, combo $ `+0xec`, combo multiplier `+0xf0`,
    decay speed `+0xf4`, next-cutoff index `+0xcc`
  - class multipliers `+0xd8`/`+0xdc`/`+0xe0`, `Completion_Mult` `+0xe4`,
    `Combo_Max_Multiplier` `+0xe8`
  - run state `+0x16c` (0 running, 1 wrap-up, 2 failed)
- **Scoring: port recipe (2026-10-12, FINAL; decompiled).** Tweaks come from `tweak_table.xtbl`
  (registered in-code; shipped values in brackets):
  - `Tank_mayhem_human_base_value` [100]
  - `Tank_mayhem_combo_start_time_ms` [15000]
  - `Tank_mayhem_combo_max_time_ms` [30000]
  - `Tank_mayhem_combo_increment_time_sp_ms` / `_coop_ms` [2500 / 2500]
  
  Despite the names, these tweaks apply to all three types.
  1. **Per kill/destroy event** (`0x00660dc0(value, classMult)`): scores only if the leash timer
     is paused (the player is inside `Activity_Center`, or the instance has none), the victim is
     inside an assigned hood (`0x00662ac0`), and combos are enabled (`+0x170`).
     ```
     comboCount += 1
     comboMult  += classMult;  if Combo_Max_Multiplier > 1: comboMult = min(comboMult, Combo_Max_Multiplier)
     add        = round(value × comboMult)
     banked    += add   (unsigned, saturating; this is the goal counter, credited immediately)
     comboCash += add
     ```
     - **Combo timer** (`0x00660af0`): if the timer is running, extend it by
       `min(round(increment / speed), window − remaining)`. Otherwise start it at
       `min(round(start_time / speed), window)`.
     - **Decay cutoffs:** while the next `Combo_Decay_Cutoff` has `comboCash ≥ Destruction_Value`
       and `Decay_Mult ≥ 1`: `newSpeed = Decay_Mult`, plus `Completion_Mult` if the goal is already
       met. Then apply `0x00660a20(newSpeed)`: `r = newSpeed / speed; speed = newSpeed;
       window = round(window / r); remaining = round(remaining / r)`. Move to the next cutoff.
       `Decay_Mult` is absolute, not cumulative.
  2. **Event values by class:**
     - **Human** (`0x00664dc0`): the human base value tweak, or the `Value` of an HVT member.
       Multiplier: `Human_Combo_Multiplier`. Kills by NPCs flagged as non-player attackers are
       excluded.
     - **Mover/object** (`0x00664f70`): `min(sqrt(bbox volume) × 100 + 300, 1500)` (`0x006780e0`),
       or the HVT `Value`. Multiplier: `Mover_Combo_Multiplier`.
     - **Vehicle** (`0x006668c0`): the vehicle record's own value field (vehicle-info `+0x4ac`;
       xtbl field name not matched), or the HVT `Value`. Multiplier: `Vehicle_Combo_Multiplier`.
       The ×2 applied when the vehicle is burning/exploded affects only the floating HUD number,
       not `banked`.
  3. **Combo end** (combo timer expired, `0x00660bd0`):
     - Show `ACT_TANK_MAYHEM_COMBO_TOTAL` = `comboCash`.
     - Reset `comboCount = 0`, `comboCash = 0`, `comboMult = 1.0`, `cutoffIndex = 0`,
       `window = max_time`, and `speed = 1.0` (plus `Completion_Mult` if the goal is met).
     - Nothing extra is banked at combo end: the $ was already credited per event.
     - **Answer:** `Completion_Mult` (Mayhem 4.5, Tank 0.0) only speeds up the combo timer after the
       goal is reached, so the last combo winds down fast. It is not a payout.
- **Win / fail (tick `0x00664320`), FINAL:**
  - **Win:** `banked ≥ goal` → state 1 (wrap-up). The live combo speed is raised by
    `Completion_Mult`. When the combo timer expires, the combo is closed. Completion is reported
    by slot 4 (`0x006641c0` → `0x00663d70`). That function returns true when every HVT group leader
    with class bit `+10 & 0x20` passes `0x00a2afc0`, or when there are no HVT groups. HIGH
    CONFIDENCE "HVT objectives settled"; not traced further.
  - **Fail** → state 2 (`ACT_MAYHEM_FAIL` "You're out of time" for type 10, `ACT_TANK_MAYHEM_FAIL`
    for 9 and 0x10) when any of these happens:
    1. The level timer `+0x74` expires (`Duration`).
    2. The leash timer `+0x8c` expires: see §6.7.
    3. Type 9 or 0x10: the player's (or partner's) tank is destroyed.
    4. Death (§6.1).
  - Leaving the assigned hoods is **not** a fail. It shows `ACT_TANK_MAYHEM_LEFT_HOOD` ("GET BACK
    TO YOUR ASSIGNED AREA!") with a waypoint, and kills there score nothing.

### 6.7 Tank Mayhem (`_a_tm_*`, same `0x00667c40` reader)

- **Win/lose:** as Mayhem, driving `Tank = sp_tank01` inside `Activity_Center` (`Radius 700`).
  **Leash, FINAL:** the reader arms the leash timer at 20000 ms and paused. Each frame the tick
  takes the closer player's distance to `Center`:
  - **Distance ≥ `Radius`:** if the timer is paused, it is resumed and reset to 20000 ms, and the
    tick shows `ACT_TANK_MAYHEM_STRAYING_AWAY` ("GET BACK TO THE TARGETS!").
  - **Distance < `Radius`:** if the timer is running, it is paused and the message is cleared. The
    radius ring is shown from `0.75 × Radius`.
  
  So each exit gives a fresh 20 s, the countdown freezes while back inside, and **expiry fails the
  run**. No events score while the countdown runs.
- **Levels (`Goal`/`Coop`/`Duration`):** DT = 125000/225000/150, 350000/600000/120,
  400000/850000/210. NE = 250000/500000/120, 500000/1000000/210, 600000/1200000/210.
  NW = 150000/275000/240, 450000/800000/300, 350000/625000/400. Notoriety is police and gang 3-4.
  `Repair_Node_Info`: 3 repair pickups (SP and MP).
- **Scoring:** the Mayhem combo with `Vehicle_Combo_Multiplier 0.05`, cutoffs
  5000/10000/20000/35000/50000/75000, and `Completion_Mult 0.0`. **Plus `High_Value_Targets`:**
  `Crowd` and `Convoy` groups, whose members carry a fixed `Value` (5000 for targets, 1000 for
  convoy guards). The HVT `Value` replaces the class base value and is still × the combo multiplier
  (§6.6). FINAL.

### 6.8 Escort (`_a_es_*`; text descriptor `0x00685f1f`; `Satisfaction_Time` read by `0x0068a040`)

- **Win:** fill the client's satisfaction by driving for `Satisfaction_Time` seconds of
  accumulated "good" time (60/75/90 NE; 65/75/95 SW Tiger; 160/180 DLC Genki). Pursuing paparazzi
  must not film too much: the `Footage_Limit` meter (co-op: `Coop_Footage_Limit`) fails the run when
  it fills. **Lose:** footage limit reached, or death. Escort has no `remove noteriety spawns`.
- **Complications (top level, data):** chased by `Enemies` (News van spawn 1.0; IRS and Spouse
  .15, `Give_Up_Sec 30`), plus per-level client requests (`Stop`, `Crash`, `Air`, `Drive_By`,
  `Drift`, `Near_Miss`, `Trip`, and the co-op button-mash/stick-rotate minigames). Each request has
  `Level`/`Chance`/`Help`, `Time_Between_Complication` is 10 s (11 s in NE_03, 3 s in co-op), and
  there are `Num_Complications` per level.
- **Levels (`Num_Spawns`/`Satisfaction_Time`/`Combos_to_advance`/`Footage`/`Complications`):**
  NE = 2/60/3/50/3, 3/75/5/60/4, 6/90/7/50/5. SW (Tiger, no complications) = 2/65/6/25/0,
  3/75/8/20/0, 4/95/10/20/0. `Percent_penalty_for_wrong_move 1`. Escort constants are in
  `spec-tables-traffic-ai.md` §19.
- **Class, CONFIRMED:** vtable `0x0113380c`. Slot 36 is the reader `0x0068a040`; slot 11 is the
  **tick `0x0068d430`**, which runs fail checks and then a phase switch on `+0x90`. Fields:
  - satisfaction remaining `+0xdc` (ms, starts at `Satisfaction_Time`×1000)
  - footage remaining `+0xe0` (ms, starts at `Footage_Limit`×1000; co-op `Coop_Footage_Limit`)
  - HUD meters: satisfaction `+0x78 = 1 − sat/Satisfaction_Time`, footage
    `1 − footage/Footage_Limit`
  - variant `+0x218` (0 base, 1 Tiger, 2 Genki)
- **Meters: port recipe (2026-10-12, FINAL; decompiled).** The main driving phase is `+0x90` = 3
  (`0x0068c610`). Each frame, in order:
  1. **Filming test** (`0x00687bd0`): for every live paparazzi or pursuer, take the distance `d`
     to the client's vehicle.
     - `d < 25.0` with a clear line of sight (ray test `0x0075df80`) means **filming**. Posts a
       "footage" reaction at most every 500 ms.
     - `25 ≤ d < 50` means only "near". Posts a client complaint line, on a 15 s cooldown.
  2. **If filming:** `footage −= frame ms`. Below 5000 ms left, the tick shows the footage warning.
     No satisfaction is gained this frame.
  3. **Otherwise**, satisfaction is gained only when all of these hold: no complication is active
     (`+0xb2`), no IRS/Spouse enemy is within 25.0 of the client (`0x00687a70`), and no
     cutscene/HUD lock is up.
     - **SP:** `sat −= frame ms` while the player is the driver of the client's vehicle
       (`0x009b93d0`: seat state 3 in that vehicle). Speed and distance do not enter into it:
       satisfaction is pure driving time.
     - **Genki:** `sat −=` the per-frame amount `+0x84`, set by its own minigame.
     - **Co-op:** the passenger minigame adjusts it instead. Each wrong move subtracts
       `round(Satisfaction_Time / (Number_of_combos_to_advance + Num_Complications +
       moves done))`; this phase also handles the success bonus.
  4. **Complications:** an active request pauses satisfaction. A Tiger rage slap that fails
     subtracts `Slap_Rage_Value` from **footage** (`+0xe0 −= +0x20c`).
  5. **Satisfaction full** (`sat < 1`): the tick shows the drop-off checkpoint (`0x0068c180`) and
     moves to phase 4, drive to drop-off. Later phases (5-0x12) handle arrival and the walk-in/exit
     cutscene, then success.
  6. **Fail: footage exhausted** (`footage < 0`, variants 0/2).
  
  The driving phase 2 (`0x00685ab0`) also drains `sat` and promotes to phase 3 once
  `sat ≤ Satisfaction_Time − ceil(Satisfaction_Time / (combos + complications + moves)) × moves`.
- **Fail checks in the tick, FINAL:**
  1. The client (or the Tiger NPC) is dead.
  2. The client's vehicle is destroyed, or the client is out of it with no recovery
     (`CRITICAL_VEHICLE_DESTROYED_VEHICLE_TEXT`).
  3. **Abandoning the client:** the player not in the client's vehicle during driving phases 1-8
     and 0xd-0xf shows `ACT_ESCORT_GETBACK_MSG` and starts a **10000 ms** countdown, with
     `ACT_ESCORT_GETBACK_COUNT_MSG` showing seconds. Expiry fails the run, and re-entering clears it.
  4. Genki (`+0x4c` = 1): the player's distance metric from the vehicle exceeds 900.0 (metric
     `0x00db9640`). **[RESOLVED 2026-10-12, raw bytes:** `0x00db9640(a, b)` returns
     `(a[0]-b[0])² + (a[1]-b[1])²`, the squared distance over the first two floats (`+0`, `+4`) of
     each vector, with no square root (x87 multiply-adds only). So the fail is **farther than 30
     units**, i.e. 30 m (900 = 30²). Which world axes `+0`/`+4` are depends on the caller's
     vectors, not traced here.]**
  5. Death (§6.1).
  
  ~~The fail-reason strings are runtime-built (pointer table `0x01134360…`) and were not resolved.~~
  **[RESOLVED 2026-10-12, raw bytes.]** How the table is built:
  - `0x01134360` holds 16 pointers to 20-byte name records at `0x014bb450 + 0x14·k`, in zero-fill data.
  - Static initializers (`0x00ff4a60`…`0x00ff5107`, one per record) number each record from the counter `0x014bb2bc` and link it into a list. Each stores a literal string pointer at record `+0x10`.

  The entries, in index order:

  | k | name | k | name |
  |---|---|---|---|
  | 0 | `fail client hit` | 8 | `stop` |
  | 1 | `fail client dead` | 9 | `crash` |
  | 2 | `fail stripper dead` | 10 | `air` |
  | 3 | `fail car destroyed` | 11 | `kill` |
  | 4 | `fail car underwater` | 12 | `drift` |
  | 5 | `fail footage` | 13 | `near_miss` |
  | 6 | `fail left car` | 14 | `button_mash` |
  | 7 | `fan` | 15 | `stick_rotate` |

  Entries 0–6 are the fail reasons. They add two not listed above: **client hit** and **car underwater** (and "stripper dead", the Genki/escort-variant client). Entries 7–15 are the escort's event names (fan, stop, crash, airtime, kill, drift, near-miss, button-mash and stick-rotate prompts). These are internal names (HIGH CONFIDENCE: event/voice-line keys), not localised UI text. Which condition raises "client hit" and "car underwater" was not traced.
  Reward per §6.1.

### 6.9 Professor Genki's Super Ethical Reality Climax (`_a_rm_*`, `Running Man`; `Required_cash` readers `0x006509a0` / `0x00651e70`)

- **Win:** reach the exit door (`Level_Exit/End_exit`) holding at least `Required_cash` (co-op:
  `Required_cash_coop`) before `Time_limit`. **Lose:** timer expiry, or death
  (`Level_flags = clear_starting_inventory, infinite_ammo, no_health_regen`).
- **Levels (`Time_limit`/`Required_cash`/`coop`):** DT = 180/1000/3000, 180/2000/4000,
  240/3500/7000. SW = 180/2000/3500, 180/3000/6000, 210/4000/8000.
- **Scoring (global `running_man_globals.xtbl`, data):**
  - **Kill cash:** base 50 (civilian), 50 (gang), 500 (hunter). Bonuses are One_hit 15/15/75,
    Melee 5/5/50, Headshot 20/20/25, Nutshot 25, Trap_Kill 30/30/0, Human_shield 0/0/24 and
    Sword 5/5/15.
  - **Targets:** Ethical +5 s/+$50. Unethical −$100/−10 s. Money +$100. Time +25 s.
    Button +$100. Health +225 HP.
  - **Trap evades:** electric +$70, fire +$50.
  - **Combo:** the multiplier table maps 2…12 consecutive scores to ×1.5…×6.5 (step 0.5). The
    combo timer is `Max_combo_ticks 2000` at `Base_decay_rate 100`, sped up ×1.1/1.25/1.5/2/5 past
    combo values 200/400/600/800/1000.
  - **Presents:** `Explosion_Timer 30000` ms, weapon drops, `Alternative_Cash_Amount 100`.
  - **Events:** `Event_Multiplier 3`.
  - **Trap damage:** spike 50, electric 50, fire 1 per tick.
- **Reward:** §6.1 ladder (`1000/2000/3000` cash). `_A_RM_DT_01` also grants `Homie_Pierce`.
  **OPEN:** whether the cash banked in-run is also paid out (HIGH CONFIDENCE not: it is the exit
  gate currency).

#### 6.9.1 Combo rule (2026-10-18)

Answers Team B spec-sync question 7 (HANDOFF-C `a8053de`). Static read of the executable: the combo
object's reader `0x00677c90` and methods `0x00677fa0`-`0x00678e30`, and the Genki activity's
host/single-player class (combo at `+0x1d18`, scoring globals at `+0x1910`). Labels: **FINAL** = read from
the instruction stream; **HIGH** = strong inference; **GUESS** = not checked.

- **Data (FINAL; reader `0x00677c90`, called from the `Scoring/Combos` reader at `0x0064788a`).**
  `Base_decay_rate` and `Max_combo_ticks` are integers. `Multiplier_table/Multiplier` rows (max 16) and
  `Decay_rate_table/Decay_rate` rows (max 8) each hold an integer `Threshold`, a float `Factor` (default
  1.0) and an optional float `Intensity`. A row is kept only if `Threshold` ≥ 0, it is strictly greater
  than the previous kept row's `Threshold`, and `Factor` > 0. `Beep_table/Beep` rows (max 4; child lookups
  are case-insensitive, so the shipped `Beep_Table` matches) hold a float `Threshold` in [0, 1], strictly
  decreasing, and an integer `Interval` in ms, > 0. Shipped values: multiplier 2…12 → ×1.5…×6.5; decay
  200/400/600/800/1000 → 1.1/1.25/1.5/2.0/5.0; `Max_combo_ticks` 2000; `Base_decay_rate` 100; beeps
  0.5/0.25/0.1/0.0 → 1000/750/500/250 ms.
- **State (FINAL).** Multiplier level m and decay level d (−1 = none), ticks T, peak P, combo count N,
  combo bank B (cash), a "multiplier reached" flag and an "expired" flag. Reset (`0x00678570`) sets m = d =
  −1 and everything else to 0/false.
- **Current factors (FINAL).** Multiplier M = `Multiplier[m].Factor`, or 1.0 when m = −1 (`0x00677fa0`).
  Decay factor F = `Decay_rate[d].Factor`, or 1.0 when d = −1.
- **Add(n, cash, ticks) (FINAL, `0x00677ff0`).** (1) T = min(T + ticks, `Max_combo_ticks`); P = max(P, T).
  (2) If T ≤ 0, stop; nothing else changes. (3) N += n; if row m+1 exists and N ≥ its `Threshold`, m += 1
  and the "multiplier reached" flag is set. (4) B += cash; if row d+1 exists and B ≥ its `Threshold`,
  d += 1. Each level moves at most one step per call. N and B saturate at 2³¹−1.
- **Refill (FINAL, `0x006785b0`).** ticks = trunc(trunc(`Base_decay_rate` × F) × k), then Add. The only
  live caller passes k = 4.0 (float at `0x012a2f6c`), so each counted score refills four seconds of decay
  at the current rate: 400 / 440 / 500 / 600 / 800 / 2000 ticks for F = 1 / 1.1 / 1.25 / 1.5 / 2 / 5 (then
  capped at 2000).
- **Decay (FINAL, `0x00678e30`).** Called once per frame by the activity tick `0x0064f890` with dt = the
  frame-time global `0x0132a0ac` (seconds). If T > 0: T −= trunc(trunc(`Base_decay_rate` × F) × dt); if
  T ≤ 0, T = 0 and "expired" is set. The nominal rate is `Base_decay_rate` × F ticks per second (100 to
  500). The two truncations make it frame-rate dependent: 3/3/4/5/6/16 ticks per frame at dt = 1/30 s.
- **What counts as a consecutive score (FINAL).** Only a kill, credited by the kill callback `0x0064c1f0`
  (vtable slot `0x0112eb84`). It reads M first, pays the victim row's `Base` and each applicable bonus as
  trunc(value × M), sums the terms, then refills with n = 1 and cash = that sum. So the kill that raises N
  is paid at the old M: kill k of a combo is paid at the factor for count k−1 (kills 1-2 at ×1.0, kill 3 at
  ×1.5, … kill 13 onward at ×6.5). "Consecutive" means the next kill lands while T > 0: within about four
  seconds of the last refill, plus whatever was stored (up to 2000 ticks).
- **Other cash events (FINAL; trap-evade routing HIGH).** Target pickups (callback `0x0064f350`, vtable slot
  `0x0112eb88`) and `Button` (`0x0064a4b0`) pay trunc(cash × M), add it to the run total, then call
  Add(0, cash, 0). They add to B (and can step d) only while a combo is live. They never count, never
  refill and never step m. The pickup rows are 12-byte {cash, time, …} records in the `Targets`/`Trap_Evade`
  block: Ethical, Money, Time, Unethical (both deductions stored negated), Health, Fire, Electrical. Trap
  evades use this path, judging by the row layout (HIGH). Time is never multiplied.
- **Breaks and resets (FINAL).**
  - Expiry: in the same frame as the decay, if "expired" is set, the activity reports the combo end (event
    3 carrying B, `0x00649b40`) and resets the combo. B is not paid again: each score's cash already went to
    the run total when it was scored.
  - A pickup whose cash or time is negative (the shipped `Unethical_Target`) reports B and resets the combo
    instead of calling Add. Its cash deduction is trunc(−100 × M), so the combo factor in force multiplies
    it. The run total is floored at 0: if total + delta < 0, the total becomes 0.
  - Level start (the state handler at `0x0064b618`) resets the combo.
- **Beep (HIGH, `0x00678610`, runs after every decay).** Only while the "multiplier reached" flag is set:
  fraction = max(T, 0) / P; take the first beep row with fraction ≥ its `Threshold`. When the beep timer
  has run for at least that row's `Interval` ms, re-arm it and play the sound hashed from
  `SYS_HUD_CNTDWN_POS`. If the timer is unarmed, it is armed and nothing plays that frame.
- **Not part of the rule.** `Intensity` (decay rows 0/0.2/0.4/0.7/1.0; absent from the multiplier rows, so
  0) is returned by `0x00677fc0`. Its one caller, `0x0064cdc3`, is in the second (co-op client) class.
  GUESS: it drives a presentation intensity. Each frame the activity also sends HUD event 1 carrying
  trunc(M).
- **Port note for `GenkiComboFactor(g, N)`.** It returns the factor for count N. Pay kill k with
  `GenkiComboFactor(g, k − 1)`, i.e. read the factor before the increment.

Test vectors (shipped data, dt = 1/30 s; reference simulation `tools/scratchpad/genki_combo_sim_1018.py`):

| # | Input | Expected |
|---|---|---|
| V1 | Fresh combo, one civilian kill (`Base` 50), then decay only | Cash 50 at ×1.0; T = 400; expires on the 134th frame (≈ 4.47 s) |
| V2 | 13 civilian kills, no decay between them | Cash 50, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300, 325; M after kill k = 1.0, 1.5, 2.0 … 6.5 (6.5 from kill 12); T 400, 800, 1200, 1600, then 2000 (capped from kill 5); d steps at kills 4 (B 275), 5 (400), 7 (725), 8 (925), 9 (1150) |
| V3 | Fresh combo, hunter kill: `Base` 500 + `Headshot` 25 | Cash 525; B = 525 but d = 0 only (F 1.1; one step per call); T = 400 |
| V4 | 3 civilian kills (total 175), then an Unethical target | Deduction trunc(−100 × 2.0) = −200 cash and −10 s; total floored 175 → 0; combo ends (event 3 with B = 175) and resets |
| V5 | No live combo, Money target | +100 (× 1.0) to the run total; B stays 0, T stays 0 |
| V6 | Kill, 60 frames of decay, kill | T 400 → 220 → 620; the second kill is paid at ×1.0, then M = 1.5 |
| V7 | d = 4 (F 5.0), T = 2000, no more kills | 16 ticks per frame; expires on the 125th frame (≈ 4.17 s) |

Residual: not checked whether `0x0132a0ac` is the clamped variable dt or the fixed 1/30 s (the vectors assume 1/30). Not traced: the co-op client copy (combo at `+0x197c`, refill scale sent ×1000) and `Event_Multiplier`'s effect on the combo. The flag-taking refill at `0x0064a2c0` has no caller in the image.

### 6.10 Open items for §6 (summary)

Closed on 2026-10-12: the in-run scoring for Fraud, Mayhem/Tank Mayhem and Escort, and their
area-leave/abandon fail tests (§6.2, §6.6-§6.8). The `k` constant at `0x012a2d70` is the double
1.0 (read from the image), so the §6.1 respect bonus is `Props_Award × (scale − 1)`. Still open:
`0x005e97a0`'s success gate; the "all instances complete" → `activity_types.Unlockables` path;
Fraud's per-hit cap writer (`+0x5c`); and the Escort fail-string table contents.

## 7. Clean-room check

Plain hex addresses throughout. No Ghidra auto-names, no decompiler pseudocode, no
`FUN_`/`DAT_`/`iVar`-style tokens used as if real identifiers. No leaked Volition/Havok/
Audiokinetic/RAD material.

## Changelog

- **2026-10-18 — new §6.9.1 Genki combo rule (FINAL/HIGH)** (SPEC TEAM, static read of the executable). Answers Team B spec-sync question 7: only kills count and refill four seconds of decay at the current rate; decay is `Base_decay_rate` × F ticks per second, with F stepped by the combo cash bank; a kill is paid at the multiplier in force before its own count step; an Unethical target breaks the combo and the run total floors at 0. Seven test vectors. Clean-room self-check: no decompiler tokens.

- **2026-10-12 — new §6 per-activity rules (FINAL); Clean-room check renumbered to §7; §4 reward-grant path CLOSED for the instanced activity family** (SPEC TEAM). The common `Activity` reader is `0x0061c6e0` (up to 6 levels; `Cash_Award`/`Props_Award`/`Reward`/`Reward2`). The teardown batch grant is `0x0061bb50` (caller `0x005e97a0`), which calls `0x009e2440` and `0x0094d920` once per not-yet-saved level. Rules for the 8 activities were taken from the per-instance `_a_*` xtbls and `running_man_globals.xtbl`.

- **2026-10-11 — §4: user-observed ground truth narrows the reward-grant search for all 10 OPEN
  diversion types to activity teardown/exit-to-freeroam** (SPEC TEAM, self, note only, no new RE
  this entry). The user confirmed the cash reward visibly arrives only on loading back into
  free-roam after an activity, favoring hypothesis (b) (batch-grant on teardown) over continuing
  to check in-activity tick/score code, and explaining why those searches kept coming up empty
  across every diversion type, not just Barnstorming.

- **2026-10-11 — `photo_op` CLOSED, the last unresolved diversion type** (SPEC TEAM, self, direct
  harness query, no agent dispatched). It is not a diversion-class instance at all — its `.xtbl`
  loader is a one-time global-config load (`Saints_Loved`/`Saints_Hated` name lists,
  `Trigger_Effect` index), not a per-row record, which is why the triad/constructor searches kept
  failing. Found the real consumers by xref: a tick function that finds a random matching NPC and
  plays a reaction, and an effect-trigger function that spawns a camera prop and shows
  `"PHOTO_OP_USE_MESSAGE"`. Neither calls either reward primitive — CONFIRMED reward-less, same
  bucket as Barnstorming. Closes the diversion-reward investigation's last open type (0 unresolved
  remaining). Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — `0x00bda0e0` opened in full: conclusively pure telemetry, orchestrator's
  stat-tracking hypothesis does NOT hold for this function** (SPEC TEAM, agent
  `a528bdcf295be8702`, independently re-verified by direct query before splicing —
  `0x00bda0e0`'s and `0x00423270`'s raw listings both matched exactly). Orchestrator-routed
  follow-up to the convergence trace below: `0x00bda0e0` receives both scaled Respect and Cash
  together, and both are persistently tracked stats, so a shallow "telemetry" label wasn't
  enough — fully opened the entire recursive callee tree. Result: a getter, a read-only
  state-snapshot filler (~14 further read-only callees), a sampling-rate gate
  (`rand()%100`-style), a named GDS middleware event serializer, and a critical-section-protected
  async job queue — no write anywhere touches a respect/cash/save field, no call anywhere reaches
  `0x009e2440`/`0x0094d920`. The three gate flags were confirmed to scope ~78-80 sibling functions
  (same shape, `0x00bd80a0`-`0x00bd94f0`+) — a project-wide "telemetry enabled" toggle, not a
  stats switch. One flag's own runtime default is OPEN (needs its setters or live capture to
  pin down), but doesn't change the conclusion. Closes the `0x00bda0e0` half of the convergence
  question. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — HUD-array reader angle CLOSED, clean negative** (SPEC TEAM, self, direct harness
  query, no agent dispatched). Opened all 4 previously-unopened functions touching the 15-slot HUD
  notification-parameter array (`0x022b7ac0`): all are slot-pool lifecycle management (allocation
  tag-stamping, pool init, two handle-release/destructor variants calling the shared
  `0x007fb4f0` release routine) — no reward-primitive call, no save/stat field write anywhere.
  Closes the "still-unfound second reader" possibility the prior convergence-trace entry left
  open; the real grant mechanism for the 10 "no direct primitive call" diversion types stays
  genuinely unlocated, now exhausted short of a live capture. Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — convergence trace: `base_jumping`'s and `stunt_drifting`'s reward chains meet
  exactly, at a confirmed-inert shared HUD/telemetry system, not a reward mechanism** (SPEC TEAM,
  agent `a6d56e7c68abe3502`, independently re-verified by direct query before splicing —
  `0x00695d50`'s raw listing, `0x007fc2d0`'s raw listing, and `0x00710cb0`'s caller list all
  matched the report exactly). Orchestrator-routed follow-up to the `base_jumping` consumer
  finding below: traced where `0x00695d50`'s scaled `Respect`/`Cash` values go after
  `0x006a4d80`, and did the same for `stunt_drifting`'s score/notify `0x006bcdb0`. Both land,
  call-for-call identically, in (a) the generic telemetry-event pusher `0x00bda0e0` and (b) the
  shared 15-slot HUD notification-parameter array at `0x022b7ac0` (stride `0x58`) via
  `0x007fc2d0`/`0x007fc190` — confirmed to be pure UI-display plumbing (localized toast-text
  format arguments), not a grant path; neither function calls either reward primitive. Also
  confirmed, as negative evidence for future passes: the generic HUD-hint dispatcher `0x00710cb0`
  is itself called by both real primitives when they genuinely grant (`0x0094d920`, `0x009e2440`),
  so its presence in a chain proves nothing about reward status either way. Net result: the real
  grant path for all 10 "no direct primitive call" types remains genuinely unlocated — this closes
  the convergence question (yes, they converge) but does NOT resolve the open mechanism question.
  One concrete lead flagged for any future pass: an xref scan of the HUD-array's readers beyond
  the one field-copy worker opened so far, to check whether rendering the toast has a grant
  side-effect. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — `base_jumping`'s own reward-tier consumer FOUND, resolving its "consumer missing"
  exception from the same-day pass below** (SPEC TEAM, agent `ad0cd4823a21ead90`, independently
  re-verified by direct query before splicing). Earlier search angles (xref-ing the two reward
  primitives directly; opening only the per-type mega-table's own tick/reset slots) missed it
  because it is reached from a completely separate per-attempt state machine, `0x00696310`
  (state==2, "jump complete"), not the mega-table's tick slot at all. The method that worked:
  xref-ing the type's own fixed singleton address, `0x014bc2a8`, directly and checking each of its
  readers individually — re-verified exactly: 10 uses across 4 functions (`0x00694430`;
  the reset function at `006947b3`/`006947bd`; `0x00695640`, not previously named anywhere in this
  file; and `0x00695d50` at `00695ddb`/`00695e5c`), plus one DATA-only reference at `0x00ff5490`,
  matching the agent's own flagged side-finding exactly. `0x00695d50` selects the highest
  `Reward_Tier` entry (array at `this+0x58`, stride `0xc`: `Max_Distance`/`Respect`/`Cash`; count
  at the newly-identified field `this+0x54`) whose `Max_Distance` the attempt's measured distance
  clears, scales `Respect`/`Cash` via the already-proven-inert clamp helper `0x006a4d80`, logs
  telemetry via `0x00bda0e0`, and notifies HUD via `0x007fc2d0`/`0x007fc190`. Its own callee list
  (re-verified by direct query: `00695570 00710e40 0069b260 009da4e0 00bd2de0 006a4d80 00bda0e0
  007f9590 007fc2d0 007fc190`) contains neither `0x009e2440` nor `0x0094d920` — reclassified from
  "ambiguous, consumer missing" into the same "no direct primitive call; reward path elsewhere,
  OPEN" bucket as the other 9 types from the same-day pass below. Net project-wide count at the
  time: 10 with no direct primitive call (real path OPEN), 2 confirmed direct-grant, 1 genuinely
  unresolved (`photo_op` only — CLOSED below, 2026-10-11). Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — `photo_op` CLOSED (orchestrator-routed, direct harness query, no agent
  dispatched): it is NOT a diversion-class instance at all, and it is CONFIRMED reward-less, same
  bucket as Barnstorming — this also explains why every earlier pass's search for its constructor/
  tick/reward-triad kept coming back structurally wrong.** `0x0091d610` (the `.xtbl` loader) isn't
  a per-row record reader — it's a one-time GLOBAL CONFIG loader: `Saints_Loved`/`Saints_Hated`
  are parsed into two fixed global name-list arrays (`0x01308ea8`/`0x01308f28`), and
  `Trigger_Effect` into a single global index (`0x01308fa8`, default `-1`) — there is no per-
  instance record at all, which is why the `Max_Respect`/`Max_Cash` triad method never applied
  here and why the "nearest candidate constructor" (`0x0091d700`) looked structurally wrong (it
  isn't a constructor — it's this config loader's own caller).

  Found the real consumers by xref-ing the three globals directly: `0x0091e2e0` is the actual
  tick/interaction body (gated on state `+0x7c == 4`) — picks a random name from whichever list
  (Saints_Loved or Saints_Hated, via a coin-flip `0x00702a30`), finds a live matching NPC nearby
  (the same generic world-query/filter pair, `0x00458230`/`0x00853b10`, other diversion types use
  for candidate-character lookup), plays a reaction line/pose on success, then advances state to
  `5`. `0x0091ede0` (the `Trigger_Effect` reader) spawns a camera prop entity (type id `9` via
  `0x00456e30`) near the player, shows the literal HUD string `"PHOTO_OP_USE_MESSAGE"`
  (`0x0093d8c0`), and applies the loaded `Trigger_Effect` value as a cosmetic effect parameter
  (`0x0093d8e0`, mode `3`) — a flash/audio cue for the camera, not a reward dispatch. `0x0091eb50`
  is the matching setup/pose-selection sibling. **None of the 3 real functions (nor their own
  depth-1 callees already dumped) call `0x0094d920` or `0x009e2440` anywhere.** Net: `photo_op` is
  a world-interaction/camera-prop flavor system — approach a Saint-loved or Saint-hated NPC, pull
  out a camera, they pose (or react), done — with genuinely no cash/respect payout, matching
  Barnstorming's own already-confirmed reward-less classification, not a data gap. Project-wide
  count, final: 10 with no direct primitive call (real path OPEN), 2 confirmed direct-grant, 2
  confirmed reward-less (Barnstorming, `photo_op`) — 0 remaining unresolved.

  - **Evidence:** full disassembly of `0x0091d610` (the loader) and its own direct xref-identified
    consumers `0x0091e2e0`/`0x0091eb50`/`0x0091ede0`, plus their depth-1 callees, via direct Ghidra
    query.
  - **Confidence:** CONFIRMED for the loader's real global-config shape (not a per-row record),
    for all 3 consumer functions' top-level behavior, and for the absence of either reward
    primitive in any of them or their immediate callees.
  - **Unknowns:** `0x0091eb50`'s own full pose-selection detail (read only to callee-list depth,
    not fully decompiled); whether `photo_op` completion feeds any OTHER system downstream (e.g. a
    photo-album/collectible counter) outside the cash/respect reward question this thread was
    scoped to. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — the 11 remaining "ambiguous" diversion types investigated: 10 confirmed to have
  no direct reward-primitive call in their own chain (real grant path still OPEN — wording
  corrected same day, this is NOT "reward-less," the shipped game visibly pays these), 2 still
  genuinely ambiguous for new, sharper reasons** (SPEC TEAM, agent `aa0589748b08aa6dc`,
  self-picked (this file's own "single largest OPEN item"), independently re-verified by direct
  query before splicing; wording corrected same day per orchestrator, same class of overclaim as
  the footstep-audio item). Found a second constructor shape ("Shape A," no `Init` call, reached
  from the startup driver) alongside the already-known "Shape B" real constructor, and resolved
  more of the per-type mega-table's layout (index 8 = tick, index 14 = name getter). 7 `stunt_*`
  types + `stunt_wheels`/`exploration_diversion`/`mugging_diversion`: own score/notify or tick
  chain opened, no call to `0x009e2440`/`0x0094d920` — ruled out as using THIS grant shape, but
  the real mechanism (a different setter, a generic diversion-complete handler, or Lua) remains
  OPEN. `base_jumping`: at the time of this pass, had a richer 10-tier reward array with no
  located consumer function — ambiguous because the consumer was missing, not because of no
  evidence. **Superseded 2026-10-11 same day, later pass: the consumer was found (`0x00695d50`,
  see below) — `base_jumping` moves into the main "no direct primitive call" bucket.** `photo_op`:
  has no `Max_Respect`/`Max_Cash`-shaped fields at all — the classification method itself doesn't
  apply, remains the sole unresolved case. Net project-wide count (as revised): 10 with no direct
  primitive call (real path OPEN), 2 confirmed direct-grant, 1 unresolved. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

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

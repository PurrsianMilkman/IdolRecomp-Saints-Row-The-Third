# Spec Coverage Summary — Dashboard for Orchestrator

Status report as of 2026-10-10. Purpose: give a high-level read on how complete the reverse-engineering
understanding is for each game system, to help decide where to invest further effort. This is a
management document, not a technical spec — it intentionally omits most hex addresses and does not
carry cleanroom-citation discipline the way the `spec-*.md` files do.

Basis for each estimate is stated per system: either an explicit number already in the specs (e.g.
"93.8% of natives specced"), or a qualitative call made by reading each spec's own confidence-label
counts (DESK-PASS / VALIDATED-BY-DATA / NEEDS-EXE / NEEDS-DATA tallies) and OPEN-items sections.

Three classifications are used for open items:
- **(a) confirmed dead end** — the spec explicitly states static/disassembly analysis is exhausted and a
  live capture (Frida hook, breakpoint, frame capture) is the only remaining avenue.
- **(b) not yet investigated** — a real gap, but no wall has been hit; more static work could close it.
- **(c) blocked on a decision** — needs a scope call, a design answer code can't supply, real game-data
  census, or another team's action (not a technical dead end).

---

## 1. Rendering / Graphics
**Files:** spec-render-pipeline, spec-vertex-format, spec-geometry-format, spec-texture-format,
spec-vint-render, spec-fxo-format, spec-effects-format, spec-low-mips.

**Specced:** ~80–85% (qualitative; texture-format, low-mips, geometry-format, and vint-render are
essentially done; render-pipeline/vertex-format/fxo/effects each carry bounded, named gaps rather than
broad unknowns).

**Key open items:**
- The top-level per-frame driver that sequences the render-pipeline's phase-entry vtable slots was
  never found — only reachable virtually. **(a) confirmed dead end, needs live trace/breakpoint.**
- The CPU-side consumer of tree-vertex POSITION data (vertex-format) was run to a confirmed static dead
  end **twice independently** — no literal callers anywhere. **(a) confirmed dead end, needs live
  capture.**
- Vint UI document-space coordinate math and glyph/text layout are only validated by emulation against
  static code reads, not a real frame capture. **(a) for the live-confirmation items specifically.**
- `rl_static_material+0x8`'s role-override field (selects the reflection pass) — all leads exhausted.
  **(a) confirmed dead end.**

None of these block a reasonable clean-room substitute, but all four are explicitly flagged as
needing dynamic analysis to close for certain.

---

## 2. World Streaming / Environment
**Files:** spec-world-streaming, spec-zone-data-format, spec-terrain-format, spec-foliage-format,
spec-tree-format, spec-resource-dispatch, spec-tables-environment.

**Specced:** ~80–85% (qualitative; tables-environment is explicitly ~92% at the loader-schema level,
the strongest single number in this group).

**Key open items:**
- The per-frame reconciliation driver that triggers zone acquire/release "once per frame" was never
  found despite exhaustive tracing. **(b)/(a) borderline** — disassembly alone hit a wall; untried as a
  live-capture target.
- Whether the `0x2244` heightmap data is ever actually rendered as visible ground geometry — called out
  as **the single most consequential open item**, directly tied to a real live-playtest ground-missing
  bug. Static analysis found no consumer. **Needs live capture.**
- The zone/terrain texturing mechanism (`0x2237` pavement/terrain geometry) — file-side analysis
  reached a confirmed dead end ("if it works some other way, it isn't visible from the file side
  alone"). **(a) confirmed dead end, needs live capture.**
- A related precedent exists: this project's navmesh-registry question (zone-data) was also "dead" by
  static analysis but a live Frida capture refuted that (99.25% success in real gameplay) — the same
  technique should be pointed at the streaming driver and heightmap questions above.

---

## 3. Physics
**Files:** spec-physics-format, spec-physics-runtime.

**Specced:** ~85–90% (physics-format: ~90%, most NEEDS-EXE items closed by a 2026-10 RTTI follow-up
confirming Havok, not Volition-authored, as the engine; physics-runtime: ~80–85%, very actively closed
out through early October).

**Key open items:**
- 6 of 7 vehicle Havok component-override slots (steering/engine/suspension/brakes/wheel-collide) have
  no mapped role; only driver-input is resolved. **(b) not yet investigated.**
- A possible unaccounted direct call from Havok-internal code into game code (`0x00ae8f10`) — flagged,
  not confirmed. **(a)/(b) borderline, needs a targeted dynamic check.**
- Ground-friction coefficient's entry point into the friction-ellipse solve, and bullet/weapon-hit
  raycast detection — both explicitly unexamined. **(b) not yet investigated.**

This is one of the more mature spec clusters — no confirmed dead ends requiring live capture; remaining
gaps are static follow-ups, not walls.

---

## 4. Vehicles
**Files:** spec-vehicle-data, spec-vehicle-geometry, spec-vehicle-damage-runtime,
spec-tables-vehicle-world.

**Specced:** ~78% (data/geometry/world-tables are mature and empirically validated against the full
123-vehicle population; damage-runtime, the newest file, is the weak point at ~60%).

**Key open items:**
- **The literal "HP reaches 0 → begin destruction" decision instruction has never been located**
  despite direct, repeated searching — the closest thing to a confirmed dead end in the vehicle docs,
  explicitly flagged as the highest-priority named lead (candidate functions identified, unopened).
  **(a) borderline confirmed dead end — more static tracing of `0x00a9f4f0` is the next step before
  calling it a true dead end.**
- The per-frame writer of the wheel-suspension/steer/spin override matrix was never located at all (no
  accumulator or bone write found). **(b) not yet investigated.**
- The paint/customization-to-shader-register hookup is proven to exist architecturally but the actual
  connecting link was never found. **(a) genuine bounded dead end, needs live tracing.**
- `.czn_pc` zone-interior data remains parked project-wide (per standing hold), affecting both this
  system's world tables and physics collision. **(c) blocked on a scope decision, not a technical gap.**

---

## 5. Characters / Animation
**Files:** spec-anim-format, spec-anim-runtime, spec-rig-format, spec-morph-format,
spec-tables-animation.

**Specced:** ~75–80% (rig-format, morph-format, tables-animation are late-stage/largely re-derived;
anim-format lags, with 8 of 27 reviewed units still awaiting the executable or more data).

**Key open items:**
- anim-format's clip constructors and permutation/step-table direction — awaiting the executable.
  **(b) not yet investigated.**
- morph-format has two real data conflicts (an 8-vs-30 compactness-trial mismatch; a 100/100-denominator
  conflict for a 95-target file) that need Team B to rerun against real files. **(c) blocked on a data
  re-run.**
- anim-runtime carries ~48 tracked OPEN items (e.g. a per-bone accumulator sub-path selector) — mostly
  not yet investigated, few blocked.
- Two genuine **confirmed, already-closed dead ends** worth noting (no further work needed): rig-format's
  bone-name hash-table-lookup theory was refuted (no runtime reader exists), and
  tables-animation's `anim_set_properties.xtbl`/`anim_prop_sets.xtbl` slots 7/9 are a confirmed dead
  registration (named but never read by any function).

---

## 6. Player Runtime
**File:** spec-player-runtime (treated separately — large and architecturally distinct: player state
machine, movement, camera, melee/combat, vehicle entry).

**Specced:** ~60–65% by volume — 15 major units landed, but the file's own OPEN list has grown past 100
numbered items as sub-investigations keep opening sub-questions.

**Key open items:**
- **Melee hit→damage hand-off.** This is the project's most heavily investigated dead end: the melee
  resolver (`0x00950c30`) was confirmed to never touch the melee damage tables or call the generic
  damage routine directly; a separate swing-time Havok hit test (`0x008cfeb0`) was found with high
  confidence, caching state in a flag byte — but **four independent passes** chasing that flag's
  consumer (including two plausible accept-callback candidates, confirmed to be pure geometric
  predicates with no damage field or sink call) have exhausted the entire statically-reachable call
  graph with no edge into the damage sink. **(a) confirmed dead end — recommended closed-as-exhausted
  unless a live capture becomes available** (an indirect/vtable call or a Havok collision-response
  callback are the only remaining possibilities, neither traceable statically).
- OPEN item #97: the HUD prompt-flag (`+0x2084`, triggers watercraft/weapon prompts) setter — extensively
  chased, two function-pointer tables ruled out, still unresolved. **(b) not yet investigated**, with
  one promising unexplored lead.
- The ground-collision-shape source feeding the Havok character-proxy support query. **(b) not yet
  investigated.**
- Game-mode stack (mode-7/mode-5) push-site exhaustiveness — only HIGH CONFIDENCE, not fully traced
  through every push site. **(b) not yet investigated.**

---

## 7. AI / Peds / Traffic
**Files:** spec-ai-behavior-format, spec-ai-runtime, spec-tables-traffic-ai, spec-conversation-format.

**Specced:** ~90%+ — three of the four files are substantially confirmed/validated against real
base-game data (ai-behavior-format largely re-derived 2026-10-02; traffic-ai validated 32/32 tables;
conversation-format validated 4,984/4,984 files); ai-runtime is a mature 20-section document with most
mechanics resolved.

**Key open items:**
- The team-id↔name registry binding and a world-object registry's ownership remain unresolved —
  **(c) blocked**, parked under the project's standing mode-a/.czn_pc hold, not a technical wall.
- Individual AI goal/action routine bodies (of ~16) and producers of several event kinds (46/81/108).
  **(b) not yet investigated.**
- conversation-format's validation-ladder ordering and per-voice-family file counts. **(c) blocked on
  data/exe re-runs**, not re-derivation unknowns.
- Two items that look like dead ends but are actually already **closed negatives**: ai-runtime's §14
  navmesh/path-point query layer is confirmed dead in every shipped file, and a navpoint-selector
  cluster for cover selection is confirmed not the real mechanism — in both cases the real logic was
  subsequently found elsewhere, so no live capture is needed here.

---

## 8. Combat / Weapons
**Files:** spec-weapons-runtime, spec-tables-weapons-combat.

**Specced:** ~85–90% — tables-weapons-combat is essentially complete at the schema/data level (22/22
tables validated against real base-game rows); weapons-runtime has mapped hit-detection, fire-gating,
ammo, and damage architecture thoroughly but carries deliberate, flagship open gaps.

**Key open items:**
- **The literal hitscan fire→damage call site remains open** (§4.4) — this is the project's own named
  flagship example of a static dead end: three named candidate functions were confirmed NOT it, and
  roughly 70–86 unattributed callers of the damage-sink function remain unopened after repeated passes.
  **(a) confirmed dead end, explicitly needs live/dynamic capture.**
- **Projectile fuse/impact detonation consumer** — the per-frame homing update was found with high
  confidence, but what actually consumes the fuse timer/impact event to apply damage is still open; one
  lead (a `+0x80` damage-apply vtable slot on the projectile itself) was cleanly ruled out — the
  projectile's vtable has exactly 32 slots, none of them that slot. **(b) not yet investigated**, one
  dead lead eliminated.
- Melee's own hit-resolution/damage hand-off (cross-referenced from Player Runtime above) is the
  combat system's other named confirmed dead end.
- Live ammo-count field byte offset and the true per-shot refire-delay consumer in weapons-runtime, plus
  several table-field consumers in weapons-combat (e.g. the `Fire_Cone` cosine test, `combat_tricks`
  Min/Max_Value semantics) that are only name-inferred, never traced to a reading instruction. **(b) not
  yet investigated.**

---

## 9. Missions / Lua Scripting
**Files:** spec-lua-api-behaviour (the project's largest single spec, ~16,800 lines), spec-lua-bindings,
spec-mission-packages, spec-agora-hydra-boundary.

**Specced:** the native-behavior layer (spec-lua-api-behaviour) has an explicit, current project-wide
figure: **93.8% of natives specced** (1,175 distinct natives across mission + side-activity + UI corpora,
1,102 specced, 36 registered-only, 37 open). The binding/registration mechanism layer
(spec-lua-bindings) is separately estimated ~60–65% desk-reviewed-consistent (not independently
re-derived from the executable in most units). spec-mission-packages (archive format) ~55–60%. The
Agora/Hydra boundary — a third-party telemetry/analytics SDK integration, **not** scripting-VM internals
— is ~70% specced within its narrow declared scope.

**Key open items:**
- `thread_close` — the project's one standing, genuinely open native. Grepped exhaustively across every
  readable Lua source (missions, side-activities, shared libs, full UI corpus) with zero hits anywhere.
  **(a) confirmed dead end for static analysis** — needs either an unexamined container or a live/dynamic
  capture.
- The vtable `+0x68` predicate (cited around dlc2_m01/§7.10/§17.5) — independently re-verified to have
  **no constant-stub shortcut; a confirmed impossibility, not an unexplored gap.** **(a) confirmed dead
  end, needs live capture.**
- spec-lua-bindings' biggest named gap: the full roster of C functions registered via
  `vint_callback_lua` — pool mechanism confirmed, specific bound names/pointers not extracted.
  **(a) confirmed dead end for statics, recommended dynamic/breakpoint capture.**
- Agora/Hydra: what dispatches its response-processing state machine has zero confirmed callers found
  (believed to run on an internal worker thread, out of scope to trace further). **(a)/(c)** — technically
  a dead end for statics, but also low-value since the real backend is permanently dead; may not be
  worth pursuing.
- Separately worth flagging: several **real engine logic bugs** (not spec gaps) are already documented
  as concrete mission-blockers in the active changelog — e.g. `vehicle_enter_group_check_done`'s
  straggler-recovery stall implicated in mission m07. The remaining critical work on this system is less
  "spec more natives" and more "close the handful of live-capture-only items and resolve the already-
  identified mission-stall bugs."

---

## 10. HUD / UI / Menus
**Files:** spec-hud-runtime, spec-frontend-menu-runtime, spec-tables-ui-controls, spec-vint-doc-format,
spec-vint-font-format, spec-localization-format.

**Specced:** ~75–85% — core architectural mechanisms (HUD data flow, menu dispatch, control/QTE tables,
vint document/font binary layout, localization lookup) are confirmed by disassembly and in several
cases validated against real shipped data (e.g. localization validated against 159 shipped `.vint_doc`
files); each file carries a tail of byte-offset-level gaps.

**Key open items:**
- The vint-doc keyframe/tween-animation encoding and its runtime interpolation consumer were never
  traced. **(b) not yet investigated** — a real structural gap affecting menu animation.
- The bitmap-image-to-PEG-texture hookup (direct vs. atlas-indirected) is unresolved. **(b) not yet
  investigated.**
- Several HUD render-side consumer traces (live notoriety-score byte offset, which of two candidate
  functions is the real HP-death trigger, ammo clip-vs-reserve field mapping) are high-confidence but
  not independently proven by tracing an actual render-side consumer. **(b)/(c) borderline** — would
  benefit from live capture to confirm.
- The one item explicitly labeled as needing live capture rather than further static work: vint-font's
  glyph-run layout numbers were derived in emulation with a hand-supplied texture size; **a real frame
  capture would close the loop.** **(a).**
- Frontend menu's fresh-game-defaults (starting cash, wardrobe, unlockables) write site and the literal
  pause-menu input binding remain unfound. **(b) not yet investigated.**

---

## 11. Audio
**Files:** spec-audio-format, spec-audio-runtime, spec-audio-codebook-library, spec-tables-audio-radio.

**Specced:** ~80% — the format/container layer, the static Vorbis codebook library (100% complete as a
data appendix, 599 codebooks hashed/verified), and all 16 audio/radio/foley `.xtbl` schemas (explicitly
"none remain unassigned") are essentially done. spec-audio-runtime carries the bulk of remaining depth
risk with 51 numbered open items, mostly deep Wwise-internal plumbing.

**Key open items:**
- What actually sets the bank load/unload state-machine's trigger flags (`+0x54`/`+0x5b`) — searched
  exhaustively, named breakpoints given. **(a) confirmed dead end, needs live capture** — shared
  verbatim between spec-audio-format §8 and spec-tables-audio-radio.
- The real Vorbis decode-trigger vtable slot (does GetBuffer actually produce PCM here?) is only
  narrowed by elimination, not independently proven. **(b) bordering (a).**
- 15 of 16 non-fallback HIRC sound-object kinds have unopened allocate/apply property-parsing pairs.
  **(b) not yet investigated.**
- `audio_line_tags.xtbl`: 97% of authored rows (4,507/4,626) never reach the runtime array — the "why"
  is a design question code can't answer. **(c) blocked on a decision/unanswerable from code alone.**

---

## 12. Video (Bink)
**Files:** spec-bink-boundary, spec-bink-codec, spec-bink-static-tables, spec-bink-video-test-vectors.

**Specced:** ~70–75%. The underlying Bink codec algorithm (entropy coding, DCT, quantization) is
essentially solved at the structural level — expected, since Bink is a well-known third-party format and
FFmpeg served as a black-box oracle for corroboration — but the project has **not** declared bit-exact
decode done. The static-table data appendix is 100% complete for its scope. SR3-specific integration
(the boundary layer) is ~80% for the low-level player contract, weaker (~40%) for higher-level trigger
wiring.

**Key open items:**
- **DC +128 level bug** — a uniform level-128 error in DC values during bit-exact validation. Two
  independent fix hypotheses were tried and **empirically refuted** against live Unicorn-emulation
  captures. **(a) confirmed dead end for static analysis — needs a different live-capture/comparison
  strategy**, not just more disassembly.
- **ChromaB never decodes** in the validation harness (pure sentinel fill, 0% real output) — the root
  poison-path instruction range was identified, but the fix hasn't been applied yet. **(c) blocked on a
  decision to attempt the fix**, not unknown.
- The cutscene-xtbl-to-Bink-module call chain (which code actually triggers playback) is narrowed to 4
  remaining state-machine handlers. **(b) not yet investigated.**
- The GPU presentation/upload step for the decoded Y/U/V buffer was never located at all. **(b) not yet
  investigated** — has a concrete starting address to resume from.

---

## 13. Save Format
**File:** spec-save-format.

**Specced:** ~80–85% (71 reviewed units: 28 DESK-PASS, 11 with fixes, 20 validated against real save
data, 9 NEEDS-DATA, 3 NEEDS-EXE).

**Key open items:** the weapon-inventory live-bit offset ambiguity (`+0x2C` vs `+0x30`); indirect
references feeding the save hash routine and an "error-5" producer; several real-save-sample field
tallies that don't sum correctly across multiple sections. Mostly **(b) not yet investigated** / **(c)
blocked on more real save-file samples** — not static dead ends.

---

## 14. Progression (Respect / Cash / Unlocks)
**File:** spec-tables-progression.

**Specced:** ~80% — a 2026-10-01 re-derivation pass cleared 12 of 18 prior NEEDS-EXE units.

**Key open items:** one reader address (§10.12) that's absent from any available disassembly dump
(**(b)**); several units re-derived "in part" but not fully cleared; a real per-award respect cap was
recently resolved for Team B (238,875, a lifetime-total ceiling from `respect_levels.xtbl`), closing one
previously confusing gap. Remaining items are data-census tasks, **(c)**, not reverse-engineering
unknowns.

---

## 15. Activities / Diversions
**Files:** spec-activity-runtime, spec-tables-diversions.

**Specced:** a split profile. The C++ runtime class framework (spec-activity-runtime) is only
**~30–35% specced** — one activity type (Barnstorming) is traced end-to-end as a worked example;
generalization to the other 8 stunt-type tables is high-confidence but unverified individually, and 5
types (cat-and-mouse, mugging, photo-op, streaking, exploration) haven't even been checked for address
clustering. The data-table side (spec-tables-diversions) is much further along, **~85–90%**, after a
2026-10-02 re-derivation pass cleared all 16 prior NEEDS-EXE units.

**Key open items:**
- **How an `_a_*` placement marker selects and constructs the correct activity instance** — this is the
  single biggest open question for this system project-wide. **(b) not yet investigated** — one
  hypothesis was ruled out this cycle, narrowing toward a batch/session-end-grant mechanism, but the
  actual construction/dispatch site is still unfound.
- The actual cash/respect reward-grant consumer — narrowed (direct `Max_Respect`/`Max_Cash` readers
  found for `stunt_jumping_diversion`, same inert pattern as Barnstorming) but not fully closed. **(b).**
- A large, mostly-unmapped function (`0x006f54e9`) referenced from the diversion tables. **(b) not yet
  investigated.**

---

## 16. Cutscenes / Cinematics
**File:** spec-cutscene-camera-format.

**Specced:** ~65% (15 reviewed units; core channel layout — position, quaternion orientation, FOV,
depth-of-field — now confirmed by disassembly; 192/192 real entries validated by Team B).

**Key open items:** which of 4 DOF channels maps to which focal-parameter shader component (**(b)**);
a data contradiction between the tail-gap histogram and its own median/p90 figures that needs a re-run
against real data (**(c) blocked on data re-run**); the location of 96 duplicate entries within the
archives (**(b)**).

---

## 17. Customization
**Files:** spec-customization-data, spec-tables-customization.

**Specced:** ~75–85% — two re-derivation passes in early October cleared most prior NEEDS-EXE units
(customization-data's one remaining NEEDS-EXE unit and 2 open questions are now confirmed;
tables-customization cleared 18 of 27).

**Key open items:**
- A persistent 47-vs-56 DLC row-count discrepancy, present in both files, still needs a real-data
  census. **(c) blocked on data.**
- The per-vehicle `_cust.xtbl` was never found as a loose file — redirected to the `.cvtf_pc` binary
  format instead, high-confidence but never byte-level confirmed. **(b) not yet investigated** — a
  real cross-format gap.
- 9 table-reader addresses in tables-customization fall outside the executable dump that was available
  this pass. **(b) not yet investigated.**

---

## 18. VPP / Container Formats & Misc Infrastructure
**Files:** spec-vpp-container, spec-xtbl-format, spec-format-inventory, spec-asm-format,
spec-ctorless-types, spec-extensionless-types, spec-runtime-data-sources, spec-output,
spec-steamworks-boundary.

**Specced:** ~70–75% in aggregate — methodologically this is one of the more mature groups (two
candidate hypotheses were actively tested and refuted rather than left ambiguous: a VPP "cache-eviction
sub-field" theory for the compression-mode switch, and an xtbl "missing decoder state" theory). The
container format itself (spec-vpp-container) is empirically validated against 6,431 real containers /
405,694 entries. format-inventory and the two small types files (ctorless/extensionless) are ~90%+,
essentially fully re-derived.

**Key open items:**
- VPP header field `0x164`'s owner/meaning is still unresolved, with a conflict between on-disk-header
  and runtime-struct readings across multiple sections. **(b) not yet investigated.**
- Cross-document container-kind count mismatches (spec-asm-format vs. spec-world-streaming disagree on
  counts over the same 805 manifests). **(c) blocked on reconciling two docs.**
- spec-steamworks-boundary has 14 numbered open items (mostly unexamined helper/dispatch functions,
  **(b)**), plus one cross-spec disagreement with spec-player-runtime over a button "pressed vs. held"
  semantic that needs a decision to reconcile (**(c)**).
- **spec-runtime-data-sources.md and spec-output.md are notably less rigorous than the rest of this
  group** — output.md explicitly pre-dates the project's confidence-labeling convention ("nothing in it
  should be read at better than HIGH CONFIDENCE — inferred") and runtime-data-sources is a thin,
  single-purpose reference list (exact file/offset/hash for externally-sourced data like the Wwise
  codebook library) rather than a reviewed spec. Not urgent, but flagged honestly rather than counted at
  face value in the aggregate above.

---

## Cross-system flagship dead ends (for quick reference)

These are the handful of items this project has already run to a genuine, exhausted static-analysis
wall, explicitly requiring a live capture (Frida hook, breakpoint, or frame capture) to close — as
opposed to the much larger population of ordinary "not yet investigated" gaps above:

1. **Melee hit→damage hand-off** (Player Runtime / Combat) — 4 independent passes, full statically-
   reachable call graph exhausted, no edge into the damage sink found.
2. **Hitscan fire→damage call site** (Combat/Weapons, §4.4) — 3 named candidates ruled out, ~70–86
   unattributed callers of the damage sink remain unopened.
3. **Projectile fuse/impact detonation consumer** (Combat/Weapons) — homing resolved, but what consumes
   the fuse timer to apply damage is still unlocated; one lead eliminated.
4. **vtable `+0x68` predicate** (Missions/Lua, cited around dlc2_m01) — independently re-verified: no
   constant-stub shortcut exists, a confirmed impossibility for static analysis.
5. **DC +128 Bink decode bug** — two fix hypotheses empirically refuted against live emulation captures.
6. **Bank load/unload trigger flags** (Audio) — exhaustive search, named breakpoints given for a future
   live pass.
7. **Render-pipeline per-frame driver** and **tree-vertex POSITION CPU consumer** (Rendering) — each
   run to a dead end twice independently.
8. **Zone/terrain texturing mechanism** and the **ground-missing heightmap question** (World
   Streaming) — both file-side dead ends with a live-capture precedent already proven effective
   elsewhere in this project (the navmesh-registry case, where a Frida capture reversed a "dead by
   static analysis" conclusion).

These are the best-leveraged targets if/when a live-capture session becomes available, since static
analysis has already been pushed to its limit on each.

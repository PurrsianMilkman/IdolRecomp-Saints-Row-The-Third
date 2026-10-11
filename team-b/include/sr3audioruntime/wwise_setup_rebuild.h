#pragma once

// sr3audioruntime - the Wwise-stripped setup-packet -> standard Vorbis I
// setup-header bitstream transcoder (spec-audio-runtime.md Sec11.11.3/
// Sec11.11.3a/Sec11.11.4, unit U-W4c). real_vorbis_decode.cpp already proves
// (via tests/real_vorbis_decode_test.cpp, a real-data regression) that the
// UNMODIFIED stripped setup packet, merely prefixed with the standard
// 7-byte packet-type+signature header, is rejected by libvorbis's
// `vorbis_synthesis_headerin` with OV_EBADHEADER every time - i.e. the
// stripped bitstream really is shaped differently from a standard Vorbis I
// setup header, not just differently framed. This file does the actual
// bit-level transcode Sec11.11.3a's own text calls for.
//
// UPDATED 2026-10-08 (codebook-library bring-up pass): Sec11.11.3a's MAJOR
// CORRECTION closes out this file's previous "honest current state" OPEN
// item. The main setup-packet bitstream does NOT read each codebook's
// fields inline at all. For each codebook slot, the main packet's shared
// bitstream cursor holds only a bare 10-bit INDEX - nothing else at that
// position. That index selects one entry of a real, static, compiled-in
// library of exactly 599 real codebook bodies (spec-audio-codebook-
// library.md; see include/sr3audioruntime/wwise_codebook_library.h for the
// compiled-in copy). The resolved entry's bytes become the data source of a
// brand-new, INDEPENDENT bitstream cursor, local to just that one codebook;
// only THAT isolated cursor is where dimension/entries/ordered/length-
// field-width/maptype/lookup-table are read, using the exact field grammar
// already described below - never from the main packet's own shared
// cursor. Control returns to the main packet's shared cursor only to read
// the NEXT codebook's own 10-bit index.
//
// This fully explains, as ARTIFACTS rather than facts, the two "genuine
// empirical findings" an earlier bring-up pass reported here (now REMOVED,
// not preserved - see wwise_setup_rebuild.cpp's transcodeOneCodebook() for
// the field grammar as actually implemented):
//   - "dim==0 means a trivial/empty slot to skip" - an artifact of reading
//     the NEXT codebook's own 10-bit index (a small value, often with a
//     low/zero top nibble) as if it were the CURRENT codebook's dimension
//     field, because the previous pass did not yet know the main packet
//     only carries a short index per slot, not a full inline codebook body.
//   - "maptype is 2 bits, not 4" - the same misreading one level further in:
//     a 4-bit read at the (wrong) inline position picked up 2 real bits
//     belonging to whatever followed (the next codebook's index, inline),
//     which is NOT the real codebook's own maptype field at all. SUPERSEDED
//     AGAIN (2026-10-08, Sec11.11.3a's later same-day UPDATE): once
//     codebooks are each parsed from their own isolated static buffer, this
//     file's own text used to say maptype "reads cleanly as the standard
//     4-bit field" - that was ALSO wrong, just a different wrong width than
//     the 2-bit misreading it replaced. The real field is a single 1-bit
//     "has VQ lookup table" flag (0/1), not 2 and not 4 - see
//     wwise_setup_rebuild.cpp's transcodeOneCodebook() for the current,
//     libvorbis-validated reading.
//
// SOURCES FOR EVERY FIELD WIDTH/ORDER CLAIM BELOW:
//   - spec-audio-runtime.md Sec11.11.3/Sec11.11.3a (CONFIRMED structure,
//     2026-10-08, including the MAJOR CORRECTION) for what is DIFFERENT
//     between the stripped and standard bitstreams (narrower codebook
//     dimension/entries fields, the new 3-bit "length field width",
//     floor_type/residue_type/mapping_type/mode windowtype+transformtype
//     never read at all, the dropped vestigial time-domain-transform
//     section, channel count supplied externally, and the per-codebook
//     10-bit-index-into-a-static-library indirection).
//   - spec-audio-codebook-library.md for the real 599-codebook library data
//     itself (index table + raw blob + SHA-256), compiled in via
//     include/sr3audioruntime/wwise_codebook_library.h.
//   - The public Vorbis I specification (vorbis.com/doc/Vorbis_I_spec.html)
//     for every field this project does NOT claim is Wwise-proprietary:
//     the floor1/residue0/mapping0 backend internals, and the universal
//     "N, 6-or-8-bit-prefixed, always +1" section-count convention already
//     confirmed to match standard Vorbis I exactly.
//   - This project's own vendored, BSD-licensed third_party/libvorbis/lib/
//     {info.c,codebook.c,sharedbook.c,floor1.c,res0.c,mapping0.c} - read
//     directly for the EXACT bit widths/order real `_vorbis_unpack_books`
//     uses, the same "legitimate use of code this project vendored for
//     exactly this purpose" footing real_vorbis_decode.cpp's own header
//     comment already claims for info.c.
//   - This project's OWN direct empirical measurement against real shipped
//     setup packets (cutscene_sounds.vpp_pc) - real data + real libvorbis
//     as the independent validator, not disassembly (Team B never
//     disassembles).
//
// WHAT IS STILL GENUINELY OPEN:
//   - RESOLVED (2026-10-08, the dense-codebook-grammar fix task): the
//     former "MAIN RESIDUAL" noted here - the dense/unordered codebook's
//     per-entry codeword-length list not closing its own bit budget - is
//     now CLOSED. Root cause was a missing mandatory 1-bit sparse/dense
//     selector immediately after the length-field-width field (every
//     prior hypothesis tried against the real library data lacked this
//     bit entirely), PLUS a second, separately-pinned correction: the
//     per-entry length field's own bit count is `widthRaw` directly
//     (0-7), NOT `widthRaw+1` - the ordered branch's seed-length read is
//     the only place the "+1" bias actually applies. Both corrections are
//     now bit-exact verified against ALL 599 real library codebooks
//     (tests/wwise_codebook_library_crosscheck_test.cpp: 599/599 full-body
//     match, up from 6/599 before this fix - zero parse failures, zero
//     field mismatches) and against real libvorbis acceptance of a
//     minimal 3-codebook setup header (spec-audio-runtime.md Sec11.11.3a's
//     2026-10-08 UPDATE). See wwise_setup_rebuild.cpp's transcodeOneCodebook()
//     for the full implementation and the per-field sourcing.
//   - NEW RESIDUAL (surfaced only once the above was fixed - previously
//     masked because every real record failed on an early codebook's BODY
//     before ever reaching this): on the real 493-packet sweep against
//     cutscene_sounds.vpp_pc, 5 records now reach libvorbis itself
//     (OV_EBADHEADER - still rejected, but a genuinely new high-water
//     mark; previously 0 records ever got that far), but 488/493 still
//     fail INSIDE this project's own transcode - now at a COMPLETELY
//     DIFFERENT point than before: not inside transcodeOneCodebook() at
//     all (that's proven airtight by the 599/599 cross-check above), but
//     in rebuildVorbisSetupHeader()'s own OUTER per-codebook loop, which
//     reads a bare 10-bit index straight off the main packet's shared
//     cursor for each of a record's `books` codebooks. For most records,
//     one of these indices (commonly the 3rd-5th codebook in the list;
//     488-record histogram: position 2 alone accounts for 322/488) comes
//     out >= 599 - i.e. inside the range spec-audio-codebook-library.md's
//     own Provenance section calls "confirmed spurious... reading off the
//     end of the real table into an unrelated .data float array." Hand
//     bit-traced directly against one real record's own raw setup-packet
//     bytes (ct_angel_01_media.bnk_pc, books=187: codebook list bytes
//     ba 0a 00 42 a5 09 80 42 ...) using the now-fully-validated field
//     grammar - the 5th codebook's (position 4) 10-bit index reproduces
//     640 bit-for-bit, matching this project's own computed value exactly,
//     which rules out a Team-B bitstream-reading bug at that position
//     (codebook BODIES 0-3 all parsed as valid small indices into
//     completely separate, isolated per-codebook buffers that cannot
//     possibly feed back into the main packet's own cursor position - the
//     outer 10-bit reads are the ONLY thing determining this value, and
//     they reproduce exactly by hand). The dominant failure value across
//     the whole sweep (772, 204/488 occurrences) and the overall
//     distribution (every failing value in [600,1023], clustering into
//     short ascending runs) both look exactly like spec's own described
//     "garbage past the real table" shape. The real puzzle this surfaces:
//     if that characterization is complete and correct, the SHIPPED game
//     would dereference the identical garbage for these identical shipped
//     files - implausible for working, presumably-audible cutscene voice
//     lines. Two live possibilities, neither of which Team B can settle
//     without disassembly: (a) the real compiled table's genuinely-valid
//     range extends further than the 599 entries currently transcribed in
//     spec-audio-codebook-library.md (the "600-1023 is spurious" check may
//     have sampled too few slots to catch a later real/valid stretch), or
//     (b) there is some other per-codebook-slot wrinkle in the MAIN
//     packet's own list encoding not yet captured. RELAYED to Team A as a
//     spec-sync question, not attempted further here, per this project's
//     own "no invented fixes / Team B never disassembles" discipline.
//
//   **UPDATE 2026-10-08 (out-of-range-codebook-index bring-up task,
//   `wwise_setup_budget_probe` tool): the NEW RESIDUAL above is now known
//   to be a SYMPTOM, not the root cause - option (b) above, not (a).**
//   This pass does NOT fix the bug (still genuinely open - see below), but
//   narrows it sharply with real-data evidence spanning the WHOLE 493-
//   record population, not one hand-traced example. Method: added
//   `probeSetupPacketBitBudget()` (this header, below) - walks a real
//   setup packet to its own end WITHOUT aborting on an out-of-range
//   codebook index (safe, because a codebook's body never touches the
//   main cursor regardless of index validity - see this file's own
//   top-of-file note). Finding: even when every codebook index is
//   in-range (the 5/493 "successful" records), the resulting
//   floors/residues/mappings/modes counts are implausible noise (e.g.
//   floors=59, modes=64, maps=62 for records that should need a handful
//   of each) - meaning `rebuildVorbisSetupHeader()`'s reads were NEVER
//   landing on real Vorbis setup-header content for ANY of the 493 real
//   records, not just the 488 that visibly fail. Tracing further: the
//   REAL WAV "data" sub-chunk (where `wwise_vorbis_setup.h`'s
//   `walkFramedPackets()` currently assumes Sec11.11.1's bare "2-byte
//   length prefix, repeat" framing begins immediately) in fact starts with
//   a clearly-structured, NON-bitstream-like region: successive 4-byte
//   groups whose second half, read as a little-endian float32, lands
//   within 1% of a round number (e.g. 32.01, 64.02, 4.00, 2.00, 8.00) for
//   HUNDREDS of consecutive bytes, then sharply (not gradually) transitions
//   to high-entropy noise - the opposite of what reading real compressed
//   Vorbis bits would ever produce. Reading that same region as a
//   sequence of 4-byte `(sampleCount: u16 LE, byteSize: u16 LE)` entries
//   and summing every entry's FIRST field up to where the "clean" pattern
//   ends reproduces the `vorb` chunk's own INDEPENDENT total-sample-count
//   field (Sec11.11.2/11.11.5's `sampleCountFallbackField`) to within
//   0.2%-7% for **all 493/493 real records** (population-wide check, not
//   a couple of examples: mean ratio 0.985, every single record's ratio
//   in [0.93, 0.998) - the shortfall is consistent with this probe's own
//   conservative "1%-of-round-number" cutoff clipping the table's last
//   few, less suspiciously-round entries, not with the match being
//   coincidental). **This is strong, population-validated, real-data
//   evidence that Sec11.11.1's "no lacing table" claim does not hold for
//   the real shipped format** - there IS a leading, per-audio-packet
//   seek/size table (roughly 1 four-byte entry per real audio packet,
//   `sampleCount` first/`byteSize` second) before the packet payload
//   pool, which `walkFramedPackets()` has no knowledge of at all; its
//   "packets[0]" is actually slicing into the MIDDLE of this table, not a
//   real setup packet, which is exactly why the main cursor's own
//   subsequent reads (codebook count, 10-bit indices, floor/residue/
//   mapping/mode counts) come out as noise for literally every record.
//   **Genuinely still OPEN - not solved this pass, per "no invented
//   fixes": where the real stripped Vorbis setup packet actually lives
//   relative to this newly-found table was NOT pinned down.** Two
//   concrete placements were tried and both empirically REJECTED (neither
//   invented nor assumed - tested directly against real bytes): (1)
//   immediately after the table's last entry - the following
//   bytes decode as `books=1` and absurd floor/residue/mapping/mode
//   counts, same noise signature as before; (2) a trailing region at the
//   very end of "data" sized `(bytes left after the table) - (sum of the
//   table's own byteSize fields)` - its leading bytes DO look like
//   genuine high-entropy data (unlike (1)), but the computed region's own
//   size is wildly inconsistent across records (2939/1468 bytes for two
//   channel=1 records vs. 15991/17131 bytes for two channel=6 records,
//   with no consistent scaling) and its parse still produces an
//   implausible `books` count (209, 117, 80, 30) and dozens of
//   out-of-range codebook indices, or an outright EOF - not a clean
//   result either way. RELAYED to Team A as a spec-sync question
//   (Sec11.11.1's packet-framing contract itself, not the codebook
//   library): what is the real byte layout of a per-packet seek/size
//   table entry, how many entries does it really have, and where exactly
//   does the actual Vorbis setup packet sit relative to it - genuinely
//   unanswerable from real-data measurement alone without crossing into
//   disassembly, which Team B does not do. The population-wide validation
//   tool (`tools/wwise_setup_budget_probe.cpp`, diagnostic-only, not
//   wired into the real decode path) is kept in the tree for whoever
//   picks this up next; its own top comment explains the method.
//   **UPDATE 2026-10-08 (vorb-chunk explicit-offset-field hypothesis task):
//   the NEW RESIDUAL's own leading-table placement question (immediately
//   above) is STILL genuinely open, but one specific candidate answer -
//   spec-audio-runtime.md Sec11.11.2's own table row calling vorb bytes
//   [4:8)/[8:12) "consistent with setup-packet-relative vs. first-audio-
//   packet-offset" - is now REJECTED, population-wide (493/493), in three
//   distinct placements tried (literal [fieldA,fieldB); a boundary-
//   corrected [fieldA, dataChunkSize-fieldB); and that corrected boundary
//   combined with Sec11.11.1's own CONFIRMED 16-bit length-prefix framing).
//   All three fail probeSetupPacketBitBudget()'s own plausibility bar,
//   0/493 each - see results/probe_vorb_explicit_offset_fields_20261008.md
//   for the full numbers. A genuine, NEW, EXACT (zero-variance, not a fuzzy
//   percentage) fact was found as a byproduct: (dataChunkSize - vorb[8:12))
//   - cleanRunEnd == exactly 4 for ALL 493 real records - independently
//   confirming, via the vorb chunk's own field rather than a byte-pattern
//   heuristic, that the leading structured region's real end sits exactly
//   4 bytes past the prior pass's own heuristic estimate. Real forward
//   progress on the container's own boundary - still does not pin down
//   where inside that now-exact leading region the setup packet's own
//   bytes begin. Public-format corroboration was also attempted (ww2ogg/
//   vgmstream's generic public Wwise-Vorbis container format, per this
//   task's explicit authorization): xoreos's own doxygen-documented C++
//   reimplementation of ww2ogg (Sound::WwRIFFVorbisStream) independently
//   confirms a `_setupPacketOffset`/`_firstAudioPacketOffset` field PAIR
//   really exists, by name, in the real public format - genuine
//   corroboration that Sec11.11.2's guessed field PAIRING is not invented -
//   but that doxygen page gives field names/types only, no byte-level
//   layout, and no standalone prose format-notes document describing the
//   real byte offsets could be found without reading the converters' own
//   SOURCE (ww2ogg's wwriff.cpp/.h, vgmstream's Wwise meta parser), which
//   this task's own hard rule explicitly fenced off ("fetch descriptive
//   documentation... not source files"). No source was read; no byte
//   offset was guessed from memory and mislabeled as documented. RELAYED to
//   Team A as an addition to the existing Sec11.11.1 spec-sync question,
//   not attempted further here, per "no invented fixes."
//
//   - Whether mapping0's per-submap "time submap" placeholder byte is still
//     physically present in the stripped bitstream or was dropped - this
//     project's rebuild assumes ABSENT, consistent with the "drop every
//     vestigial field" pattern Sec11.11.3a documents elsewhere, but NOT
//     independently validated (real population reach into the mapping
//     section still depends on the NEW RESIDUAL above - only 5/493 real
//     records have reached that far so far).
//   - residue_type's hardcoded value: real libvorbis encoder output uses
//     type 2 for a coupled (channel-count > 1) submap and type 1 for an
//     uncoupled one; this project hardcodes by channel count, NOT
//     independently validated for the same reason.
//   - The exact semantic of the 3-bit "length field width" field: raw value
//     0-7 is used DIRECTLY as the per-entry length field's own bit count in
//     the ordered==0 branch (NOT +1-biased - only the resulting per-entry
//     length VALUE gets +1), vs. raw+1 for the ordered branch's own seed
//     length. Both are now bit-exact confirmed (599/599 real codebooks,
//     zero mismatches) - no longer open for the field WIDTH semantics
//     themselves; what remains open is only the separate NEW RESIDUAL
//     above (the main packet's own per-codebook 10-bit index list).
//
//   **UPDATE 2026-10-08 (vorb-chunk seek-table-relative-offset confirmation
//   task): the NEW RESIDUAL's own leading-table placement question (the
//   out-of-range-codebook-index UPDATE above) is STILL genuinely open, but
//   spec-audio-runtime.md Sec11.11.5 landed a disassembly-CONFIRMED formula
//   this pass re-tested population-wide before accepting it - commit
//   64f705a on `main` ("Wwise vorb: setup/first-audio offsets = vorb field
//   + seek-table length (vorb[16:20]) + data start"), verified directly
//   against the real commit object (not trusted from the relay alone, per
//   standing practice - this caught one small paraphrase slip: the rare
//   selector==1 fallback's term is `this+0xe0`, a live decode-state field,
//   not "vorb[0xe0]"), then cherry-picked onto this branch as c1f586e.**
//   `setupPacketStart = dataChunkOffset + vorb[4:8) + vorb[16:20)` and
//   `firstAudioPacketStart = dataChunkOffset + vorb[8:12) + vorb[16:20)`
//   (the common/default path, which spec's own text says every real
//   standalone WAV here hits) were tested against all 493 real records
//   with `tools/wwise_vorb_seektable_offset_probe.cpp` - **REJECTED,
//   0/493 plausible**, under the literal formula and two motivated
//   alternate readings (drop the vorb[4:8) term; subtract it instead of
//   adding). One EXACT, zero-variance, 493/493 falsification:
//   `firstAudioPacketStart` computes to EXACTLY the end of the "data"
//   sub-chunk for every real record (dataEnd - firstAudioPacketStart ==
//   0, min=max=mean=0) - zero bytes left for any audio packet, in a
//   corpus where every record demonstrably holds hundreds of KB of real
//   compressed audio; traces algebraically to vorb[8:12) + vorb[16:20) ==
//   dataChunkSize holding exactly for every unlooped record. A
//   +-64/+256-byte local-neighbourhood scan around the computed
//   setupPacketStart found no nearby fix either - not a rounding error.
//   The one sub-claim corroborated three ways (this project's own direct
//   measurement: vorb chunk size is a constant 42/0x2a across all 493
//   records; Team A's disassembly; and - per this round's now-superseded,
//   explicitly-authorized public-source reading, stopped once the
//   orchestrator's redirect to the real disassembly answer landed - both
//   `ww2ogg`'s `wwriff.cpp` and `vgmstream`'s `wwise.c`, the latter naming
//   this exact game in its own `case 0x2a:` vorb-size-variant switch):
//   vorb[16:20) really is the leading seek table's own exact byte length.
//   Full numbers and worked examples: results/probe_vorb_seektable_
//   relative_offsets_20261008.md. RELAYED to Team A as a spec-sync
//   question (what this+0x48/this+0x4c are actually consumed FOR by the
//   Seek-shaped virtual call once computed - not a disassembly correction,
//   which Team B does not do), not attempted further here per "no invented
//   fixes." Production code (this file, real_vorbis_decode.cpp) is NOT
//   changed by this pass - the rejected formula is not wired into the real
//   decode path.
//
//   **UPDATE 2026-10-08 (vorb-chunk setup/audio-offset-pair re-hypothesis
//   task): FIRST REAL DECODES SUCCEEDED, 5/493 - the REJECTED formula above
//   was a correct NUMBER under the WRONG NAME.** Re-tested this file's own
//   independently-derived new hypothesis (`setupPacketStart = dataChunkStart
//   + vorb[16:20)` alone, `firstAudioPacketStart = dataChunkStart +
//   vorb[20:24)`) against all 493 real records - CONFIRMED, population-wide
//   (INVARIANT_EXACT: the setup packet's own declared length reaches
//   firstAudioPacketStart EXACTLY, 493/493; audio region positive, 493/493).
//   Mid-task, the orchestrator relayed Team A's own direct-disassembly
//   correction (commit 8871fb6 on `main`, spec-audio-runtime.md Sec11.11.5,
//   `0x00fa4250`/`0x00fa36b0` read directly - cherry-picked onto this branch
//   spec-text-only): `this+0x48`/`vorb[4:8)` was never a setup-offset term -
//   it IS (summed with vorb[16:20)) the first-audio-packet offset, the
//   IDENTICAL formula under correct naming. Identity cross-check
//   (vorb[20:24) == vorb[4:8) + vorb[16:20)) holds 493/493 on this project's
//   own corpus, matching Team A's own stated 981/981. [CONFIRMED] now, not
//   HYPOTHESIS - Team A's own disassembly answer, not a guess.
//
//   TWO FURTHER bitstream-grammar fixes, labelled `agrees-with-public-
//   format` (not [CONFIRMED] - Team B never disassembles): found by hand
//   bit-tracing one real record once the offset was fixed
//   (tools/wwise_setup_packet_bit_trace_probe.cpp), corroborated against
//   vgmstream's own public source (src/coding/vorbis_custom_utils_wwise.c,
//   read via WebFetch for format understanding only, never transcribed):
//   (1) `residue_type` IS read as a bare 2-bit field immediately before
//   begin/end/grouping - this project previously hardcoded it by channel
//   count and never read it, a 2-bit drift that corrupted every residue
//   section's own fields (groupbook computed as 80 against only 32 real
//   codebooks); (2) the per-submap "time submap" placeholder byte (8 bits)
//   IS present on the wire and must be read-and-discarded (vgmstream's own
//   gloss: "passed through as an unused placeholder") - this project
//   previously assumed it ABSENT, an 8-bit-per-submap drift that zeroed
//   every floorsub/residuesub and put one mode's mapping index out of
//   range. Both fixed in transcodeOneResidue()/transcodeOneMapping() below.
//   Also confirmed via vgmstream's own source: the existing bare-2-byte-
//   length-prefix packet-header-shape assumption was ALWAYS correct for
//   this exact version (WWVORBIS_V53 -> WWV_TYPE_2, no granule field) - the
//   prior pass's own 19-24-byte short reads were a wrong OFFSET, never a
//   wrong HEADER SHAPE.
//
//   With all three fixes, probeSetupPacketBitBudget()'s full plausibility
//   gate passes 493/493 (up from 0/493), and real_vorbis_decode_test.cpp's
//   real sweep gets vorbis_synthesis_headerin to ACCEPT all 3 header
//   packets for 493/493 records (0 OV_EBADHEADER) and 5/493 to decode all
//   the way to real, non-silent 16-bit PCM - saved as a playable .wav,
//   tests/golden/wwise_vorbis_decode/ct_kinzie_03_media_first_success.wav.
//   STILL genuinely OPEN (488/493, isolated precisely, not guessed): every
//   failure's own rebuilt.codebookCount is exactly 32 (442 records) or 34
//   (46 records) - 442+46=488, an exact match; the 5 successes are exactly
//   codebookCount 35 (4) and 42 (1) - 4+1=5, an exact match. Points at
//   vorbis_book_init_decode's own Kraft-inequality check
//   (third_party/libvorbis/lib/sharedbook.c's _make_words()) failing for at
//   least one specific codebook-library slot present in the 32/34-book
//   selections but absent from 35/42 - a codebook-LIBRARY-DATA question
//   (spec-audio-codebook-library.md), not an offset or framing question,
//   not attempted further this pass per "no invented fixes." Full detail:
//   results/probe_vorb_setup_audio_dword_20261008.md. Production code
//   CHANGED this pass: this file's transcodeOneResidue()/
//   transcodeOneMapping(), and real_vorbis_decode.cpp's
//   attemptWwiseVorbisDecode() (setupPacketStart/firstAudioPacketStart now
//   computed from vorb[16:20)/vorb[20:24) instead of walking from
//   dataChunkOffset with no skip) - the first production-path change this
//   branch has made since it started testing offset hypotheses.
//
//   UPDATE 2026-10-09 (independent-oracle verification task): the "5/493
//   FIRST REAL DECODES SUCCEEDED" claim directly above is REFUTED, not
//   confirmed. Ran the independent, off-the-shelf `vgmstream-cli` (official
//   prebuilt binary, run as a pure black box, never read as source) against
//   the SAME raw record bytes for all 5 claimed successes and compared
//   PCM per-sample: every one shows |correlation| < 0.02 against the real
//   audio at EVERY lag searched (full-length FFT cross-correlation, not
//   just zero lag) - statistically indistinguishable from noise, and
//   durations are wildly short versus the oracle (e.g. zscene_01_03_media:
//   0.34s decoded vs the real 21.65s). NONE of the 5 is a correct decode;
//   "first real decoded game audio" is withdrawn. Root cause, bisected
//   packet-by-packet (tools/wwise_vorbis_packet_divergence_probe.cpp): the
//   packet-framing walk IS self-consistent to the exact end of "data" for
//   every record (the offset fix above is NOT what this refutes), and
//   packet index 1 (first audio packet) always correctly produces 0
//   samples (expected lapped-MDCT priming) - but packet index 2 (first
//   REAL-CONTENT packet) fails `vorbis_synthesis()` with exactly
//   OV_ENOTAUDIO (-135) every time, in every record examined, starting a
//   long contiguous run of failures (not scattered noise); per-record
//   `vorbis_synthesis()` success rates range 1.7%-49.4% of attempted audio
//   packets. Separately, independently sufficient to explain the garbled
//   output on its own: real_vorbis_decode.cpp's own success gate
//   (`decodedOk == 0`) treats ANY nonzero decodedOk as `ok = true` and
//   splices the surviving, non-contiguous packets' PCM together with no
//   gap-fill or decoder reset, breaking the lapped-MDCT overlap-add for
//   every subsequent packet regardless of whether its own math was right.
//   A NEW, more precisely located residual, downstream of and different
//   from the 488/493 codebook-count/Kraft-inequality issue (these 5 are
//   not among that 488) - not yet root-caused to a specific field, not
//   guess-fixed further per "no invented fixes." No production code
//   changed this pass. Full detail:
//   results/verify_vorbis_oracle_comparison_20261008.md.
//
//   UPDATE 2026-10-09 (488/493 codebook-count investigation task): the
//   488/493 Kraft-inequality residual directly above is now root-caused to
//   an EXACT library slot, not just "codebookCount 32 or 34" - diagnostic
//   tool `tools/wwise_vorbis_codebook_kraft_probe.cpp` (new).
//
//   (1) Standalone library sweep: ran the REAL, vendored, unmodified
//   `vorbis_book_init_decode()` (third_party/libvorbis/lib/sharedbook.c,
//   linked, not reimplemented) against each of the 599 real library slots
//   individually (no record/setup-header context at all - this is a pure
//   function of one codebook's own dim/entries/lengths, so a per-slot
//   result applies unconditionally to every record that ever selects that
//   slot). RESULT: exactly 7/599 slots fail - 16, 160, 161, 183, 200, 317,
//   598 - every other slot (592/599) passes standalone.
//
//   (2) Population cross-reference: re-ran the per-codebook 10-bit-index
//   list against EVERY ONE of the real 488 failing records (not a sample -
//   all 442 codebookCount=32 and all 46 codebookCount=34 records from
//   cutscene_sounds.vpp_pc) and every one of the 5 passing records.
//   RESULT, zero exceptions either way: all 488 failing records select
//   library slot 160 at codebook list position 1 (the second codebook),
//   and FAIL vorbis_book_init_decode there (confirmed by reaching into the
//   record's own already-libvorbis-unpacked `vorbis_info::codec_setup` and
//   calling vorbis_book_init_decode() per position directly - the REAL
//   parsed static_codebook for that position, not this project's own
//   transcode struct); no other position in any of these 488 records ever
//   fails. None of the 5 passing records (4 at codebookCount=35, 1 at
//   codebookCount=42) ever select slot 160, or any of the other 6
//   standalone-failing slots. In this corpus, slot 160 alone accounts for
//   100% of the 488/493 split.
//
//   (3) Slot 160 hand bit-traced to certainty: its real library body is 4
//   bytes (`41 00 0c 04`), byteLen matching spec-audio-codebook-library.md's
//   own table exactly. Every field - dim(4b)=1, entries(14b)=4, ordered(1b)
//   =1, length-field-width(3b)=1(+1=2), seed-length(2b)=0(+1=1), run-count
//   (ilog(4)=3b)=4 - reads unambiguously, closing with exactly 4 leftover
//   bits (ordinary byte-alignment slack, same bar every other pass on this
//   branch uses) - i.e. this project's OWN bitstream-reading grammar for
//   this slot is airtight, not in question. The resolved lengths are
//   `[1,1,1,1]`: four length-1 codewords is mathematically impossible (only
//   2 distinct 1-bit codewords exist) - Kraft sum = 2.0 exactly, double the
//   legal maximum of 1.0. Slot 161 is BYTE-IDENTICAL to slot 160 (same 4
//   raw bytes, same failure) - a striking duplication worth flagging on its
//   own. The other 4 "49-entry" slots (16, 183, 200, 317) each ALSO sum to
//   EXACTLY 2.0 (verified at full double precision, not a rounding
//   coincidence) - and shifting every one of their lengths by exactly +1
//   makes every one of those 6 slots sum to EXACTLY 1.0 (a perfectly
//   complete, valid tree). Slot 598 (the LAST real slot before the
//   library's own NULL terminator at 599, entries=3276/dim=13) does NOT fit
//   this pattern - its Kraft sum is 197.91, a qualitatively different and
//   much larger overpopulation, not a simple off-by-one.
//
//   (4) Independent corroboration via the public, off-the-shelf
//   `vgmstream-cli` (official prebuilt binary r2117, same SHA-256 as the
//   independent-oracle task above, run as a pure black box, same discipline):
//   fed the SAME raw bytes for one codebookCount=32 record and one
//   codebookCount=34 record (plus one of each passing bucket for a
//   baseline) - vgmstream recognizes and FULLY decodes all four, correct
//   full-length PCM (durations/sample counts match the WAV file sizes
//   exactly), real-audio-shaped sample statistics (0% clipping, lag-1
//   autocorrelation 0.92-0.99). This rules out "the record is malformed" or
//   "uses a format vgmstream also can't handle" - these are ordinary,
//   valid, decodable records; the defect is entirely within this project's
//   own compiled-in copy of the shared codebook library
//   (`kWwiseCodebookLibraryBlob`/`wwise_codebook_library_data.cpp`), not a
//   property of the real game data, and not a bitstream-grammar/field-width
//   bug (see (3) - the grammar reads this exact slot unambiguously).
//
//   **This appears to conflict with spec-audio-codebook-library.md's own
//   2026-10-09 "Re-validated" provenance note** ("this library is ruled
//   OUT as the cause... every referenced body byte-exact and
//   Kraft-complete"), which reports reproducing this exact 488-vs-5,
//   32/34-vs-35/42 split but finding no Kraft failures. That note's own
//   headline names `vorbis_synthesis_headerin` as the failing call; on
//   this branch (already confirmed before this pass, and reconfirmed by
//   this pass's own full population run) `vorbis_synthesis_headerin`
//   ACCEPTS all 493/493 records (0 OV_EBADHEADER) - the Kraft check this
//   pass found is NOT part of `vorbis_synthesis_headerin`'s own validation
//   at all (that function's `_vorbis_unpack_books` never calls
//   `vorbis_book_init_decode`/`_make_words`); it is strictly a LATER,
//   separate libvorbis call, `vorbis_synthesis_init` -> `_vds_shared_init`
//   -> `vorbis_book_init_decode` (third_party/libvorbis/lib/block.c line
//   ~240, sharedbook.c line ~354). Whether that note tested a genuinely
//   different check (in which case there is no real conflict, just two
//   different validators answering two different questions) or the same
//   one under a mislabeled call name (in which case the two results
//   directly disagree on the same bytes) is NOT resolved here - Team B
//   cannot disassemble to adjudicate it directly. RELAYED to Team A as a
//   spec-sync question, not decided here: please re-check specifically
//   whether your own validation pass ever called the real
//   `vorbis_book_init_decode()` (or reimplemented its exact Kraft-
//   inequality/`_make_words` logic) on library slots 16/160/161/183/200/
//   317/598 in isolation, and if so, what dim/entries/lengths it computed
//   for slot 160 - this pass's own tool
//   (`wwise_vorbis_codebook_kraft_probe sweep`) is fully reproducible and
//   its slot-160 hand trace above is laid out byte-by-byte for an
//   independent re-check.
//
//   NOT fixed this pass, per "no invented fixes": (3)'s "+1 to every
//   length" pattern is a strong, exact, mathematically clean clue for 6 of
//   the 7 bad slots, but this project has no clean-room way to confirm it
//   is the TRUE correct data (vs. e.g. a wrong entries/dim count, or a
//   mistranscribed length-field-width specific to just these slots) without
//   crossing into disassembly, which Team B does not do. Patching lengths
//   to force a passing Kraft check would be exactly the kind of
//   unconfirmed, non-authentic fix this project's standing discipline
//   forbids. Full detail: results/verify_vorbis_488_codebook_kraft_
//   investigation_20261009.md.

#include <cstdint>
#include <string>
#include <vector>

namespace sr3audioruntime {

struct SetupRebuildResult {
    bool ok = false;
    // Packet type 5 (setup), WITH the standard 7-byte packet-type+"vorbis"
    // signature prefix already applied - ready to hand straight to
    // `vorbis_synthesis_headerin` via an ogg_packet, same convention
    // real_vorbis_decode.cpp's own buildSetupHeaderPacket() used for the
    // (now superseded) raw-passthrough hypothesis.
    std::vector<uint8_t> standardSetupPacket;
    std::string diagnostic;

    // Population-sweep diagnostics - filled best-effort even on failure
    // (whatever was successfully parsed before the first error), per this
    // task's own "failure reasons categorized" requirement.
    int codebookCount = 0;
    int floorCount = 0;
    int residueCount = 0;
    int mappingCount = 0;
    int modeCount = 0;

    // Per-mode blockflag (true == long block), in the same order the setup
    // header's own mode list declares them (index == the real Vorbis mode
    // number) - needed by rebuildWwiseAudioPacket() (below) to resolve the
    // audio packet's own hardcoded mode-selector bit to "is this a long
    // block," per spec-audio-runtime.md Sec11.11.4a. Populated during the
    // same mode-list parse rebuildVorbisSetupHeader() already does for the
    // standard setup packet's own mode section; empty if the rebuild failed
    // before reaching the mode list.
    std::vector<bool> modeBlockflags;
};

// Rebuilds a standard-conformant Vorbis I setup-header bitstream from one
// real Wwise-stripped setup packet's raw bytes (the packet PAYLOAD only -
// NOT including the 2-byte length-prefix framing wwise_vorbis_setup.h's
// walkFramedPackets() already strips off).
//
// `channels` is the externally supplied channel count (Sec11.11.3a:
// "channel count is not read from the packet either - it's supplied by the
// caller as popcount(decode-state+0x6c)") - this project's own real,
// already-parsed fmt-chunk channel count (StandardWaveFmt::channels), never
// re-derived or guessed from the setup packet itself.
//
// For each codebook slot, reads a bare 10-bit index off the main packet's
// shared cursor and resolves it against the compiled-in 599-entry static
// library (include/sr3audioruntime/wwise_codebook_library.h) - see this
// file's own top-of-file note for the full mechanism and its sourcing.
SetupRebuildResult rebuildVorbisSetupHeader(const uint8_t* strippedSetup, size_t strippedSetupLen, int channels);

// Result of resolveWwiseAudioModeBit() (below).
struct AudioModeBitResolution {
    bool ok = false;
    long modeBit = -1;    // the raw 0/1 value read off the packet's own first bit; -1 if !ok
    bool isLong = false;  // modeBlockflags[modeBit] - only meaningful if ok
    std::string diagnostic;
};

// Reads ONLY `packet`'s own single hardcoded mode-selector bit (spec-audio-
// runtime.md Sec11.11.4a [CONFIRMED]: the real per-packet dispatcher,
// `0x00fa4810`, pushes the plain immediate `1` as the bit-read width before
// its only bit-read call - never a value computed from the setup header's
// own declared mode count, i.e. NOT `ilog(modeBlockflags.size()-1)` bits)
// and resolves it against the setup header's own already-rebuilt mode list.
// Touches ONLY `packet`'s own bytes - this function has no way to read a
// neighboring packet's bytes even by mistake, since it is never given one.
//
// 2026-10-09 CORRECTION (this task's own first job): split out of what used
// to be rebuildWwiseAudioPacket()'s own internal `peekModeBit()` helper,
// which that function called directly on `prevPacket`/`nextPacket` raw
// bytes - i.e. it read a NEIGHBORING packet's own bytes out of its own
// sequential turn to resolve that neighbor's mode bit early. Sec11.11.4a is
// explicit that the real exe's own window-flag derivation is "NEVER read
// from the bitstream at all, not even by peeking at neighboring packets'
// own raw bytes" - that prior implementation, despite computing the
// mathematically identical final flag VALUES, used exactly the mechanism
// this sentence rules out (and is, in fact, the classic public ww2ogg-style
// technique this project's own original, now-superseded brief for this
// task described - re-implemented under a different name rather than
// genuinely replaced). This function is now the ONLY place a mode bit is
// ever read, and it is only ever called by a caller on a packet whose own
// sequential turn has arrived - see rebuildWwiseAudioPacket()'s own header
// comment below and real_vorbis_decode.cpp's per-packet loop comment for
// the full, genuinely turn-respecting mechanism.
AudioModeBitResolution resolveWwiseAudioModeBit(const uint8_t* packet, size_t packetLen,
                                                 const std::vector<bool>& modeBlockflags);

// Result of rebuildWwiseAudioPacket() (below).
struct AudioPacketRebuildResult {
    bool ok = false;
    // A standard-conformant Vorbis I audio packet, ready to hand straight to
    // `vorbis_synthesis()` via an ogg_packet (packet type 0, same convention
    // the setup/id/comment header packets above use for their own leading
    // type byte/bit).
    std::vector<uint8_t> standardPacket;
    std::string diagnostic;
};

// Rebuilds one real Wwise "modified packet" audio packet into a standard
// Vorbis I audio packet (spec-audio-runtime.md Sec11.11.4a, [CONFIRMED] from
// direct disassembly of this exe's own per-packet audio dispatcher,
// `0x00fa4810`, 2026-10-09 - supersedes this project's own earlier, public-
// format-only hypothesis for this same question).
//
// THE REAL BIT LAYOUT, CONFIRMED:
//   - Wwise's raw audio packet carries NO packet-type bit at all - not
//     omitted-but-implicit, genuinely absent. The packet's very first bit IS
//     the mode selector; nothing precedes it.
//   - The mode selector is exactly ONE HARDCODED LITERAL BIT - not a
//     computed `ceil(log2(modeCount))` field (this project's own original,
//     public-ww2ogg-derived hypothesis for this specific field, now a dated
//     NEGATIVE result for this exe). resolveWwiseAudioModeBit() (above)
//     reads exactly 1 bit, always - it structurally can never address more
//     than 2 live mode-table entries, independent of the setup header's own
//     declared mode count.
//   - For a long-block mode, the previous/next window-flag bits are NEVER
//     present in the Wwise bitstream in any form (neither explicit bits nor
//     an implicit neighbor-packet encoding) - this exe's own decoder derives
//     them from running per-call decode state (the previous packet's own
//     mode/blockflag, retained across calls) instead of reading bits.
//     `previousPacketIsLong`/`nextPacketIsLong` (below) supply the
//     mathematically IDENTICAL standard-Vorbis quantities -
//     previousWindowFlag = blockflag of the mode the PRECEDING packet
//     selects, nextWindowFlag = blockflag of the mode the FOLLOWING packet
//     selects (the ordinary public Vorbis I encoder convention any standard
//     encoder already writes).
//
//     2026-10-09 CORRECTION (this task's own first job - see
//     resolveWwiseAudioModeBit()'s own header comment above for the full
//     story): this function used to take `prevPacket`/`nextPacket` raw byte
//     pointers and read their mode bits itself, internally, at the moment
//     THIS packet was rebuilt - i.e. it peeked into a neighboring packet's
//     own bytes out of that neighbor's own sequential turn, exactly the
//     mechanism Sec11.11.4a's own text rules out for the real exe (the
//     values it computed were mathematically identical, but the mechanism
//     was not). This function no longer touches any packet's bytes other
//     than `packet`'s own - `hasPreviousPacket`/`previousPacketIsLong` and
//     `hasNextPacket`/`nextPacketIsLong` are now plain booleans the CALLER
//     supplies from its own running decode state, each resolved by calling
//     resolveWwiseAudioModeBit() on that OTHER packet at THAT packet's own
//     turn in a strictly-forward sequential loop (real_vorbis_decode.cpp's
//     per-packet loop: a one-packet submission lag - packet N is only
//     finalized and handed to `vorbis_synthesis()` once packet N+1's own
//     turn has read ITS OWN mode bit, which supplies packet N's
//     nextWindowFlag; packet N's previousWindowFlag was already known two
//     iterations earlier). This reproduces Sec11.11.4a's own "running
//     decode state carried across packets as you decode sequentially"
//     mechanism as an explicit state-threading loop instead of the exe's
//     own opaque per-decoder-instance fields (+0x54/+0x58/+0x5c/+0x60/
//     +0x64/+0x66) - a different IMPLEMENTATION STRATEGY for the identical
//     final quantities, which is as far as clean-room reimplementation is
//     meant to go (Team B does not disassemble/replicate the exe's own
//     code) - but, unlike the prior implementation, one that genuinely
//     never reads a packet's bytes before that packet's own turn.
//
// Stream-boundary edge cases (NOT given by any CONFIRMED source - explicitly
// CHOSEN here, not guessed-and-hidden): `hasPreviousPacket` == false (no
// real previous audio packet - the first real audio packet,
// framed.packets[1], which only primes the lapped-MDCT window and is known,
// both from this project's own measurement and Sec11.11.4a's own text, to
// decode to 0 samples regardless of window shape, so this choice is
// PROVABLY unobservable in the final PCM, not merely assumed safe) and
// `hasNextPacket` == false (the LAST packet, no real following packet) both
// resolve previousWindowFlag/nextWindowFlag to 0 - the same "no adjacent
// content" convention a standard encoder uses at either end of a stream.
// `previousPacketIsLong`/`nextPacketIsLong` are ignored when the
// corresponding `has*Packet` is false.
//
// `modeBlockflags` is `SetupRebuildResult::modeBlockflags` from the SAME
// record's already-accepted setup-header rebuild - the mode-number-to-
// blockflag mapping this function needs is a property of the stream's own
// setup header, not of any individual audio packet.
AudioPacketRebuildResult rebuildWwiseAudioPacket(const uint8_t* packet, size_t packetLen, bool hasPreviousPacket,
                                                  bool previousPacketIsLong, bool hasNextPacket,
                                                  bool nextPacketIsLong, const std::vector<bool>& modeBlockflags);

// Header fields parsed out of ONE codebook body by the field grammar
// described above (dim/entries/ordered always populated; maptype/valueBits/
// quantvals populated only to the extent a real parse reaches them -
// valueBits/quantvals stay 0 when maptype==0, same as the spec's own
// `value_bits_if_lookup`/`quantvals_if_lookup` CSV columns do for a
// no-lookup codebook).
//
// UPDATE 2026-10-09 (488/493 codebook-count investigation task): `lengths`
// added - the resolved per-entry codeword-length list (0 = unused entry,
// same convention transcodeOneCodebook()'s own re-emit loop uses), filled
// whenever the parse reaches the end of the length-list read (both the
// ordered and unordered/dense-or-sparse branches), regardless of whether a
// LATER field (maptype/lookup table) still fails. This is the exact input
// third_party/libvorbis/lib/sharedbook.c's own `_make_words()` (the Kraft-
// inequality "lengths must specify an overpopulated tree" check inside
// `vorbis_book_init_decode`) consumes - exposed purely so a diagnostic
// probe can run that REAL libvorbis check standalone, per codebook, without
// needing a full record/setup-header context. Pure data exposure, no
// change to rebuildVorbisSetupHeader()'s own behavior.
struct CodebookParseInfo {
    long dim = 0;
    long entries = 0;
    long ordered = 0;
    long maptype = 0;
    long valueBits = 0;
    long quantvals = 0;
    std::vector<long> lengths;
};

// Parses ONE codebook body (e.g. a slice of kWwiseCodebookLibraryBlob,
// `bytes[0:len)`) using the exact same field-grammar logic
// rebuildVorbisSetupHeader() uses for every resolved codebook slot -
// exposed standalone so spec-audio-codebook-library.md's own 599 real
// codebook bodies can each be cross-checked against the index table's own
// pre-decoded dim/entries/ordered/has_lookup/value_bits_if_lookup/
// quantvals_if_lookup columns (this task's own "strong, free correctness
// check" requirement). Returns false on any bitstream error (`err` is set);
// `info` is filled best-effort even on failure.
bool parseOneCodebookForCrossCheck(const uint8_t* bytes, size_t len, CodebookParseInfo& info, std::string& err);

// --- DIAGNOSTIC ONLY (2026-10-08, out-of-range-codebook-index bring-up
// task) - does NOT feed the real decode path (real_vorbis_decode.cpp never
// calls this). ---
//
// Tests whether the OUTER per-codebook 10-bit-index bit grammar itself (as
// opposed to the codebook LIBRARY's own completeness, spec-audio-codebook-
// library.md's 599 transcribed bodies) is the thing actually wrong, by
// empirically checking real data rather than guessing further. A codebook's
// BODY is always read from a completely separate, isolated buffer sliced
// from the static library (this file's own top-of-file note) - it never
// touches the main packet's own bitstream cursor, whether the resolved
// index is valid or not. So skipping an out-of-range index costs the main
// cursor nothing extra: exactly the same 10 bits are consumed either way.
// This function exploits that fact: it walks a real setup packet all the
// way through codebooks/floors/residues/mappings/modes WITHOUT aborting on
// an out-of-range codebook index (it just records the fact and moves on),
// then reports whether the packet's OWN declared byte length is closed
// exactly (small leftover-bit slack) once every section is read. If that
// holds for the bulk of the real population, it is strong real-data
// evidence that every field width in this file (books-count, per-slot
// index, and every section after it) is correct, and the out-of-range
// indices are purely a codebook-library-data completeness gap (missing
// bodies for slots >=599) - not a Team-B bitstream-reading bug anywhere in
// this file. If it does NOT hold, that is equally real evidence of a
// still-unfound bit-grammar bug in this outer loop or a section after it.
struct SetupBudgetProbeResult {
    bool ranToEnd = false;  // false only on a genuine EOF before the last mode's own fields
    long leftoverBits = -1;  // bits remaining in the packet after the last mode is read, when ranToEnd
    int books = 0;
    int floors = 0;
    int residues = 0;
    int maps = 0;
    int modes = 0;
    int outOfRangeCount = 0;
    std::vector<int> outOfRangeIndices;    // the actual out-of-range index values seen, in declared order
    std::vector<int> outOfRangePositions;  // their 0-based codebook-slot position within `books`
    std::string diagnostic;
};
SetupBudgetProbeResult probeSetupPacketBitBudget(const uint8_t* strippedSetup, size_t strippedSetupLen, int channels);

}  // namespace sr3audioruntime

# Free-roam audit: AUDIO / RADIO (2026-10-14)

Status: implemented / partial / missing. Specs: spec-audio-runtime.md, spec-tables-audio-radio.md, spec-audio-codebook-library.md, spec-audio-format.md.

| Area | Spec | Status | Where |
|---|---|---|---|
| Wwise bank/HIRC/media parse, Vorbis decode, sound-id to media | audio-format, codebook-library, runtime Sec1-13 | implemented | include/sr3audio, include/sr3audioruntime |
| XAudio2 one-shot output, 3D pan/attenuation for one-shots | runtime (3D emitter notes) | partial (CHOSEN attenuation curve; Wwise attenuation authored in init.bnk_pc not decoded) | audio_output_device.h, viewer playOneShot3D |
| Footsteps | runtime Sec11a.8 (event "FOOTSTEP", material switch) | partial: real left/right-foot trigger frames drive a stand-in clip; material switch and real event bank not wired | sr3_viewer.cpp footstep block |
| Radio data tables (stations, personas, playlists, events, commercials) | tables-audio-radio Sec6-16 | implemented (readers) | include/sr3tables_audio_radio |
| Radio station roster, numbering, cycle order, tune gate, OFF as station 2 | runtime 14.1-14.4, 14.6.1 (FINAL) | implemented (THIS BRANCH) | include/sr3radio/radio_sim.h |
| Default station on vehicle entry, NPC acquire roll | 14.6.1 | implemented, rules 1/3 partly omitted (PoliceFBI gives 0xFF in retail) | radio_sim.h |
| MT19937 ring, bag pick, intro/outro pick (with defect), queue builder, advance | 14.6.2 (FINAL) | implemented; unit-tested on ring vector only (queue vector needs per-station cue tables) | radio_sim.h |
| Continuous virtual timeline, join-mid-song seek, overlap-adjusted arm | 14.6.2 | implemented | radio_sim.h tick/becomeAudible |
| Per-station cue tables (12 files in misc_tables, entries 636-647) -> content table | 14.6.2 | MISSING (no reader) | - |
| Cue -> media file (radio_*_media.bnk_pc) and actual song playback | 14.6.3 | MISSING (resolution chain specified FINAL; decoder exists) | - |
| Station voice 2D/3D, in-car attenuation | 14.6.3 | OPEN in spec (init.bnk_pc) | - |
| OFF/enter-car bus events 0xC4F14EEA / 0x01DF15F0 | 14.6.3 | specified; bus ids undecoded, no backend | - |
| HUD station-logo popup image | 14.6.3 | logic implemented (hudImage()); HUD hook not wired | radio_sim.h |
| Vehicle D-pad input -> cycle, per-vehicle +0x1630 | 14.3 | logic implemented; viewer vehicle input NOT wired (in sr3vehicle/viewer vehicle code, out of my scope) | - |
| Vehicle engine sound (foley_engine.xtbl) | tables Sec10 | table reader only; no runtime engine-loop emitter spec found | - |
| Music/state switches, ambient world emitters, mixing buses/ducking | runtime Sec9-10, 14.6.3 | OPEN/PARTIAL in spec; ducking is Wwise-authored | - |
| Sing-along selector, newsbreak playback | 14.6.3 | PARTIAL spec; newsbreak posting rule specified, not implemented | - |

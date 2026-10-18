// Read a `<name>.cte_xtbl` into the playback scene model - only the
// element names the specs state (spec-xtbl-format.md 6.3/6.4,
// spec-cutscene-camera-format.md "In-game cutscene playback" B-D):
//   Cutscene row: CutsceneType (through sr3tables_cutscene::ParseSceneFile),
//     Soundtrack, BinkMovieFilename, NextScene, VintDocument,
//     RestorePlayerPos;
//   Shots/Shot: SlateName, DefinedShotLength, AnimShotLength,
//     Audios/Audio: Filename, DelayBefore, Subtitle, SubtitleDuration,
//     LipsyncCharIndex.
// Not read, because their element names are not stated: the per-shot actor
// rows (and their Animations), the Characters/Character name, an Audio
// row's lipsync-clip element and an Option row's name element. The caller
// fills those parts of the model (see HANDOFF-C.md, spec-sync question).
#pragma once

#include <string>

#include "sr3cutsceneplay/scene.h"
#include "sr3xtbl/xtbl.h"

namespace sr3cutsceneplay {

Scene ReadScene(const sr3xtbl::Document& cteXtbl, const std::string& sceneName);

}  // namespace sr3cutsceneplay

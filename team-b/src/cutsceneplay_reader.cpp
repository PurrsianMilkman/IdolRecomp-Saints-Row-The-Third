// .cte_xtbl -> playback scene model. See include/sr3cutsceneplay/scene_reader.h.

#include "sr3cutsceneplay/scene_reader.h"

#include "sr3tables_cutscene/scene_table.h"

namespace sr3cutsceneplay {

namespace {
const sr3xtbl::Node* RowParent(const sr3xtbl::Document& doc) {
    const sr3xtbl::Node* t = doc.table();
    return t ? t : doc.root();
}
std::optional<std::string> Text(const sr3xtbl::Node* n, const char* name) {
    if (const std::string* t = sr3xtbl::ChildText(n, name)) return *t;
    return std::nullopt;
}
}  // namespace

Scene ReadScene(const sr3xtbl::Document& cteXtbl, const std::string& sceneName) {
    Scene s;
    s.name = sceneName;
    const sr3tables_cutscene::SceneFields f = sr3tables_cutscene::ParseSceneFile(cteXtbl);
    // An unreadable kind reads as Story, the reader's "any other text" rule;
    // a case-only variant (OPEN in the table reader) also lands here.
    s.kind = static_cast<SceneKind>(f.kind.value_or(sr3tables_cutscene::kKindStory));

    const sr3xtbl::Node* cs = sr3xtbl::FindChild(RowParent(cteXtbl), "Cutscene");
    if (!cs) return s;
    s.soundtrack = Text(cs, "Soundtrack");
    s.binkMovieFilename = Text(cs, "BinkMovieFilename");
    s.nextScene = Text(cs, "NextScene");
    s.vintDocument = Text(cs, "VintDocument");
    s.restorePlayerPos = sr3xtbl::GetBool(cs, "RestorePlayerPos").value_or(false);

    if (const sr3xtbl::Node* shots = sr3xtbl::FindChild(cs, "Shots")) {
        for (const sr3xtbl::Node* sh : sr3xtbl::Children(shots, "Shot")) {
            SceneShot shot;
            shot.slateName = Text(sh, "SlateName").value_or("");
            if (const std::string* d = sr3xtbl::ChildText(sh, "DefinedShotLength"))
                shot.definedShotLength = sr3xtbl::ParseInt32(*d);
            if (const std::string* a = sr3xtbl::ChildText(sh, "AnimShotLength"))
                shot.animShotLength = sr3xtbl::ParseInt32(*a);
            if (const sr3xtbl::Node* audios = sr3xtbl::FindChild(sh, "Audios")) {
                for (const sr3xtbl::Node* au : sr3xtbl::Children(audios, "Audio")) {
                    AudioLine l;
                    l.filename = Text(au, "Filename").value_or("");
                    l.delayBeforeFrames = sr3xtbl::GetFloat(au, "DelayBefore").value_or(0.0f);
                    l.subtitleKey = Text(au, "Subtitle").value_or("");
                    l.subtitleDurationFrames = sr3xtbl::GetFloat(au, "SubtitleDuration").value_or(0.0f);
                    if (const std::string* c = sr3xtbl::ChildText(au, "LipsyncCharIndex"))
                        l.lipsyncCharIndex = sr3xtbl::ParseInt32(*c);
                    shot.audio.push_back(l);
                }
            }
            s.shots.push_back(shot);
        }
    }
    return s;
}

}  // namespace sr3cutsceneplay

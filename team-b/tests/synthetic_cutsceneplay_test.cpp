// Synthetic tests for sr3cutsceneplay. Every expected value comes from the
// TEXT of spec-cutscene-camera-format.md "In-game cutscene playback, end to
// end" (the F.1 05_in and F.2 22_choice vectors, the B-E rules) and
// spec-xtbl-format.md 6.3/6.4, never from this project's own source.
//
// Style follows tests/synthetic_tables_progression_test.cpp.

#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "sr3cutsceneplay/playback.h"
#include "sr3cutsceneplay/scene.h"
#include "sr3cutsceneplay/scene_reader.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"        \
                      << __LINE__ << "\n";                                 \
            ++g_failures;                                                 \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                               \
    do {                                                                    \
        ++g_checks;                                                        \
        const double va_ = (a), vb_ = (b);                                 \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                            \
            std::cerr << "CHECK_NEAR FAILED: " #a " = " << va_ << ", " #b  \
                      << " = " << vb_ << " at " __FILE__ ":" << __LINE__  \
                      << "\n";                                             \
            ++g_failures;                                                 \
        }                                                                   \
    } while (0)

using namespace sr3cutsceneplay;

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    if (g_failures != before) std::cerr << "  (in " << name << ")\n";
}

const double kFrame = 1.0 / 30.0;

// F.1 05_in: eight shots, four subtitle lines (absolute times from the
// spec's table, turned into shot + DelayBefore frames).
Scene Scene05In() {
    Scene s;
    s.name = "05_IN";
    s.kind = SceneKind::Story;
    s.soundtrack = "CINEMATIC : STORY_05_INTRO";
    for (int d : {137, 74, 114, 80, 111, 77, 112, 82}) {
        SceneShot sh;
        sh.definedShotLength = d;
        s.shots.push_back(sh);
    }
    // Shot 1: an NPC clip 0-137; #PLAYER# hidden.
    ShotActorRow bikini;
    bikini.name = "BIKINI";
    bikini.clips.push_back({"D:/x/05_IN_BIKINI_1.animx", 0, 137, 0, 1.0f});
    ShotActorRow player1;
    player1.name = "#PLAYER#";
    player1.hideChar = true;
    s.shots[0].actors = {bikini, player1};
    // Shots 5, 6 and 8: player clips (05_in_pc_5 has no female variant).
    ShotActorRow p5;
    p5.name = "#PLAYER#";
    p5.clips.push_back({"05_in_pc_5.animx", 0, 111, 0, 1.0f});
    s.shots[4].actors = {p5};
    ShotActorRow p6;
    p6.name = "#PLAYER#";
    p6.clips.push_back({"05_in_pc_6.animx", 0, 77, 0, 1.0f});
    s.shots[5].actors = {p6};
    // Subtitles: 16.1333 (shot 5 + 75), 17.8000 (shot 6 + 13),
    // 20.0667 (shot 7 + 3), 24.4000 (shot 8 + 20).
    s.shots[4].audio.push_back({"Audio Data 1", 75, "Story05_In_01_Pierce", 55, "", 0});
    s.shots[5].audio.push_back({"Audio Data 2", 13, "Story05_In_02_Player_BM", 70, "lip_pc", 1});
    s.shots[6].audio.push_back({"Audio Data 3", 3, "Story05_In_03_Shaundi", 110, "", 0});
    s.shots[7].audio.push_back({"Audio Data 4", 20, "Story05_In_04_Player_BM", 75, "", 0});
    return s;
}

struct Fired {
    double t;
    Event e;
};

std::vector<Fired> RunAll(CutscenePlayer& p, double dt, int maxSteps = 100000) {
    std::vector<Fired> all;
    for (const Event& e : p.start()) all.push_back({p.time(), e});
    for (int i = 0; i < maxSteps && p.state() != PlayState::Done; ++i)
        for (const Event& e : p.step(dt)) all.push_back({p.time(), e});
    return all;
}

// ===========================================================================
void testIdsAndNames() {
    CHECK(WwiseId("CINEMATIC") == 0x6d96bfb0u);
    CHECK(WwiseId("Cinematic_CTE") == 0x86e8dbcfu);
    CHECK(WwiseId("Cinematic_Persona") == 0xe0fc12b9u);
    CHECK(WwiseId("Stop_Cinematics") == 0x9895c6ceu);
    CHECK(WwiseId("STORY_05_INTRO") == 0x32492709u);
    CHECK(WwiseId("M22_CTE") == 0x5da8d7a5u);

    auto sw = ParseSoundtrack("CINEMATIC : STORY_05_INTRO");
    CHECK(sw.has_value());
    CHECK(sw->group == "CINEMATIC" && sw->value == "STORY_05_INTRO");
    CHECK(sw->groupId == 0x6d96bfb0u && sw->valueId == 0x32492709u);
    CHECK(sw->bankFile == "story_05_intro.bnk_pc");
    CHECK(ParseSoundtrack("CINEMATIC : M22_CTE")->bankFile == "m22_cte.bnk_pc");
    CHECK(!ParseSoundtrack("no colon").has_value());
    CHECK(!ParseSoundtrack(":x").has_value());

    CHECK(BinkMoviePath("D:\\build\\videos\\22_choice.bik") == "videos\\22_choice.bik");
    CHECK(BinkMoviePath("art/videos/02_IN_v2.bik") == "videos\\02_IN_v2.bik");
    CHECK(ScenePackageName("05_IN", false) == "05_IN.str2_pc");
    CHECK(ScenePackageName("05_IN", true) == "05_IN_f.str2_pc");

    CHECK(ClipName("D:/x/y/05_IN_BIKINI_1.animx") == "05_in_bikini_1");
    CHECK(ClipName("C:\\a\\05_in_pc_6.ANIMX") == "05_in_pc_6");
    const std::set<std::string> have = {"05_in_pc_5", "05_in_pc_6", "f_05_in_pc_6", "f_05_in_pc_8"};
    auto exists = [&](const std::string& n) { return have.count(n) != 0; };
    CHECK(ResolveClipName("05_in_pc_6.animx", true, exists) == "f_05_in_pc_6");
    CHECK(ResolveClipName("05_in_pc_5.animx", true, exists) == "05_in_pc_5");  // shot 5 fallback
    CHECK(ResolveClipName("05_in_pc_6.animx", false, exists) == "05_in_pc_6");

    CHECK(IsPlayerActorName("Player") && IsPlayerActorName("#Player#") && IsPlayerActorName("#PLAYER#"));
    CHECK(!IsPlayerActorName("BIKINI"));
    CHECK(ShotActorObjectName("#PLAYER#") == "#PLAYER1#");
    CHECK(ShotActorObjectName("CLARA") == "CLARA");
    CHECK(SceneActorObjectName("Pierce", SceneKind::Zscene, true) == "Pierce_cs");
    CHECK(SceneActorObjectName("Pierce", SceneKind::Zscene, false) == "Pierce");
    CHECK(SceneActorObjectName("Pierce", SceneKind::Story, true) == "Pierce");

    SceneCharacter angel;
    angel.isAngel = true;
    angel.mesh = "ignored";
    CHECK(CharacterMesh(angel, {true, false, false}) == "npc_Angel_clean");
    CHECK(CharacterMesh(angel, {false, true, false}) == "Npc_Angel_withmask");
    CHECK(CharacterMesh(angel, {}) == "npc_Angel_nomask");
    SceneCharacter kb;
    kb.mesh = "npc_killbane";
    kb.killbaneMesh = "npc_killbane_alt";
    CHECK(CharacterMesh(kb, {}) == "npc_killbane");
    CHECK(CharacterMesh(kb, {false, false, true}) == "npc_killbane_alt");
}

void testTimelineF1() {
    const Scene s = Scene05In();
    const std::vector<ShotSpan> sp = ShotSpans(s);
    const double starts[] = {0.0, 4.6, 7.1, 10.9333, 13.6333, 17.3667, 19.9667, 23.7333};
    const double ends[] = {4.6, 7.1, 10.9333, 13.6333, 17.3667, 19.9667, 23.7333, 26.5};
    CHECK(sp.size() == 8);
    for (size_t i = 0; i < 8; ++i) {
        CHECK_NEAR(sp[i].start, starts[i], 1e-4);
        CHECK_NEAR(sp[i].end, ends[i], 1e-4);
    }
    CHECK_NEAR(SceneDuration(s), 26.5, 1e-9);  // 795 frames; the XML sum is 787
}

void testPlaybackF1() {
    PlaybackOptions o;
    o.playerPersonaId = 0x1234;
    o.femalePlayer = true;
    const std::set<std::string> have = {"f_05_in_pc_6"};
    o.clipExists = [&](const std::string& n) { return have.count(n) != 0; };
    CutscenePlayer p(Scene05In(), o);
    const std::vector<Fired> all = RunAll(p, kFrame);

    // Load and start.
    CHECK(all.size() > 4);
    CHECK(all[0].e.kind == EventKind::BankLoad && all[0].e.text == "story_05_intro.bnk_pc" &&
          all[0].e.id0 == 0x32492709u);
    bool sawStart = false, sawCamera = false;
    for (const Fired& f : all) {
        if (f.e.kind == EventKind::SoundtrackStart) {
            sawStart = true;
            CHECK(f.e.id0 == 0x6d96bfb0u && f.e.id1 == 0x32492709u && f.e.id2 == 0x1234u);
        }
        if (f.e.kind == EventKind::CameraEnter) sawCamera = true;
    }
    CHECK(sawStart && sawCamera);

    // Shot begins at the F.1 times; clip names; subtitle times.
    std::vector<double> shotTimes;
    std::vector<std::pair<double, std::string>> subs;
    std::vector<std::string> clipStarts;
    double lipsyncT = -1, sub2T = -1;
    bool hidePlayerShot1 = false;
    for (const Fired& f : all) {
        if (f.e.kind == EventKind::ShotBegin) shotTimes.push_back(f.t);
        if (f.e.kind == EventKind::ClipStart) clipStarts.push_back(f.e.text);
        if (f.e.kind == EventKind::SubtitleShow) {
            subs.push_back({f.t, f.e.text});
            if (f.e.text == "Story05_In_02_Player_BM") sub2T = f.t;
        }
        if (f.e.kind == EventKind::LipsyncStart) lipsyncT = f.t;
        if (f.e.kind == EventKind::ActorSetup && f.e.shot == 0 && f.e.text == "#PLAYER1#" && f.e.flag)
            hidePlayerShot1 = true;
    }
    CHECK(hidePlayerShot1);
    CHECK(shotTimes.size() == 8);
    const double starts[] = {0.0, 4.6, 7.1, 10.9333, 13.6333, 17.3667, 19.9667, 23.7333};
    for (size_t i = 0; i < shotTimes.size() && i < 8; ++i) CHECK_NEAR(shotTimes[i], starts[i], kFrame + 1e-3);
    CHECK(clipStarts.size() == 3);
    CHECK(clipStarts[0] == "05_in_bikini_1");
    CHECK(clipStarts[1] == "05_in_pc_5");   // no female variant: plain name
    CHECK(clipStarts[2] == "f_05_in_pc_6");
    const double subTimes[] = {16.1333, 17.8, 20.0667, 24.4};
    CHECK(subs.size() == 4);
    for (size_t i = 0; i < subs.size() && i < 4; ++i) CHECK_NEAR(subs[i].first, subTimes[i], kFrame + 1e-3);
    CHECK(subs[3].second == "Story05_In_04_Player_BM");
    CHECK(sub2T > 0 && lipsyncT > 0);
    CHECK_NEAR(sub2T - lipsyncT, 0.1, kFrame + 1e-3);  // lipsync 0.1 s earlier

    // End: a 0 ms fade request at <= 0.8 s left, stop, teardown, fade-in 500.
    double fadeT = -1;
    bool stopped = false, finished = false, soundStop = false, fadeIn500 = false;
    for (const Fired& f : all) {
        if (f.e.kind == EventKind::FadeOut) {
            fadeT = f.t;
            CHECK(f.e.value == 0.0);
        }
        if (f.e.kind == EventKind::PlaybackStopped) stopped = !f.e.flag;
        if (f.e.kind == EventKind::SoundtrackStop) soundStop = true;
        if (f.e.kind == EventKind::FadeIn) fadeIn500 = f.e.value == 500.0;
        if (f.e.kind == EventKind::Finished) finished = true;
    }
    CHECK_NEAR(fadeT, 26.5 - 0.8, kFrame + 1e-3);
    CHECK(stopped && soundStop && fadeIn500 && finished);
    CHECK(!p.skipped());
    CHECK_NEAR(p.time(), 26.5, 1e-9);
}

void testBinkF2() {
    Scene s;
    s.name = "22_choice";
    s.kind = SceneKind::Story;
    s.soundtrack = "CINEMATIC : M22_CTE";
    s.binkMovieFilename = "D:\\art\\videos\\22_choice.bik";
    SceneShot sh;
    sh.slateName = "Shot_1-v_cam01_1";
    sh.definedShotLength = 1128;
    SceneOption fs;
    fs.name = "Fullscreen Bink Play";
    fs.enabled = true;
    sh.options.push_back(fs);
    ShotActorRow pc;
    pc.name = "#PLAYER#";
    pc.clips.push_back({"22_choice_pc_1.animx", 0, 1128, 0, 1.0f});
    sh.actors.push_back(pc);
    sh.audio.push_back({"Audio Data 1", 24, "22_Choice_01_Angel", 64, "", 0});
    s.shots.push_back(sh);
    CHECK_NEAR(SceneDuration(s), 37.6333, 1e-4);

    CutscenePlayer p(s, {});
    const std::vector<Fired> all = RunAll(p, kFrame);
    bool open = false, push = false, stop = false;
    double subT = -1;
    size_t pushIdx = 0, shotIdx = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        const Fired& f = all[i];
        if (f.e.kind == EventKind::BinkOpen) open = f.e.text == "videos\\22_choice.bik";
        if (f.e.kind == EventKind::ShotBegin && f.e.shot == 0) shotIdx = i;
        if (f.e.kind == EventKind::BinkFullscreenPush) {
            push = true;
            pushIdx = i;
        }
        if (f.e.kind == EventKind::BinkStop) stop = true;
        if (f.e.kind == EventKind::SoundtrackStart) CHECK(f.e.id1 == 0x5da8d7a5u);
        if (f.e.kind == EventKind::SubtitleShow) subT = f.t;
    }
    CHECK(open && push && stop);
    CHECK(pushIdx > shotIdx);  // fires at shot begin
    CHECK_NEAR(subT, 0.8, kFrame + 1e-3);
    CHECK_NEAR(p.time(), 37.6333, 1e-4);

    // Bink options do nothing without a movie; Bink Play < 0 pauses.
    Scene nm = s;
    nm.binkMovieFilename.reset();
    CutscenePlayer q(nm, {});
    for (const Fired& f : RunAll(q, kFrame)) {
        CHECK(f.e.kind != EventKind::BinkFullscreenPush);
        CHECK(f.e.kind != EventKind::BinkOpen);
    }
    Scene bp = s;
    bp.shots[0].options = {SceneOption{"Bink Play", std::nullopt, -1.0f, std::nullopt, false},
                           SceneOption{"Bink Play", std::nullopt, 12.0f, std::nullopt, false}};
    CutscenePlayer r(bp, {});
    int pauses = 0, plays = 0;
    for (const Event& e : r.start()) {
        if (e.kind == EventKind::BinkPause) ++pauses;
        if (e.kind == EventKind::BinkPlay && e.value == 12.0) ++plays;
    }
    CHECK(pauses == 1 && plays == 1);
}

void testSkipPlain() {
    Scene s = Scene05In();
    CutscenePlayer p(s, {});
    p.start();
    for (int i = 0; i < 59; ++i) p.step(kFrame);  // 1.97 s
    CHECK(!p.requestSkip());                     // not > 2000 ms yet
    for (int i = 0; i < 2; ++i) p.step(kFrame);
    CHECK(!p.requestSkip(true));                 // a blocking flag
    CHECK(p.requestSkip());
    CHECK(p.state() == PlayState::SkipRequested);
    const std::vector<Event> ev = p.step(kFrame);
    bool fade0 = false, skippedStop = false, fadeIn = false;
    for (const Event& e : ev) {
        if (e.kind == EventKind::FadeOut) fade0 = e.value == 0.0;
        if (e.kind == EventKind::PlaybackStopped) skippedStop = e.flag;
        if (e.kind == EventKind::FadeIn) fadeIn = e.value == 500.0;
    }
    CHECK(fade0 && skippedStop && fadeIn);
    CHECK(p.skipped() && p.state() == PlayState::Done);
    CHECK(!p.requestSkip());  // nothing to skip any more

    // Zscene: a 125 ms fade, then a 125 ms fade-in.
    Scene z = Scene05In();
    z.kind = SceneKind::Zscene;
    CutscenePlayer q(z, {});
    q.start();
    for (int i = 0; i < 70; ++i) q.step(kFrame);
    CHECK(q.requestSkip());
    bool f125 = false, in125 = false, unmute = false;
    for (const Event& e : q.step(kFrame)) {
        if (e.kind == EventKind::FadeOut) f125 = e.value == 125.0;
        if (e.kind == EventKind::FadeIn) in125 = e.value == 125.0;
        if (e.kind == EventKind::ZsceneUnmute) unmute = true;
    }
    CHECK(f125 && in125 && unmute);
}

void testSkipTailAndCompletion() {
    Scene s = Scene05In();
    s.tailHoldSeconds = 1.5;
    CutscenePlayer p(s, {});
    p.start();
    for (int i = 0; i < 90; ++i) p.step(kFrame);  // 3 s
    CHECK(p.requestSkip());
    std::vector<Event> ev = p.step(kFrame);
    CHECK(ev.size() == 1 && ev[0].kind == EventKind::FadeOut && ev[0].flag);
    CHECK(p.step(kFrame).empty());  // waiting for the fade
    p.notifyFadedOut();
    ev = p.step(kFrame);
    bool seek = false, stopSound = false, lastShot = false;
    for (const Event& e : ev) {
        if (e.kind == EventKind::SoundtrackSeek) seek = true;
        if (e.kind == EventKind::SoundtrackStop) stopSound = e.flag;
        if (e.kind == EventKind::ShotBegin && e.shot == 7) lastShot = true;
    }
    CHECK(seek && stopSound && lastShot);
    CHECK_NEAR(p.time(), 26.5 - 1.5, 1e-9);
    CHECK(p.state() == PlayState::Playing);
    // The tail plays out, then the scene ends as skipped.
    bool stoppedSkipped = false;
    for (int i = 0; i < 200 && p.state() != PlayState::Done; ++i)
        for (const Event& e : p.step(kFrame))
            if (e.kind == EventKind::PlaybackStopped) stoppedSkipped = e.flag;
    CHECK(stoppedSkipped);

    // Completion screen: no skip with 0.6 s or less left; the end waits for
    // the HUD screens.
    Scene c = Scene05In();
    c.completionScreen = true;
    CutscenePlayer q(c, {});
    q.start();
    while (q.duration() - q.time() > 0.6) q.step(kFrame);
    CHECK(!q.requestSkip());
    q.setCompletionHudGone(false);
    for (int i = 0; i < 60; ++i) q.step(kFrame);
    CHECK(q.state() != PlayState::Done);
    q.setCompletionHudGone(true);
    q.step(kFrame);
    CHECK(q.state() == PlayState::Done);

    // The "Show Completion Screen" option sets the flag.
    Scene o = Scene05In();
    o.shots[0].options.push_back(SceneOption{"Show Completion Screen", true, std::nullopt, std::nullopt, false});
    CutscenePlayer r(o, {});
    r.start();
    CHECK(r.scene().completionScreen);
}

void testNextSceneZsceneFadePauseAndOptions() {
    Scene s;
    s.name = "17_out";
    s.kind = SceneKind::Zscene;
    s.nextScene = "17_out_pt2";
    SceneShot a;
    a.definedShotLength = 29;  // 1.0 s
    s.shots.push_back(a);
    CutscenePlayer p(s, {});
    const std::vector<Fired> all = RunAll(p, kFrame);
    bool chain = false, fadeIn = false;
    double fadeT = -1;
    for (const Fired& f : all) {
        if (f.e.kind == EventKind::ChainNextScene) chain = f.e.text == "17_out_pt2";
        if (f.e.kind == EventKind::FadeIn) fadeIn = true;
        if (f.e.kind == EventKind::FadeOut) {
            fadeT = f.t;
            CHECK(f.e.value == 125.0);
        }
    }
    CHECK(chain && !fadeIn);
    CHECK_NEAR(fadeT, 1.0 - 0.125, kFrame + 1e-3);

    // Pause holds the clock and pauses the soundtrack.
    CutscenePlayer q(Scene05In(), {});
    q.start();
    q.step(kFrame);
    const double t = q.time();
    CHECK(q.pause().size() == 1);
    q.step(1.0);
    CHECK(q.time() == t);
    CHECK(q.resume().size() == 1);
    q.step(kFrame);
    CHECK(q.time() > t);

    // Pre-fire options fire one shot ahead; Designer scenes have no camera.
    Scene d = Scene05In();
    d.kind = SceneKind::Designer;
    d.shots[1].options.push_back(SceneOption{"Some Option", true, std::nullopt, std::nullopt, true});
    CutscenePlayer r(d, {});
    const std::vector<Event> ev = r.start();
    bool camera = false, early = false;
    for (const Event& e : ev) {
        if (e.kind == EventKind::CameraEnter) camera = true;
        if (e.kind == EventKind::ShotOption && e.shot == 1) early = true;
    }
    CHECK(!camera && early);
}

void testReader() {
    const char* xml =
        "<root><Table><Cutscene>"
        "<CutsceneType>Story</CutsceneType>"
        "<Soundtrack>CINEMATIC : STORY_05_INTRO</Soundtrack>"
        "<BinkMovieFilename>D:\\videos\\x.bik</BinkMovieFilename>"
        "<VintDocument>cte_binoc_only</VintDocument>"
        "<NextScene>17_out_pt2</NextScene>"
        "<RestorePlayerPos>True</RestorePlayerPos>"
        "<Shots>"
        "<Shot><SlateName>S1</SlateName><DefinedShotLength>137</DefinedShotLength>"
        "<AnimShotLength>140</AnimShotLength></Shot>"
        "<Shot><SlateName>S2</SlateName><DefinedShotLength>74</DefinedShotLength>"
        "<Audios><Audio><Filename>Audio Data 1</Filename><DelayBefore>75</DelayBefore>"
        "<Subtitle>Story05_In_01_Pierce</Subtitle><SubtitleDuration>55</SubtitleDuration>"
        "<LipsyncCharIndex>2</LipsyncCharIndex></Audio></Audios></Shot>"
        "</Shots></Cutscene></Table></root>";
    const Scene s = ReadScene(sr3xtbl::ParseDocument(xml), "05_IN");
    CHECK(s.name == "05_IN");
    CHECK(s.kind == SceneKind::Story);
    CHECK(s.soundtrack && *s.soundtrack == "CINEMATIC : STORY_05_INTRO");
    CHECK(s.binkMovieFilename && BinkMoviePath(*s.binkMovieFilename) == "videos\\x.bik");
    CHECK(s.vintDocument && *s.vintDocument == "cte_binoc_only");
    CHECK(s.nextScene && *s.nextScene == "17_out_pt2");
    CHECK(s.restorePlayerPos);
    CHECK(s.shots.size() == 2);
    CHECK(s.shots[0].slateName == "S1" && s.shots[0].definedShotLength == 137);
    CHECK(s.shots[0].animShotLength && *s.shots[0].animShotLength == 140);
    CHECK(s.shots[1].audio.size() == 1);
    CHECK(s.shots[1].audio[0].delayBeforeFrames == 75.0);
    CHECK(s.shots[1].audio[0].subtitleKey == "Story05_In_01_Pierce");
    CHECK(s.shots[1].audio[0].subtitleDurationFrames == 55.0);
    CHECK(s.shots[1].audio[0].lipsyncCharIndex == 2);
    CHECK_NEAR(SceneDuration(s), (138 + 75) / 30.0, 1e-12);

    const Scene z = ReadScene(sr3xtbl::ParseDocument(
        "<root><Table><Cutscene><CutsceneType>Zscene</CutsceneType></Cutscene></Table></root>"), "z");
    CHECK(z.kind == SceneKind::Zscene && z.shots.empty() && !z.soundtrack);
}

}  // namespace

int main() {
    run("testIdsAndNames", testIdsAndNames);
    run("testTimelineF1", testTimelineF1);
    run("testPlaybackF1", testPlaybackF1);
    run("testBinkF2", testBinkF2);
    run("testSkipPlain", testSkipPlain);
    run("testSkipTailAndCompletion", testSkipTailAndCompletion);
    run("testNextSceneZsceneFadePauseAndOptions", testNextSceneZsceneFadePauseAndOptions);
    run("testReader", testReader);
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
